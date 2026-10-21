#!/usr/bin/env python3
"""Exercise the local native firmware preview API; never connects to hardware."""
import argparse
import json
from pathlib import Path
import re
import time
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', type=int, default=8790)
parser.add_argument('--tap', nargs=2, type=float)
parser.add_argument('--hold', type=int, default=100)
parser.add_argument('--key')
parser.add_argument('--restart', action='store_true')
parser.add_argument('--swipe', nargs=4, type=float)
parser.add_argument('--capture', type=Path)
parser.add_argument('--wait', type=float, default=0.8)
args = parser.parse_args()
if not 1024 <= args.port <= 65524 or not 0 <= args.wait <= 10:
    parser.error('invalid port or wait')
base = f'http://127.0.0.1:{args.port}'
html = urllib.request.urlopen(base, timeout=5).read().decode()
token = re.search(r'<meta name="ricky-token" content="([^"]+)"', html).group(1)


def wait_ready():
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        status = json.loads(urllib.request.urlopen(base + '/api/status', timeout=5).read())
        if status.get('input_ready') and status.get('alive'):
            return
        time.sleep(.1)
    raise RuntimeError('Native preview did not become input-ready')


data = None
if args.tap:
    x, y = args.tap
    data = dict(type='touch', x1=x, y1=y, x2=x, y2=y, duration=args.hold)
elif args.swipe:
    data = dict(type='touch', **dict(zip(('x1', 'y1', 'x2', 'y2'), args.swipe)), duration=args.hold)
elif args.key:
    data = dict(type='key', key=args.key, duration=args.hold)
if data or args.restart:
    if data and not args.restart:
        wait_ready()
    req = urllib.request.Request(base + ('/api/restart' if args.restart else '/api/input'), json.dumps(data or {}).encode(),
                                 headers={'Content-Type': 'application/json', 'X-Ricky-Token': token})
    urllib.request.urlopen(req, timeout=5).read()
    if args.restart:
        wait_ready()
time.sleep(args.wait)
print(urllib.request.urlopen(base + '/api/status', timeout=5).read().decode())
if args.capture:
    args.capture.parent.mkdir(parents=True, exist_ok=True)
    args.capture.write_bytes(urllib.request.urlopen(base + '/api/screenshot', timeout=5).read())
    print(args.capture.resolve())
