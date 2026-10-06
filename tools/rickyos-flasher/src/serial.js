import { ESPLoader, Transport } from 'esptool-js';
import { md5 } from 'hash-wasm';
import { checkSecurity, requireThat, hex } from './policy.js';

// esptool-js readFlash asks the stub for 4 KiB packets with up to 1024 of them
// unacknowledged. On macOS the serial input queue holds about 1 KiB; Chrome
// drains it more slowly than esptool, so a burst overflows it and bytes are
// silently dropped (traced: a 1026-byte packet arriving as 864 or 512 bytes,
// then a stall). Keep every packet below that queue even if SLIP doubles it:
// 448 data bytes, one unacknowledged, acknowledged as soon as it lands.
const READ_PACKET = 448, READ_UNACKED = 1;
export async function pacedReadFlash(loader, transport, offset, size, progress) {
  let pkt = loader._appendArray(loader._intToByteArray(offset), loader._intToByteArray(size));
  pkt = loader._appendArray(pkt, loader._intToByteArray(READ_PACKET));
  pkt = loader._appendArray(pkt, loader._intToByteArray(READ_UNACKED));
  requireThat(await loader.checkCommand('read flash', loader.ESP_READ_FLASH, pkt) == 0, '串口读取命令失败。');
  const bytes = new Uint8Array(size);
  let done = 0;
  while (done < size) {
    const packet = await transport.read(loader.FLASH_READ_TIMEOUT);
    requireThat(packet instanceof Uint8Array && packet.length > 0 && done + packet.length <= size,
      '串口读取数据错误。');
    bytes.set(packet, done); done += packet.length;
    await transport.write(loader._intToByteArray(done));
    progress?.(done, size);
  }
  return bytes;
}
// Consume and verify the final raw READ_FLASH digest, which follows the data.
// Leaving it unread can desync the next command. Spec: Espressif
// serial-protocol.rst / Reading Flash.
export async function verifiedRead(loader, transport, offset, size, progress) {
  const bytes = typeof loader.checkCommand === 'function' ?
    await pacedReadFlash(loader, transport, offset, size, progress) :
    await loader.readFlash(offset, size, (_packet, done) => progress?.(done, size));
  requireThat(bytes instanceof Uint8Array && bytes.length === size, '串口读取长度错误。');
  const digest = await transport.read(loader.FLASH_READ_TIMEOUT);
  requireThat(digest instanceof Uint8Array && digest.length === 16 &&
    hex(digest) === await md5(bytes), '串口读取摘要错误，备份未完成。');
  return bytes;
}
// The Read Pico's USB-Serial/JTAG stream can stall part-way through a large
// stub read; esptool shows the same on this board with 1 MB reads. Read in
// small pieces and, after a stall, reset into the stub again and re-read only
// that piece. The session still checks the whole flash digest afterwards.
const READ_PIECE = 16 * 1024, READ_ATTEMPTS = 5;
export class SerialAdapter {
  constructor(log, trace = false) { this.log = log; this.trace = trace; }
  async connect(port) {
    this.port = port;
    this.transport = new Transport(port, this.trace);
    // A larger browser-side buffer than the 255-byte Web Serial default.
    this.loader = new ESPLoader({ transport: this.transport, baudrate: 115200, romBaudrate: 115200,
      serialOptions: { bufferSize: 1 << 16 },
      terminal: { clean() {}, writeLine: line => this.log(this.redact(line)), write: line => this.log(this.redact(line)) } });
    await this.loader.detectChip('default_reset');
    const info = await this.loader.getSecurityInfo();
    requireThat(this.loader.chip.CHIP_NAME === 'ESP32-S3', '当前芯片不是 ESP32-S3。');
    // Inspect security before running any RAM stub; unknown status fails closed.
    checkSecurity(this.loader.chip.CHIP_NAME, '16MB', info, this.loader.secureDownloadMode);
    await this.loader.chip.postConnect?.(this.loader);
    await this.loader.runStub();
    requireThat(this.loader.IS_STUB === true, '备份需要受支持的读取程序，连接已停止。');
    // A 16 KiB piece takes well under a second; give up on a stalled one quickly.
    this.loader.FLASH_READ_TIMEOUT = 5000;
    this.log('读取方式：448 字节分包逐包确认。');
    const size = await this.loader.detectFlashSize();
    checkSecurity(this.loader.chip.CHIP_NAME, size, info, this.loader.secureDownloadMode);
  }
  redact(line) { return String(line).replace(/(?:[0-9a-f]{2}:){5}[0-9a-f]{2}/gi, '[设备标识已隐藏]'); }
  async read(offset, size, progress) {
    const bytes = new Uint8Array(size);
    for (let at = 0; at < size; at += READ_PIECE) {
      const length = Math.min(READ_PIECE, size - at);
      for (let attempt = 1; ; attempt++) {
        try {
          bytes.set(await verifiedRead(this.loader, this.transport, offset + at, length,
            done => progress?.(at + done, size)), at);
          break;
        } catch (error) {
          if (attempt >= READ_ATTEMPTS) throw error;
          this.log(`读取中断（${error.message}），重新连接后重试第 ${attempt} 次。`);
          await this.reconnect();
        }
      }
    }
    return bytes;
  }
  async reconnect() {
    await this.transport.disconnect().catch(() => {});
    await this.connect(this.port);
  }
  digest(offset, size) { return this.loader.flashMd5sum(offset, size); }
  write(bytes, offset, progress) {
    // The session supplies strictly checked ranges (app update or first-install
    // components). Never erase all, alter image headers, or expose addresses to UI.
    return this.loader.writeFlash({ fileArray: [{ data: bytes, address: offset }],
      flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep', eraseAll: false,
      compress: true, reportProgress: (_index, done, total) => progress(done, total) });
  }
  // esptool-js 0.7.0's HardReset only releases RTS. After flashing RTS is already
  // released, so nothing happened and the device stayed in the loader looking
  // frozen. Do what esptool.py does: hold EN low through RTS, then release it,
  // with DTR released so IO0 stays high and the app boots.
  async reset() {
    const t = this.transport;
    await t.setDTR(false);
    await t.setRTS(true);
    await new Promise(resolve => setTimeout(resolve, 120));
    await t.setRTS(false);
    await new Promise(resolve => setTimeout(resolve, 60));
  }
  async close() { if (this.transport) await this.transport.disconnect(); }
}
