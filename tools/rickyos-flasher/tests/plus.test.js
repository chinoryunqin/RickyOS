import test from 'node:test';
import assert from 'node:assert/strict';
import { md5, sha256 } from 'hash-wasm';
import { FLASH_BYTES, equalBytes, activeOffset, checkPartitions } from '../src/policy.js';
import { PLUS, inspectPlus, inspectPlusBackup, plusBootRecord, checkPlusRelease, checkPlusImage } from '../src/plus.js';
import { catalogReleases, releaseFiles } from '../src/catalog.js';
import { FlashSession } from '../src/session.js';
import { vendorTable, rickyTable, otaRecord, plusApp, plusFirmware, plusBootloader, factoryFlash, rickyFlash }
  from './plus-fixture.js';

class FakeAdapter {
  constructor(bytes) { this.bytes = bytes; this.writes = []; }
  async read(at, size, progress) { progress?.(size, size); return this.bytes.slice(at, at + size); }
  digest(at, size) { return md5(this.bytes.subarray(at, at + size)); }
  async write(bytes, at, progress) { this.writes.push([at, bytes.length]); this.bytes.set(bytes, at); progress?.(bytes.length, bytes.length); }
  async reset() {}
}
const tableFile = async () => (await rickyTable()).slice(0, 3072);
async function release(firmware = plusFirmware(), boot = plusBootloader()) {
  const table = await tableFile();
  return { version: '1.2.0', approved: true, hardwareAccepted: true, mode: 'plus-install', board: PLUS.board,
    chipId: PLUS.chipId, flashBytes: FLASH_BYTES, file: 'firmware/RickyOS-Plus-1.2.0.bin', bytes: firmware.length,
    sha256: await sha256(firmware), bootloader: { offset: PLUS.bootloaderOffset,
      file: 'firmware/RickyOS-Plus-1.2.0-bootloader.bin', bytes: boot.length, sha256: await sha256(boot) },
    partitions: { offset: PLUS.tableOffset, file: 'firmware/RickyOS-Plus-1.2.0-partitions.bin',
      bytes: table.length, sha256: await sha256(table) } };
}
const assetsFor = async boot => [boot, await tableFile()];
const slice = (flash, at, size) => flash.slice(at, at + size);
async function inspect(flash) {
  return inspectPlus(slice(flash, PLUS.tableOffset, 4096), slice(flash, PLUS.otaOffset, PLUS.otaBytes),
    async (at, size) => slice(flash, at, size));
}

test('fixtures match the tables read from the device and the Read Pico layout', async () => {
  assert.equal(await sha256(await vendorTable()), PLUS.vendorTableSha256);
  await checkPartitions(await rickyTable());
});

test('only RickyOS on the RickyOS layout is an app update; the vendor system is named; all else is other', async () => {
  assert.equal((await inspect(await factoryFlash())).kind, 'factory');
  assert.equal((await inspect(await rickyFlash(1))).kind, 'upgrade');
  const fromApp1 = await inspect(await rickyFlash(2));
  assert.equal(fromApp1.kind, 'upgrade'); assert.equal(fromApp1.offset, 0x650000);
  // RickyOS on the vendor layout (earlier installs), a foreign app, a foreign table, nothing at all.
  const oldLayout = await factoryFlash();
  oldLayout.set(plusFirmware(), PLUS.vendorSlots[0]);
  const foreignApp = await rickyFlash(1); foreignApp.set(plusApp('SomethingElse'), PLUS.appOffset);
  const foreignTable = await factoryFlash(); foreignTable[PLUS.tableOffset + 40] ^= 1;
  const empty = new Uint8Array(FLASH_BYTES).fill(255);
  for (const flash of [oldLayout, foreignApp, foreignTable, empty]) assert.equal((await inspect(flash)).kind, 'other');
  assert.equal((await inspectPlusBackup(await factoryFlash())).kind, 'factory');
});

test('the boot record selects app0', () => {
  assert.equal(activeOffset(plusBootRecord()), PLUS.appOffset);
  assert.ok(plusBootRecord().subarray(4096).every(b => b === 255));
});

for (const [name, make] of [['the vendor system', factoryFlash], ['another system', async () => {
  const flash = await factoryFlash(); flash.set(plusApp('SomeOtherFirmware'), PLUS.vendorSlots[0]);
  flash[PLUS.tableOffset + 40] ^= 1; return flash;
}]]) {
  test(`${name} gets the complete install onto the RickyOS layout`, async () => {
    const flash = await make();
    const adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
    assert.notEqual((await session.inspectDevice()).kind, 'upgrade');
    const firmware = plusFirmware(), boot = plusBootloader();
    assert.equal(await session.install(firmware, await release(firmware, boot), () => {}, true, () => {}, await assetsFor(boot)), true);
    assert.ok(equalBytes(slice(flash, PLUS.appOffset, firmware.length), firmware));
    const tail = PLUS.appOffset + Math.ceil(firmware.length / 4096) * 4096;
    assert.ok(flash.subarray(tail).every(b => b === 255), 'everything after app0 cleared');
    assert.ok(flash.subarray(PLUS.nvsOffset, PLUS.nvsOffset + PLUS.nvsBytes).every(b => b === 255), 'NVS cleared');
    assert.ok(equalBytes(slice(flash, PLUS.bootloaderOffset, boot.length), boot));
    assert.ok(equalBytes(slice(flash, PLUS.tableOffset, 4096), await rickyTable()));
    assert.equal(activeOffset(slice(flash, PLUS.otaOffset, PLUS.otaBytes)), PLUS.appOffset);
    assert.equal((await inspect(flash)).kind, 'upgrade', 'the result is a RickyOS board');
    const order = adapter.writes.map(([at]) => at);
    assert.equal(order[0], PLUS.appOffset);
    assert.equal(order.at(-1), PLUS.tableOffset, 'partition table last');
  });
}

test('RickyOS on the RickyOS layout gets the app only, into the slot that boots', async () => {
  for (const seq of [1, 2]) {
    const flash = await rickyFlash(seq), before = flash.slice();
    const adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
    const plan = await session.inspectDevice();
    const firmware = plusFirmware(), boot = plusBootloader();
    await session.install(firmware, await release(firmware, boot), () => {}, true, () => {}, await assetsFor(boot));
    assert.deepEqual(adapter.writes.map(([at]) => at), [plan.offset]);
    assert.ok(equalBytes(flash.subarray(0, plan.offset), before.subarray(0, plan.offset)), 'settings and records kept');
  }
});

test('mismatched firmware, bootloader, table or device is refused before any write', async () => {
  const firmware = plusFirmware(), boot = plusBootloader();
  const good = await release(firmware, boot);
  const pico = plusApp('RickyOS-Plus-1.2', 'RickyOS 1.2.0 CROSSPOINT-BOARD-V1:readpico;', 8192);
  assert.throws(() => checkPlusImage(pico, '1.2.0'), /机型标记/);
  const vendorFile = (await vendorTable()).slice(0, 3072);
  for (const [bad, assets, pattern] of [
    [{ ...good, sha256: '0'.repeat(64) }, null, /SHA-256/],
    [{ ...good, bootloader: { ...good.bootloader, sha256: '0'.repeat(64) } }, null, /启动加载器/],
    [{ ...good, partitions: { ...good.partitions, sha256: await sha256(vendorFile) } }, [boot, vendorFile], /分区/],
    [{ ...good, bytes: PLUS.slotBytes }, null, /正式发行检查/],
  ]) {
    const flash = await factoryFlash(), adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
    await session.inspectDevice();
    await assert.rejects(session.install(firmware, bad, () => {}, true, () => {}, assets ?? await assetsFor(boot)), pattern);
    assert.equal(adapter.writes.length, 0);
  }
  const flash = await factoryFlash(), adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
  await session.inspectDevice();
  await assert.rejects(session.install(firmware, { ...good, board: 'readpico' }, () => {}, true, () => {}, await assetsFor(boot)),
    /设备不一致/);
  assert.equal(adapter.writes.length, 0);
});

test('the catalog holds at most one release per board and lists each release file', async () => {
  const plus = await release();
  assert.equal(catalogReleases({ schema: 1, product: 'RickyOS', releases: [plus] })[PLUS.board].version, '1.2.0');
  assert.deepEqual(releaseFiles(plus), [plus.file, plus.bootloader.file, plus.partitions.file]);
  assert.throws(() => catalogReleases({ schema: 1, product: 'RickyOS', releases: [plus, plus] }), /最多只能有一份/);
  assert.throws(() => catalogReleases({ schema: 1, product: 'RickyOS', releases: [{ ...plus, board: 'x' }] }), /最多只能有一份/);
  assert.throws(() => checkPlusRelease({ ...plus, mode: 'auto-install' }), /正式发行检查/);
});

test('OTA files: the Read Pico manifest unchanged, the Plus beside it only once released', async () => {
  const { otaFiles, otaManifest } = await import('../scripts/build-ota.js');
  const { readFile } = await import('node:fs/promises');
  const published = JSON.parse(await readFile(new URL('../public/releases.json', import.meta.url), 'utf8'));
  // The Read Pico's offer on its own, whether or not the Plus is already in the catalog.
  const live = { ...published, releases: published.releases.filter((item) => item.board !== PLUS.board) };
  const picoOnly = otaFiles(live);
  assert.deepEqual(picoOnly.map(([path]) => path), ['ota.json', 'ota-notes.txt']);
  assert.equal(picoOnly[0][1], JSON.stringify(otaManifest(live), null, 2) + '\n');
  const plus = await release();
  const both = otaFiles({ ...live, releases: [...live.releases, { ...plus, notes: ['首个版本'] }] });
  assert.deepEqual(both.map(([path]) => path), ['ota.json', 'ota-notes.txt', 'plus/ota.json', 'plus/ota-notes.txt']);
  assert.equal(both[0][1], picoOnly[0][1]);
  const offer = JSON.parse(both[2][1]);
  assert.equal(offer.board, PLUS.board); assert.equal(offer.chipId, 32); assert.equal(offer.status, 'update_available');
  // Exactly the keys every device parser accepts.
  assert.deepEqual(Object.keys(offer), Object.keys(JSON.parse(picoOnly[0][1])));
  assert.equal(both[3][1], '1.2.0\n首个版本\n');
});
