#!/usr/bin/env python3
"""Local macOS browser frontend for the real native RickyOS simulator; no device I/O."""
import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import secrets
import shlex
import shutil
import signal
import socket
import socketserver
import struct
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit
import zlib
import webbrowser

ROOT = Path(__file__).resolve().parents[2]
ASSETS = Path(__file__).resolve().parent
MAX_PIXELS = 1216 * 684
KEYS = {'BACK', 'ENTER', 'UP', 'DOWN', 'LEFT', 'RIGHT', 'POWER', 'SLEEP'}


def build_toolchain():
    """Prefer this checkout's environment, then PATH; retain local-review compatibility."""
    environment = os.environ.copy()
    local_pio = ROOT / '.venv/bin/pio'
    if local_pio.is_file():
        return str(local_pio), environment
    path_pio = shutil.which('pio')
    if path_pio:
        return path_pio, environment
    if ROOT.name == 'candidate' and ROOT.parent.parent.name == 'upgrade-review':
        toolchain = ROOT.parents[2] / 'work/crossmux'
        review_pio = toolchain / '.venv/bin/pio'
        if review_pio.is_file():
            environment.setdefault('PLATFORMIO_CORE_DIR', str(toolchain / '.platformio'))
            return str(review_pio), environment
    raise RuntimeError('没有找到 pio；请安装 pioarduino 并激活项目 .venv。')


def frame_to_png(raw):
    if len(raw) < 16:
        raise ValueError('incomplete frame')
    magic, width, height, sequence = struct.unpack_from('<4sIII', raw)
    if (magic != b'RKB1' or width < 1 or height < 1 or width > 1216 or height > 1216
            or width * height > MAX_PIXELS or len(raw) != 16 + width * height):
        raise ValueError('invalid native frame')
    # Lossless encoding of the simulator's grayscale pixels, not a second UI renderer.
    scanlines = b''.join(b'\0' + raw[16 + y * width:16 + (y + 1) * width] for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(scanlines, 2)) + chunk(b'IEND', b''))
    return png, width, height, sequence


def input_command(data):
    if not isinstance(data, dict):
        raise ValueError('invalid input')
    if data.get('type') == 'key':
        key = data.get('key')
        duration = data.get('duration', 100)
        if key not in KEYS or type(duration) is not int or not 40 <= duration <= 1500:
            raise ValueError('invalid key')
        return f'K {key} {duration}'.encode()
    if data.get('type') == 'touch':
        values = [data.get(key) for key in ('x1', 'y1', 'x2', 'y2')]
        if any(type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1 for value in values):
            raise ValueError('invalid touch coordinates')
        duration = data.get('duration', 100)
        if type(duration) is not int or not 40 <= duration <= 650:
            raise ValueError('invalid touch duration')
        return ('T ' + ' '.join(f'{value:.6f}' for value in values) + f' {duration}').encode()
    raise ValueError('unknown input type')


def seed_demo(sd):
    """Only initialize a new private demo card; never overwrite its existing settings."""
    sd.mkdir(parents=True, exist_ok=True)
    settings_dir = sd / '.crosspoint'
    if (settings_dir / 'settings.json').exists():
        return
    settings_dir.mkdir(exist_ok=True)
    (sd / 'books').mkdir(exist_ok=True)
    (sd / 'images').mkdir(exist_ok=True)
    books = [
        ('阅读与生活', '这是 RickyOS 浏览器预览里的演示图书。\n\n慢一点，读几页。\n\n'
         '首页可以继续上一次阅读，在书库查找图书，在储存整理文件。这里的设置只保存在模拟器里。\n'),
        ('随身书房', '在一个安静的下午，带着一本书出门。\n\n这一页只用来测试中文排版和翻页。\n'),
        ('RickyOS 使用指南', '点击底部图标切换首页、书库、储存、应用和设置。\n\n'
         '点击头像可以修改个人资料。左侧返回按钮对应设备返回操作。\n\n'
         '浏览器画面来自真实固件模拟器；墨水屏残影和功耗仍需要实机检查。\n')]
    recent = []
    for title, text in books:
        (sd / 'books' / (title + '.txt')).write_text((text + '\n') * 12, encoding='utf-8')
        recent.append({'path': '/books/' + title + '.txt', 'title': title, 'author': 'Ricky AI Studio', 'coverBmpPath': ''})
    settings = {'uiTheme': 5, 'inxTabPosition': 1, 'language': 'ZH_CN', 'onboardingVersion': 1,
                'orientation': 0, 'clockTimezone': 255, 'clockUtcOffsetQ': 32,
                'clockAutoSync': 0, 'rickyNickname': 'Ricky', 'rickyAvatarPath': '',
                'standbyShortcutEnabled': 1, 'sleepTimeoutMinutes': 60}
    (settings_dir / 'settings.json').write_text(json.dumps(settings, ensure_ascii=False), encoding='utf-8')
    (settings_dir / 'recent.json').write_text(json.dumps({'books': recent}, ensure_ascii=False), encoding='utf-8')
    (settings_dir / 'user-guide.checked').write_text('1')


class Preview:
    def __init__(self, state_dir, port):
        self.state_dir = state_dir.resolve()
        self.state_dir.mkdir(parents=True, exist_ok=True)
        self.state_lock = (self.state_dir / 'preview.lock').open('a')
        try:
            fcntl.flock(self.state_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            self.state_lock.close()
            raise RuntimeError('这份演示 SD 卡已被另一个预览占用，请使用不同的 --state-dir。') from None
        self.sd = self.state_dir / 'sd'
        seed_demo(self.sd)
        # Keep Unix socket path short; workspace paths exceed Darwin's 104-byte limit.
        self.transport = Path(tempfile.mkdtemp(prefix='ricky-web-'))
        self.port = port
        self.token = secrets.token_urlsafe(32)
        self.lock = threading.RLock()
        self.frame_lock = threading.Lock()
        self.process = None
        self.build_process = None
        self.build_thread = None
        self.closing = False
        self.busy = False
        self.error = ''
        self.frame_stamp = None
        self.frame = None
        self.latest_png = None
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.socket.settimeout(0.5)
        self.library = self.state_dir / 'bridge.dylib'
        self.binary = ROOT / '.pio/build/simulator_rickyos/program'
        self.compile_bridge()
        self.start()

    def compile_bridge(self):
        if platform.system() != 'Darwin':
            raise RuntimeError('此浏览器桥接版目前支持 macOS。')
        source = ASSETS / 'bridge.cpp'
        if self.library.exists() and self.library.stat().st_mtime_ns >= source.stat().st_mtime_ns:
            return
        flags = shlex.split(subprocess.check_output(['sdl2-config', '--cflags', '--libs'], text=True))
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-dynamiclib', str(source),
                        '-o', str(self.library), *flags], check=True)

    def start(self):
        with self.lock:
            if self.closing:
                return
            if not self.binary.is_file():
                raise RuntimeError('请先编译 simulator_rickyos；参见同目录 README。')
            self.stop()
            self.error = ''
            for name in ('frame.gray', 'frame.tmp', 'input.sock'):
                path = self.transport / name
                if path.exists():
                    path.unlink()  # Exact owned bridge transport files, never SD content.
            with self.frame_lock:
                self.frame_stamp = self.frame = self.latest_png = None
            environment = os.environ.copy()
            # No inherited automation can click/delete files or quit this interactive session.
            for key in list(environment):
                if key.startswith('CROSSPOINT_SIM_') or key == 'DYLD_INSERT_LIBRARIES':
                    environment.pop(key)
            environment.update(SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                               DYLD_INSERT_LIBRARIES=str(self.library), RICKYOS_BRIDGE_DIR=str(self.transport),
                               CROSSPOINT_SIM_SD=str(self.sd), CROSSPOINT_SIM_HTTP_PORT=str(self.port + 10))
            with (self.state_dir / 'simulator.log').open('wb') as log:
                self.process = subprocess.Popen([str(self.binary)], cwd=ROOT, env=environment,
                                                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

    def status(self):
        with self.lock:
            alive = self.process is not None and self.process.poll() is None
            activity = ''
            path = self.state_dir / 'simulator.log'
            if path.exists():
                with path.open('rb') as log:
                    log.seek(max(0, path.stat().st_size - 32768))
                    entries = re.findall(r'Entering activity: ([\w-]+)', log.read().decode('utf-8', errors='replace'))
                    if entries:
                        activity = entries[-1]
            return {'product': 'RickyOS-browser-preview', 'alive': alive, 'building': self.busy, 'activity': activity,
                    'error': self.error, 'input_ready': (self.transport / 'input.sock').exists(),
                    'binary_updated': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(self.binary.stat().st_mtime))}

    def get_frame(self):
        with self.frame_lock:
            path = self.transport / 'frame.gray'
            try:
                stat = path.stat()
                stamp = (stat.st_mtime_ns, stat.st_size)
                if stamp != self.frame_stamp:
                    raw = path.read_bytes()
                    png, width, height, _ = frame_to_png(raw)
                    self.frame = (png, width, height, hashlib.sha256(raw).hexdigest())
                    self.frame_stamp = stamp
                    self.latest_png = png
            except (FileNotFoundError, ValueError):
                pass
            return self.frame

    def input(self, data):
        command = input_command(data)
        with self.lock:
            if self.process is None or self.process.poll() is not None:
                raise RuntimeError('模拟器未运行，请重新启动。')
            self.socket.sendto(command, str(self.transport / 'input.sock'))

    def rebuild(self):
        with self.lock:
            if self.closing:
                raise RuntimeError('预览正在关闭。')
            if self.busy:
                raise RuntimeError('正在编译，请稍候。')
            self.busy = True
            self.error = ''
        def worker():
            try:
                pio, environment = build_toolchain()
                with (self.state_dir / 'build.log').open('wb') as log:
                    with self.lock:
                        if self.closing:
                            return
                        self.build_process = subprocess.Popen([str(pio), 'run', '-e', 'simulator_rickyos'],
                                                              cwd=ROOT, env=environment, stdout=log,
                                                              stderr=subprocess.STDOUT, start_new_session=True)
                    if self.build_process.wait() != 0:
                        raise RuntimeError('编译失败，旧预览仍保留；查看 build.log。')
                self.start()
            except (OSError, RuntimeError) as error:
                self.error = str(error)
            finally:
                with self.lock:
                    self.busy = False
        with self.lock:
            self.build_thread = threading.Thread(target=worker, daemon=True)
            self.build_thread.start()

    def close(self):
        with self.lock:
            self.closing = True
            self.stop()
            if self.build_process and self.build_process.poll() is None:
                # Only our own build process group; never a device upload command.
                os.killpg(self.build_process.pid, signal.SIGTERM)
                try:
                    self.build_process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(self.build_process.pid, signal.SIGKILL)
                    self.build_process.wait()
        if self.build_thread:
            self.build_thread.join(timeout=5)
        self.socket.close()
        self.state_lock.close()


class LocalServer(ThreadingHTTPServer):
    def server_bind(self):
        # Avoid reverse DNS lookups (which can stall startup on some Mac networks).
        socketserver.TCPServer.server_bind(self)
        self.server_name = 'localhost'
        self.server_port = self.server_address[1]

    def get_request(self):
        connection, address = super().get_request()
        connection.settimeout(5)
        return connection, address


def handler_for(preview):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def local_request(self):
            return self.headers.get('Host') in {f'127.0.0.1:{preview.port}', f'localhost:{preview.port}'}

        def send(self, code, body=b'', content_type='application/json', extra=None):
            self.send_response(code)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob:; frame-ancestors 'none'")
            for key, value in (extra or {}).items():
                self.send_header(key, value)
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_GET(self):
            if not self.local_request():
                return self.send(403)
            path = urlsplit(self.path).path
            if path in {'/', '/index.html'}:
                return self.send(200, (ASSETS / 'index.html').read_bytes().replace(b'__TOKEN__', preview.token.encode()), 'text/html; charset=utf-8')
            if path in {'/app.js', '/style.css'}:
                kind = 'text/javascript' if path == '/app.js' else 'text/css'
                return self.send(200, (ASSETS / path[1:]).read_bytes(), kind + '; charset=utf-8')
            if path == '/api/status':
                return self.send(200, json.dumps(preview.status()).encode())
            if path in {'/api/frame', '/api/screenshot'}:
                frame = preview.get_frame()
                if not frame:
                    return self.send(503, b'{"error":"waiting for first frame"}')
                png, width, height, etag = frame
                headers = {'ETag': '"' + etag + '"', 'X-Frame-Width': str(width), 'X-Frame-Height': str(height)}
                if path == '/api/frame' and self.headers.get('If-None-Match') == headers['ETag']:
                    return self.send(304, extra=headers)
                if path == '/api/screenshot':
                    headers['Content-Disposition'] = 'attachment; filename="RickyOS-preview.png"'
                return self.send(200, png, 'image/png', headers)
            return self.send(404)

        def do_POST(self):
            origin = self.headers.get('Origin')
            origins = {f'http://127.0.0.1:{preview.port}', f'http://localhost:{preview.port}'}
            if (not self.local_request() or self.headers.get('X-Ricky-Token') != preview.token
                    or (origin is not None and origin not in origins)):
                return self.send(403)
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 0 <= length <= 2048:
                    return self.send(413)
                data = json.loads(self.rfile.read(length) or b'{}')
                path = urlsplit(self.path).path
                if path == '/api/input':
                    preview.input(data)
                elif path == '/api/restart':
                    if preview.busy:
                        raise RuntimeError('正在更新预览，请稍候。')
                    preview.start()
                elif path == '/api/rebuild':
                    preview.rebuild()
                else:
                    return self.send(404)
                return self.send(200, b'{"ok":true}')
            except (ValueError, TypeError) as error:
                return self.send(400, json.dumps({'error': str(error)}).encode())
            except (OSError, RuntimeError) as error:
                return self.send(503, json.dumps({'error': str(error)}, ensure_ascii=False).encode())
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--state-dir', type=Path, default=ROOT / '.cache/rickyos-browser')
    parser.add_argument('--open', action='store_true', help='启动后打开默认浏览器')
    args = parser.parse_args()
    if not 1024 <= args.port <= 65524:
        parser.error('port must be between 1024 and 65524')
    # Bind before launching a second simulator when the page is already running.
    server = LocalServer(('127.0.0.1', args.port), BaseHTTPRequestHandler)
    try:
        preview = Preview(args.state_dir, args.port)
    except Exception:
        server.server_close()
        raise
    server.RequestHandlerClass = handler_for(preview)
    def interrupted(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    print(f'RickyOS 浏览器预览：http://127.0.0.1:{args.port}/', flush=True)
    print('只运行电脑模拟器，不连接设备。关闭终端或按 Ctrl+C 停止。', flush=True)
    if args.open:
        webbrowser.open(f'http://127.0.0.1:{args.port}/')
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        preview.close()
        server.server_close()


if __name__ == '__main__':
    main()
