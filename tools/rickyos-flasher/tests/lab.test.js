import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { md5, sha256 } from 'hash-wasm';
import { FLASH_BYTES, SLOT_BYTES, otaCrc, checkRelease } from '../src/policy.js';
import { LabFlashSession, checkLabManifest, checkLabFirmware } from '../src/lab-session.js';

const identity = '12'.repeat(32);

test('lab page exposes every controller ID and labels the installed test version', async () => {
  const html = await readFile(new URL('../lab-eego.html', import.meta.url), 'utf8');
  const app = await readFile(new URL('../src/lab-app.js', import.meta.url), 'utf8');
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
  assert.equal(ids.length, new Set(ids).size);
  for (const match of app.matchAll(/\$\('([^']+)'\)/g)) assert.ok(ids.includes(match[1]), match[1]);
  assert.match(app, /manifest\.version/);
  assert.match(html, /尚待实机验证/);
});
function image(board = 'eego_a4') {
  const bytes = new Uint8Array(4096), view = new DataView(bytes.buffer);
  bytes[0] = 0xe9; bytes[1] = 1; view.setUint16(12, 9, true);
  view.setUint32(32, 0xabcd5432, true);
  bytes.set(Buffer.from(identity, 'hex'), 176);
  bytes.set(new TextEncoder().encode(`RickyOS 1.2.2 CROSSPOINT-BOARD-V1:${board};`), 300);
  return bytes;
}
async function offer(bytes = image()) {
  return { schema: 1, product: 'RickyOS', channel: 'test', board: 'eego_a4',
    approved: false, hardwareAccepted: false, chipId: 9, flashBytes: FLASH_BYTES,
    version: '1.2.2', build: 'a4-ui-fixes-20261019', file: 'firmware/A4-test.bin',
    bytes: bytes.length, sha256: await sha256(bytes), compatibleAppElfSha256: [identity] };
}
async function flash(seq) {
  const bytes = new Uint8Array(FLASH_BYTES).fill(255), view = new DataView(bytes.buffer);
  const entries = [[1, 2, 0x9000, 0x5000, 'nvs'], [1, 0, 0xe000, 0x2000, 'otadata'],
    [0, 16, 0x10000, SLOT_BYTES, 'app0'], [0, 17, 0x650000, SLOT_BYTES, 'app1'],
    [1, 130, 0xc90000, 0x360000, 'spiffs'], [1, 3, 0xff0000, 0x10000, 'coredump']];
  entries.forEach(([type, subtype, offset, size, name], i) => {
    const at = 0x8000 + i * 32; bytes.fill(0, at, at + 32);
    view.setUint16(at, 0x50aa, true); bytes[at + 2] = type; bytes[at + 3] = subtype;
    view.setUint32(at + 4, offset, true); view.setUint32(at + 8, size, true);
    bytes.set(new TextEncoder().encode(name), at + 12);
  });
  bytes[0x80c0] = bytes[0x80c1] = 0xeb;
  bytes.set(Buffer.from(await md5(bytes.subarray(0x8000, 0x80c0)), 'hex'), 0x80d0);
  view.setUint32(0xe000, seq, true); view.setUint32(0xe018, 2, true);
  view.setUint32(0xe01c, otaCrc(bytes.subarray(0xe000, 0xe004)), true);
  bytes.set(image(), seq % 2 ? 0x10000 : 0x650000);
  return bytes;
}
class Adapter {
  constructor(bytes) { this.bytes = bytes; this.writes = []; this.resets = 0; this.badDigest = false; }
  async read(at, size) { return this.bytes.slice(at, at + size); }
  async digest(at, size) { return this.badDigest ? '0'.repeat(32) : md5(this.bytes.subarray(at, at + size)); }
  async write(bytes, at) { this.writes.push([at, bytes.length]); this.bytes.set(bytes, at); }
  async reset() { this.resets++; }
}
async function prepared(seq = 1) {
  const manifest = await offer(), adapter = new Adapter(await flash(seq));
  const session = new LabFlashSession(adapter, manifest);
  await session.inspectDevice(); return { manifest, adapter, session };
}

test('lab manifest explicitly stays unapproved and cannot enter production release policy', async () => {
  const manifest = await offer(); checkLabManifest(manifest);
  checkLabManifest({ ...manifest, version: '1.2.3-test.2' });
  assert.throws(() => checkRelease(manifest));
  for (const patch of [{ channel: 'stable' }, { board: 'readpico' }, { approved: true }, { hardwareAccepted: true },
    { version: '1.2.3-dev' }, { chipId: 32 }, { bytes: SLOT_BYTES }, { file: '../firmware.bin' }, { compatibleAppElfSha256: [] }])
    assert.throws(() => checkLabManifest({ ...manifest, ...patch }));
});
test('wrong-board and damaged test files fail before writes', async () => {
  const bytes = image(), manifest = await offer(bytes);
  await checkLabFirmware(bytes, manifest);
  const wrong = image('readpico');
  await assert.rejects(() => checkLabFirmware(wrong, manifest));
  await assert.rejects(async () => checkLabFirmware(wrong, await offer(wrong)));
  bytes[1000] ^= 1;
  await assert.rejects(() => checkLabFirmware(bytes, manifest));
});
test('both active slots update just the application and reset only after verification', async () => {
  for (const seq of [1, 2]) {
    const { adapter, session, manifest } = await prepared(seq);
    const table = adapter.bytes.slice(0x8000, 0x9000), ota = adapter.bytes.slice(0xe000, 0x10000);
    await session.install(image(), manifest, () => {}, true);
    assert.deepEqual(adapter.writes, [[seq % 2 ? 0x10000 : 0x650000, 4096]]);
    assert.deepEqual(adapter.bytes.slice(0x8000, 0x9000), table);
    assert.deepEqual(adapter.bytes.slice(0xe000, 0x10000), ota);
    assert.equal(adapter.resets, 1);
    await assert.rejects(() => session.install(image(), manifest, () => {}, true));
    assert.equal(adapter.writes.length, 1);
  }
});
test('confirmation, known A4 identity and live boot information are mandatory', async () => {
  for (const mode of ['confirmation', 'identity', 'table', 'ota', 'disconnect', 'offer']) {
    const { adapter, session, manifest } = await prepared();
    if (mode === 'identity') adapter.bytes[0x10000 + 176] ^= 1;
    if (mode === 'table') adapter.bytes[0x8004] ^= 1;
    if (mode === 'ota') adapter.bytes[0xe000] ^= 1;
    if (mode === 'disconnect') session.invalidate();
    if (mode === 'offer') manifest.build = 'other-build';
    await assert.rejects(() => session.install(image(), manifest, () => {}, mode !== 'confirmation'));
    assert.equal(adapter.writes.length, 0); assert.equal(adapter.resets, 0);
  }
  const manifest = await offer(), unknown = new Adapter(new Uint8Array(FLASH_BYTES));
  const session = new LabFlashSession(unknown, manifest);
  await assert.rejects(() => session.inspectDevice());
  assert.equal(session.ready, false); assert.equal(unknown.writes.length, 0);
});
test('write verification failure cannot reboot or retry the session', async () => {
  const { adapter, session, manifest } = await prepared();
  adapter.badDigest = true;
  await assert.rejects(() => session.install(image(), manifest, () => {}, true));
  assert.equal(adapter.resets, 0); assert.equal(session.writeStarted, true);
  await assert.rejects(() => session.install(image(), manifest, () => {}, true));
  assert.equal(adapter.writes.length, 1);
});
test('A4 full backup is downloadable and rechecked before installation', async () => {
  const { adapter, session, manifest } = await prepared();
  const backup = await session.backupFlash(() => {});
  assert.equal(backup.bytes.length, FLASH_BYTES); assert.equal(backup.compatible, true);
  assert.equal(session.ready, true); assert.equal(session.plan.kind, 'upgrade');
  adapter.bytes[0x9000] ^= 1;
  await assert.rejects(() => session.install(image(), manifest, () => {}, true));
  assert.equal(adapter.writes.length, 0);
});
