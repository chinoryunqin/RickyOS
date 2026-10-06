import test from 'node:test';
import assert from 'node:assert/strict';
import { md5, sha256 } from 'hash-wasm';
import { FLASH_BYTES, SLOT_BYTES, TAG, otaCrc, activeOffset, checkSecurity,
  checkPartitions, checkRelease, checkFirmware, inspectBackup, checkImage } from '../src/policy.js';
import { FlashSession } from '../src/session.js';
import { verifiedRead, SerialAdapter } from '../src/serial.js';

function image(version = '1.1.1') {
  const bytes = new Uint8Array(4096); bytes[0] = 0xe9; bytes[1] = 1; bytes[12] = 9;
  bytes.set(new TextEncoder().encode(`${TAG} RickyOS ${version}`), 24);
  return bytes;
}
async function partitionTable() {
  const bytes = new Uint8Array(4096).fill(255), view = new DataView(bytes.buffer);
  const entries = [[1, 2, 0x9000, 0x5000, 'nvs'], [1, 0, 0xe000, 0x2000, 'otadata'],
    [0, 16, 0x10000, SLOT_BYTES, 'app0'], [0, 17, 0x650000, SLOT_BYTES, 'app1'],
    [1, 130, 0xc90000, 0x360000, 'spiffs'], [1, 3, 0xff0000, 0x10000, 'coredump']];
  entries.forEach(([type, subtype, offset, size, name], i) => {
    const at = i * 32; bytes.fill(0, at, at + 32);
    view.setUint16(at, 0x50aa, true); bytes[at + 2] = type; bytes[at + 3] = subtype;
    view.setUint32(at + 4, offset, true); view.setUint32(at + 8, size, true);
    bytes.set(new TextEncoder().encode(name), at + 12);
  });
  bytes[192] = bytes[193] = 0xeb;
  bytes.set(Buffer.from(await md5(bytes.subarray(0, 192)), 'hex'), 208);
  return bytes;
}
function otaRecord(seq = 1, state = 2, second = null) {
  const bytes = new Uint8Array(8192).fill(255), view = new DataView(bytes.buffer);
  for (const [at, value] of [[0, seq], [4096, second]]) if (value !== null) {
    view.setUint32(at, value, true); view.setUint32(at + 24, state, true);
    view.setUint32(at + 28, otaCrc(bytes.subarray(at, at + 4)), true);
  }
  return bytes;
}
async function fixture(seq = 1) {
  const bytes = new Uint8Array(FLASH_BYTES).fill(255);
  bytes.set(await partitionTable(), 0x8000); bytes.set(otaRecord(seq), 0xe000);
  bytes.set(image(), seq % 2 ? 0x10000 : 0x650000);
  return bytes;
}
async function release(bytes = image()) {
  return { approved: true, hardwareAccepted: true, mode: 'app-upgrade', board: 'readpico',
    chipId: 9, flashBytes: FLASH_BYTES, version: '1.1.1',
    bytes: bytes.length, sha256: await sha256(bytes), file: 'firmware/RickyOS-13.bin' };
}
class FakeAdapter {
  constructor(bytes) { this.bytes = bytes; this.writes = []; this.resets = 0; this.reads = []; }
  async read(at, size, progress) { this.reads.push([at, size]); progress?.(size, size); return this.bytes.slice(at, at + size); }
  digest(at, size) { return md5(this.bytes.subarray(at, at + size)); }
  async write(bytes, at) { this.writes.push([at, bytes.length]); this.bytes.set(bytes, at); }
  async reset() { this.resets++; }
}
async function prepared(seq = 1) {
  const adapter = new FakeAdapter(await fixture(seq)), session = new FlashSession(adapter);
  const backup = await session.backupFlash(() => {});
  return { adapter, session };
}
test('security fails closed for unknown, encrypted, secure-boot and wrong chip/capacity', () => {
  const info = { chipId: 9, flags: 0, flashCryptCnt: 0, parsedFlags: { SECURE_BOOT_EN: false, SECURE_DOWNLOAD_ENABLE: false } };
  checkSecurity('ESP32-S3', '16MB', info, false);
  for (const item of [null, { ...info, flashCryptCnt: 1 }, { ...info, chipId: 5 },
    { ...info, flags: null }, { ...info, parsedFlags: { SECURE_BOOT_EN: true } }]) {
    assert.throws(() => checkSecurity('ESP32-S3', '16MB', item, false));
  }
  assert.throws(() => checkSecurity('ESP32-S3', '8MB', info, false));
  assert.throws(() => checkSecurity('ESP32-C3', '16MB', info, false));
  assert.throws(() => checkSecurity('ESP32-S3', '16MB', info, true));
});
test('partition MD5, boundaries, flags and trailer enforced', async () => {
  await checkPartitions(await partitionTable());
  for (const at of [0, 4, 8, 12, 28, 192, 208, 240]) {
    const table = await partitionTable(); table[at] ^= 1;
    await assert.rejects(() => checkPartitions(table));
  }
  await assert.rejects(() => checkPartitions(new Uint8Array(4095)));
});
test('OTA CRC matches ESP-IDF / Python fixed vectors', () => {
  assert.equal(otaCrc(Uint8Array.of(1, 0, 0, 0)), 0x4743989a);
  assert.equal(activeOffset(otaRecord(1)), 0x10000);
  assert.equal(activeOffset(otaRecord(2)), 0x650000);
  assert.equal(activeOffset(otaRecord(1, 2, 2)), 0x650000);
  for (const state of [0, 1, 3, 4]) assert.throws(() => activeOffset(otaRecord(1, state)));
  for (const seq of [0, 0xffffffff]) assert.throws(() => activeOffset(otaRecord(seq)));
  const bad = otaRecord(); bad[28] ^= 1; assert.throws(() => activeOffset(bad));
  assert.throws(() => activeOffset(new Uint8Array(8192).fill(255)));
});
test('board tags cannot be bypassed by matching tag alongside another board', () => {
  checkImage(image());
  const bad = image(); bad.set(new TextEncoder().encode('CROSSPOINT-BOARD-V1:sticky;'), 500);
  assert.throws(() => checkImage(bad));
  const wrong = image(); wrong[12] = 5; assert.throws(() => checkImage(wrong));
  assert.throws(() => checkImage(new Uint8Array(4096)));
});
test('only approved hardware-accepted non-dev releases with 512 KiB reserve', async () => {
  const good = await release(); checkRelease(good);
  for (const patch of [{ approved: false }, { hardwareAccepted: false }, { board: 'sticky' },
    { chipId: 5 }, { mode: 'full-install' }, { bytes: SLOT_BYTES }, { bytes: 4095 },
    { version: '1.1.0-dev' }, { sha256: 'wrong' },
    { file: '../firmware.bin' }, { file: 'https://example.com/fw.bin' }]) {
    assert.throws(() => checkRelease({ ...good, ...patch }));
  }
});
test('firmware size, digest, brand and version checked', async () => {
  const good = await release(); await checkFirmware(image(), good);
  const bad = image(); bad[300] ^= 1; await assert.rejects(() => checkFirmware(bad, good));
  const wrong = image('1.1.2');
  await assert.rejects(async () => checkFirmware(wrong, { ...good, sha256: await sha256(wrong) }));
});
test('full backup selects correct active slot and rejects truncated backup', async () => {
  assert.equal((await inspectBackup(await fixture(2))).offset, 0x650000);
  await assert.rejects(() => inspectBackup(new Uint8Array(4096)));
});
test('backup failure and cancellation never permit write', async () => {
  const adapter = new FakeAdapter(await fixture()), session = new FlashSession(adapter);
  adapter.read = async () => { throw new Error('serial stream stopped'); };
  await assert.rejects(() => session.backupFlash(() => {}));
  await assert.rejects(() => session.install(image(), null, () => {}, true));
  assert.equal(adapter.writes.length, 0); assert.equal(session.backup, null);
  const cancelled = new FlashSession(new FakeAdapter(await fixture()));
  await assert.rejects(() => cancelled.backupFlash(() => {}, () => true));
  assert.equal(cancelled.backup, null);
});
test('backup transport chunks bounded to 64 KiB; whole-device MD5 mandatory', async () => {
  const { adapter, session } = await prepared();
  assert.equal(adapter.reads.length, 256);
  assert.ok(adapter.reads.every(([, size]) => size === 65536));
  adapter.digest = async () => '0'.repeat(32);
  await assert.rejects(() => session.backupFlash(() => {}));
  assert.equal(session.backup, null);
});
test('another system needs a full-install release; an app-only release writes nothing', async () => {
  const adapter = new FakeAdapter(new Uint8Array(FLASH_BYTES)), session = new FlashSession(adapter);
  const backup = await session.backupFlash(() => {});
  assert.equal(backup.compatible, true); assert.equal(session.plan.kind, 'other');
  await assert.rejects(async () => session.install(image(), await release(), () => {}, true));
  assert.equal(adapter.writes.length, 0);
});
test('explicit confirmation required; unpublished releases cannot erase/write', async () => {
  const { adapter, session } = await prepared(), published = await release();
  await assert.rejects(() => session.install(image(), published, () => {}, false));
  await assert.rejects(() => session.install(image(), { ...published, approved: false }, () => {}, true));
  assert.equal(adapter.writes.length, 0);
});
test('live partition, OTA or other flash changes block before writing', async () => {
  for (const at of [0x8004, 0xe000, 0x9000]) {
    const { adapter, session } = await prepared(); adapter.bytes[at] ^= 1;
    await assert.rejects(async () => session.install(image(), await release(), () => {}, true));
    assert.equal(adapter.writes.length, 0); assert.equal(adapter.resets, 0);
  }
});
test('success writes only active app and resets after all checks', async () => {
  for (const seq of [1, 2]) {
    const { adapter, session } = await prepared(seq);
    const original = adapter.bytes.slice();
    await session.install(image(), await release(), () => {}, true);
    const at = seq === 1 ? 0x10000 : 0x650000;
    assert.deepEqual(adapter.writes, [[at, 4096]]); assert.equal(adapter.resets, 1);
    assert.deepEqual(adapter.bytes.subarray(0, at), original.subarray(0, at));
    assert.deepEqual(adapter.bytes.subarray(at + 4096), original.subarray(at + 4096));
    await assert.rejects(async () => session.install(image(), await release(), () => {}, true));
  }
});
test('write interruption or bad digest prevents reset and re-use', async () => {
  for (const mode of ['write', 'verify', 'metadata']) {
    const { adapter, session } = await prepared();
    adapter.write = async (bytes, at) => {
      adapter.writes.push([at, bytes.length]);
      if (mode === 'write') throw new Error('stream stopped');
      adapter.bytes.set(bytes, at);
      if (mode === 'verify') adapter.bytes[at] ^= 1;
      if (mode === 'metadata') adapter.bytes[0xe000] ^= 1;
    };
    await assert.rejects(async () => session.install(image(), await release(), () => {}, true));
    assert.equal(adapter.resets, 0); assert.equal(session.writeStarted, true);
    assert.equal(session.ready, false);
  }
});
test('disconnect invalidates existing backup authorization', async () => {
  const { session, adapter } = await prepared(); session.invalidate();
  await assert.rejects(async () => session.install(image(), await release(), () => {}, true));
  assert.equal(adapter.writes.length, 0);
});
test('serial read consumes final digest and rejects truncation/noise', async () => {
  const bytes = image(), digest = Uint8Array.from(Buffer.from(await md5(bytes), 'hex'));
  let consumed = 0;
  const loader = { readFlash: async () => bytes };
  const transport = { read: async () => { consumed++; return digest; } };
  assert.equal(await verifiedRead(loader, transport, 0, bytes.length), bytes);
  assert.equal(consumed, 1);
  await assert.rejects(() => verifiedRead(loader, { read: async () => new Uint8Array(16) }, 0, bytes.length));
  await assert.rejects(() => verifiedRead(loader, { read: async () => new Uint8Array(15) }, 0, bytes.length));
  await assert.rejects(() => verifiedRead(loader, transport, 0, bytes.length + 1));
});
test('a stalled read piece reconnects and is re-read; persistent stalls still fail', async () => {
  const flash = new Uint8Array(65536).map((_, i) => i * 7);
  const adapter = new SerialAdapter(() => {}); let pending = null, stalls = 1, reconnects = 0;
  adapter.loader = { readFlash: async (at, size) => {
    if (stalls-- > 0) throw new Error('No serial data received.');
    pending = flash.slice(at, at + size); return pending;
  } };
  adapter.transport = { read: async () => Uint8Array.from(Buffer.from(await md5(pending), 'hex')) };
  adapter.reconnect = async () => { reconnects++; };
  assert.deepEqual(await adapter.read(0, flash.length), flash);
  assert.equal(reconnects, 1);
  stalls = Infinity;
  await assert.rejects(() => adapter.read(0, flash.length));
});
test('adapter cannot request erase-all or alter flash header parameters', async () => {
  const adapter = new SerialAdapter(() => {}); let options;
  adapter.loader = { writeFlash: async value => { options = value; } };
  await adapter.write(image(), 0x10000, () => {});
  assert.equal(options.eraseAll, false); assert.equal(options.fileArray.length, 1);
  assert.equal(options.flashSize, 'keep'); assert.equal(options.flashMode, 'keep'); assert.equal(options.flashFreq, 'keep');
});
test('paced read asks for 448-byte packets, one unacknowledged, and acknowledges each', async () => {
  const { pacedReadFlash } = await import('../src/serial.js');
  const flash = new Uint8Array(5000).map((_, i) => i), acks = []; let request;
  const int = n => Uint8Array.from([n & 255, (n >> 8) & 255, (n >> 16) & 255, (n >>> 24) & 255]);
  const loader = { FLASH_READ_TIMEOUT: 10, ESP_READ_FLASH: 0xd, _intToByteArray: int,
    _appendArray: (a, b) => Uint8Array.from([...a, ...b]), checkCommand: async (_n, _op, pkt) => { request = pkt; return 0; } };
  let at = 0;
  const transport = { read: async () => { const p = flash.slice(at, at + 448); at += p.length; return p; },
    write: async bytes => acks.push(new DataView(bytes.buffer).getUint32(0, true)) };
  assert.deepEqual(await pacedReadFlash(loader, transport, 0x2000, flash.length), flash);
  assert.deepEqual([...request], [...int(0x2000), ...int(5000), ...int(448), ...int(1)]);
  assert.deepEqual(acks, [448, 896, 1344, 1792, 2240, 2688, 3136, 3584, 4032, 4480, 4928, 5000]);
});
test('without a backup the device is classified from headers and installs the same way', async () => {
  const { adapter: from } = await prepared();
  const adapter = new FakeAdapter(from.bytes.slice()), session = new FlashSession(adapter);
  assert.equal((await session.inspectDevice()).kind, 'upgrade'); assert.equal(session.backup, null);
  await session.install(image(), await release(), () => {}, true);
  assert.equal(adapter.writes.length, 1); assert.equal(adapter.resets, 1);
});
test('reset holds EN low through RTS, then releases it with IO0 high', async () => {
  const calls = [];
  const adapter = new SerialAdapter(() => {});
  adapter.transport = { setDTR: async v => calls.push(['DTR', v]), setRTS: async v => calls.push(['RTS', v]) };
  await adapter.reset();
  assert.deepEqual(calls, [['DTR', false], ['RTS', true], ['RTS', false]]);
});
