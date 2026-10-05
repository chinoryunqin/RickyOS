import { ESPLoader, Transport } from 'esptool-js';
import { md5 } from 'hash-wasm';
import { checkSecurity, requireThat, hex } from './policy.js';

// Consume and verify the final raw READ_FLASH digest. esptool-js 0.7.0
// readFlash returns the data before this packet. Leaving it unread can desync
// the next command. Spec: Espressif serial-protocol.rst / Reading Flash.
export async function verifiedRead(loader, transport, offset, size, progress) {
  const bytes = await loader.readFlash(offset, size, (_packet, done) => progress?.(done, size));
  requireThat(bytes instanceof Uint8Array && bytes.length === size, '串口读取长度错误。');
  const digest = await transport.read(100000);
  requireThat(digest instanceof Uint8Array && digest.length === 16 &&
    hex(digest) === await md5(bytes), '串口读取摘要错误，备份未完成。');
  return bytes;
}
export class SerialAdapter {
  constructor(log) { this.log = log; }
  async connect(port) {
    this.transport = new Transport(port, false);
    this.loader = new ESPLoader({ transport: this.transport, baudrate: 115200, romBaudrate: 115200,
      terminal: { clean() {}, writeLine: line => this.log(this.redact(line)), write: line => this.log(this.redact(line)) } });
    await this.loader.detectChip('default_reset');
    const info = await this.loader.getSecurityInfo();
    requireThat(this.loader.chip.CHIP_NAME === 'ESP32-S3', '当前芯片不是 ESP32-S3。');
    // Inspect security before running any RAM stub; unknown status fails closed.
    checkSecurity(this.loader.chip.CHIP_NAME, '16MB', info, this.loader.secureDownloadMode);
    await this.loader.chip.postConnect?.(this.loader);
    await this.loader.runStub();
    requireThat(this.loader.IS_STUB === true, '备份需要受支持的读取程序，连接已停止。');
    const size = await this.loader.detectFlashSize();
    checkSecurity(this.loader.chip.CHIP_NAME, size, info, this.loader.secureDownloadMode);
  }
  redact(line) { return String(line).replace(/(?:[0-9a-f]{2}:){5}[0-9a-f]{2}/gi, '[设备标识已隐藏]'); }
  read(offset, size, progress) { return verifiedRead(this.loader, this.transport, offset, size, progress); }
  digest(offset, size) { return this.loader.flashMd5sum(offset, size); }
  write(bytes, offset, progress) {
    // The session supplies strictly checked ranges (app update or first-install
    // components). Never erase all, alter image headers, or expose addresses to UI.
    return this.loader.writeFlash({ fileArray: [{ data: bytes, address: offset }],
      flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep', eraseAll: false,
      compress: true, reportProgress: (_index, done, total) => progress(done, total) });
  }
  reset() { return this.loader.after('hard_reset'); }
  async close() { if (this.transport) await this.transport.disconnect(); }
}
