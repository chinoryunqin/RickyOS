import test from 'node:test';
import assert from 'node:assert/strict';
import { md5, sha256 } from 'hash-wasm';
import { FLASH_BYTES, SLOT_BYTES, TAG, otaCrc, activeOffset, inspectBackup, checkFactoryPartitions,
  checkRelease, checkPlanRelease, checkInstallAssets } from '../src/policy.js';
import { FlashSession } from '../src/session.js';

const factoryEntries = [[1, 2, 0x9000, 0x5000, 'nvs'], [1, 1, 0xe000, 0x1000, 'phy_init'],
  [0, 0, 0x10000, 0x400000, 'factory'], [1, 129, 0x410000, 0x500000, 'storage']];
const targetEntries = [[1, 2, 0x9000, 0x5000, 'nvs'], [1, 0, 0xe000, 0x2000, 'otadata'],
  [0, 16, 0x10000, SLOT_BYTES, 'app0'], [0, 17, 0x650000, SLOT_BYTES, 'app1'],
  [1, 130, 0xc90000, 0x360000, 'spiffs'], [1, 3, 0xff0000, 0x10000, 'coredump']];
async function table(entries) {
  const bytes = new Uint8Array(4096).fill(255), view = new DataView(bytes.buffer);
  entries.forEach(([type, subtype, offset, size, name], i) => {
    const at = i * 32; bytes.fill(0, at, at + 32); view.setUint16(at, 0x50aa, true);
    bytes[at + 2] = type; bytes[at + 3] = subtype;
    view.setUint32(at + 4, offset, true); view.setUint32(at + 8, size, true);
    bytes.set(new TextEncoder().encode(name), at + 12);
  });
  const end = entries.length * 32; bytes[end] = bytes[end + 1] = 0xeb;
  bytes.set(Buffer.from(await md5(bytes.subarray(0, end)), 'hex'), end + 16); return bytes;
}
function espHeader(bytes) { bytes[0] = 0xe9; bytes[1] = 1; bytes[12] = 9; return bytes; }
async function original() {
  const bytes = new Uint8Array(FLASH_BYTES).fill(0x36);
  bytes.set(await table(factoryEntries), 0x8000);
  const app = espHeader(new Uint8Array(4096)), view = new DataView(app.buffer);
  view.setUint32(28, 256, true); view.setUint32(32, 0xabcd5432, true);
  app.set(new TextEncoder().encode('Read_Pico'), 80); bytes.set(app, 0x10000);
  return bytes;
}
class Device {
  constructor(bytes) { this.bytes = bytes; this.writes = []; this.resets = 0; }
  async read(at, size, progress) { progress?.(size, size); return this.bytes.slice(at, at + size); }
  digest(at, size) { return md5(this.bytes.subarray(at, at + size)); }
  async write(bytes, at, progress) { this.writes.push([at, bytes.length]); this.bytes.set(bytes, at); progress?.(bytes.length, bytes.length); }
  async reset() { this.resets++; }
}
async function prepared() {
  const adapter = new Device(await original()), session = new FlashSession(adapter);
  const saved = await session.backupFlash(() => {});
  const firmware = espHeader(new Uint8Array(4100));
  firmware.set(new TextEncoder().encode(`${TAG} RickyOS 1.1.1`), 24);
  const boot = espHeader(new Uint8Array(4100)), ota = new Uint8Array(8192).fill(255), view = new DataView(ota.buffer);
  view.setUint32(0, 1, true); view.setUint32(28, otaCrc(ota.subarray(0, 4)), true);
  const assets = [boot, (await table(targetEntries)).slice(0, 3072), ota];
  const plan = session.plan;
  const release = { approved: true, hardwareAccepted: true, mode: 'auto-install', board: 'readpico',
    chipId: 9, flashBytes: FLASH_BYTES, version: '1.1.1',
    bytes: firmware.length, sha256: await sha256(firmware), file: 'firmware/RickyOS-13.bin',
    fullInstall: { approved: true, hardwareAccepted: true,
      factoryLayouts: [plan.layout],
      segments: await Promise.all(assets.map(async (bytes, i) => ({ role: ['bootloader', 'partitions', 'boot_app0'][i],
        offset: [0, 0x8000, 0xe000][i], bytes: bytes.length, sha256: await sha256(bytes),
        file: `firmware/component-${i}.bin` }))) } };
  return { adapter, session, firmware, release, assets };
}
async function preparedOther() {
  const item = await prepared();
  // A community firmware with its own layout: nothing at 0x8000 parses.
  const bytes = new Uint8Array(FLASH_BYTES).fill(0x5a);
  item.adapter = new Device(bytes); item.session = new FlashSession(item.adapter);
  const saved = await item.session.backupFlash(() => {});
  item.release.fullInstall.otherSystems = true;
  return item;
}
function install(item) {
  return item.session.install(item.firmware, item.release, () => {}, true, () => {}, item.assets);
}
test('factory is automatically identified by full layout and Read_Pico app descriptor', async () => {
  const bytes = await original(), plan = await inspectBackup(bytes);
  assert.equal(plan.kind, 'factory'); assert.equal(plan.layout, 'mindreset-factory-4m-v1');
  assert.equal(plan.tableSha256, await sha256(bytes.subarray(0x8000, 0x9000)));
  for (const at of [0x10000, 0x1000c, 0x10020, 0x10050]) {
    // Not the factory system any more: it is offered the complete install instead.
    const bad = bytes.slice(); bad[at] ^= 1; assert.equal((await inspectBackup(bad)).kind, 'other');
  }
});
test('factory partition checks reject changed geometry, flags, extra entries and digest', async () => {
  const good = await table(factoryEntries); await checkFactoryPartitions(good);
  for (const at of [4, 12, 28, 68, 72, 128, 144, 180]) {
    const bad = good.slice(); bad[at] ^= 1; await assert.rejects(() => checkFactoryPartitions(bad));
  }
  const old = factoryEntries.map(item => item.slice()); old[2][3] = 0x200000; old[3][2] = 0x210000;
  await assert.rejects(async () => checkFactoryPartitions(await table(old)));
});
test('first install requires accepted components and an accepted factory layout', async () => {
  const item = await prepared(); checkRelease(item.release); checkPlanRelease(item.session.plan, item.release);
  // Any factory image on the accepted layout qualifies: builds of the open-source
  // factory firmware differ per device, so its digest is not part of the gate.
  const rebuilt = { ...item.session.plan, appSha256: '0'.repeat(64), bootloaderSha256: '1'.repeat(64) };
  checkPlanRelease(rebuilt, item.release);
  assert.throws(() => checkPlanRelease({ ...item.session.plan, layout: 'mindreset-factory-2m' }, item.release));
  for (const patch of [{ approved: false }, { hardwareAccepted: false }, { segments: [] }, { factoryLayouts: [] },
    { factoryLayouts: ['mindreset-factory-2m'] }, { factoryLayouts: undefined }]) {
    assert.throws(() => checkRelease({ ...item.release, fullInstall: { ...item.release.fullInstall, ...patch } }));
  }
  for (const patch of [{ offset: 0x9000 }, { bytes: 0x9000 }, { role: 'firmware' }, { file: '../boot.bin' }]) {
    const bad = structuredClone(item.release); Object.assign(bad.fullInstall.segments[0], patch);
    assert.throws(() => checkRelease(bad));
  }
  const bad = structuredClone(item.release); bad.fullInstall.segments[0].file = bad.file;
  assert.throws(() => checkRelease(bad));
  assert.equal(item.adapter.writes.length, 0);
});
test('missing/corrupt/wrong-chip components and wrong OTA are rejected before writing', async () => {
  for (const mode of ['missing', 'hash', 'chip', 'ota', 'table']) {
    const item = await prepared();
    if (mode === 'missing') item.assets.pop();
    else if (mode === 'hash') item.assets[0][200] ^= 1;
    else {
      const i = mode === 'chip' ? 0 : mode === 'table' ? 1 : 2;
      if (mode === 'chip') item.assets[i][12] = 5;
      if (mode === 'table') item.assets[i][28] = 1;
      if (mode === 'ota') {
        const view = new DataView(item.assets[i].buffer); view.setUint32(0, 2, true);
        view.setUint32(28, otaCrc(item.assets[i].subarray(0, 4)), true);
      }
      item.release.fullInstall.segments[i].sha256 = await sha256(item.assets[i]);
    }
    await assert.rejects(() => install(item)); assert.equal(item.adapter.writes.length, 0);
  }
});
test('migration preserves NVS, clears stale internal data and commits partitions last', async () => {
  const item = await prepared(), before = item.adapter.bytes.slice(), progress = [];
  await checkInstallAssets(item.assets, item.release);
  await item.session.install(item.firmware, item.release, (done, total) => progress.push([done, total]), true, () => {}, item.assets);
  assert.deepEqual(item.adapter.writes.at(-1), [0x8000, 4096]); assert.equal(item.adapter.resets, 1);
  assert.equal(item.adapter.writes[0][0], 0x10000);
  assert.ok(item.adapter.writes.every(([at, size]) => at % 4096 === 0 && size % 4096 === 0 && at + size <= FLASH_BYTES &&
    (at + size <= 0x9000 || at >= 0xe000)));
  assert.deepEqual(item.adapter.bytes.subarray(0x9000, 0xe000), before.subarray(0x9000, 0xe000));
  assert.deepEqual(item.adapter.bytes.subarray(0x10000, 0x10000 + item.firmware.length), item.firmware);
  assert.ok(item.adapter.bytes.subarray(0x10000 + item.firmware.length).every(value => value === 255));
  assert.equal((await inspectBackup(item.adapter.bytes)).kind, 'upgrade');
  assert.equal(progress.at(-1)[0], progress.at(-1)[1]);
  await assert.rejects(() => install(item));
});
test('unaccepted layout, backup changes and absent confirmation cause zero writes', async () => {
  for (const mode of ['layout', 'live', 'confirmation']) {
    const item = await prepared();
    if (mode === 'layout') item.release.fullInstall.factoryLayouts = ['mindreset-factory-2m'];
    if (mode === 'live') item.adapter.bytes[0x410000] ^= 1;
    await assert.rejects(() => item.session.install(item.firmware, item.release, () => {}, mode !== 'confirmation', () => {}, item.assets));
    assert.equal(item.adapter.writes.length, 0); assert.equal(item.adapter.resets, 0);
  }
});
test('migration staging failure or disconnect never commits partitions or resets', async () => {
  for (const mode of ['stream', 'digest', 'disconnect', 'nvs']) {
    const item = await prepared(), originalWrite = item.adapter.write.bind(item.adapter);
    item.adapter.write = async (bytes, at, progress) => {
      if (at >= 0x650000 && mode === 'stream') throw new Error('serial stopped');
      await originalWrite(bytes, at, progress);
      if (at >= 0x650000 && mode === 'digest') item.adapter.bytes[at] ^= 1;
      if (at >= 0x650000 && mode === 'disconnect') item.session.invalidate();
      if (at === 0 && mode === 'nvs') item.adapter.bytes[0x9000] ^= 1;
    };
    await assert.rejects(() => install(item));
    assert.ok(!item.adapter.writes.some(([at]) => at === 0x8000)); assert.equal(item.adapter.resets, 0);
    assert.equal(item.session.writeStarted, true); assert.equal(item.session.ready, false);
  }
});
test('partition commit failure and final flash corruption never trigger reset', async () => {
  for (const mode of ['commit', 'final']) {
    const item = await prepared(), originalWrite = item.adapter.write.bind(item.adapter);
    item.adapter.write = async (bytes, at, progress) => {
      if (at === 0x8000 && mode === 'commit') throw new Error('commit failed');
      await originalWrite(bytes, at, progress);
      if (at === 0x8000 && mode === 'final') item.adapter.bytes[0x9000] ^= 1;
    };
    await assert.rejects(() => install(item)); assert.equal(item.adapter.resets, 0);
  }
});
test('per-device NVS and internal files are not mistaken for factory firmware identity', async () => {
  const item = await prepared(), bytes = await original();
  bytes[0x9000] ^= 1; bytes[0x410000] ^= 1;
  const other = new FlashSession(new Device(bytes)), saved = await other.backupFlash(() => {});
 
  checkPlanRelease(other.plan, item.release);
  assert.equal(other.plan.bootloaderSha256, item.session.plan.bootloaderSha256);
  assert.equal(other.plan.appSha256, item.session.plan.appSha256);
});
test('the same unified release updates installed devices without touching boot or partitions', async () => {
  const item = await prepared(); await install(item);
  const bytes = item.adapter.bytes.slice(), adapter = new Device(bytes), session = new FlashSession(adapter);
  const backup = await session.backupFlash(() => {});
  checkPlanRelease(session.plan, item.release);
  await session.install(item.firmware, item.release, () => {}, true);
  assert.deepEqual(adapter.writes, [[0x10000, item.firmware.length]]);
  assert.equal(adapter.resets, 1);
});
test('another system gets the complete install, starting with empty NVS', async () => {
  const item = await preparedOther();
  assert.equal(item.session.plan.kind, 'other');
  await install(item);
  assert.deepEqual(item.adapter.writes.at(-1), [0x8000, 4096]); assert.equal(item.adapter.resets, 1);
  assert.ok(item.adapter.bytes.subarray(0x9000, 0xe000).every(value => value === 255));
  assert.deepEqual(item.adapter.bytes.subarray(0x10000, 0x10000 + item.firmware.length), item.firmware);
  assert.ok(item.adapter.bytes.subarray(0x10000 + item.firmware.length).every(value => value === 255));
  assert.equal((await inspectBackup(item.adapter.bytes)).kind, 'upgrade');
});
test('another system is refused without the release opting in, before any write', async () => {
  for (const otherSystems of [undefined, false]) {
    const item = await preparedOther(); item.release.fullInstall.otherSystems = otherSystems;
    await assert.rejects(() => install(item)); assert.equal(item.adapter.writes.length, 0);
  }
});
test('boot records are read like the ESP-IDF bootloader: invalid records do not vote', async () => {
  // Arduino's boot_app0.bin: record 0 picks app0, record 1 has sequence 0 and CRC 0xffffffff.
  const ota = new Uint8Array(8192).fill(255), view = new DataView(ota.buffer);
  view.setUint32(0, 1, true); view.setUint32(28, otaCrc(ota.subarray(0, 4)), true);
  view.setUint32(4096, 0, true);
  assert.equal(activeOffset(ota), 0x10000);
  const none = new Uint8Array(8192).fill(255); new DataView(none.buffer).setUint32(4096, 0, true);
  assert.throws(() => activeOffset(none));
});
test('factory and other systems install without a backup, checking each write', async () => {
  for (const make of [prepared, preparedOther]) {
    const item = await make(), bytes = item.adapter.bytes.slice();
    item.adapter = new Device(bytes); item.session = new FlashSession(item.adapter);
    const plan = await item.session.inspectDevice();
    assert.equal(plan.kind, make === prepared ? 'factory' : 'other');
    await install(item);
    assert.deepEqual(item.adapter.writes.at(-1), [0x8000, 4096]); assert.equal(item.adapter.resets, 1);
    assert.equal((await inspectBackup(item.adapter.bytes)).kind, 'upgrade');
  }
});
