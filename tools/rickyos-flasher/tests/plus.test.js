import test from 'node:test';
import assert from 'node:assert/strict';
import { md5, sha256 } from 'hash-wasm';
import { FLASH_BYTES, equalBytes } from '../src/policy.js';
import { PLUS, inspectPlus, inspectPlusBackup, plusActiveOffset, plusBootRecord, checkPlusRelease,
  checkPlusImage } from '../src/plus.js';
import { catalogReleases, releaseFiles } from '../src/catalog.js';
import { FlashSession } from '../src/session.js';
import { plusTable, otaRecord, plusApp, plusFirmware, plusBootloader, factoryFlash } from './plus-fixture.js';

class FakeAdapter {
  constructor(bytes) { this.bytes = bytes; this.writes = []; }
  async read(at, size, progress) { progress?.(size, size); return this.bytes.slice(at, at + size); }
  digest(at, size) { return md5(this.bytes.subarray(at, at + size)); }
  async write(bytes, at, progress) { this.writes.push([at, bytes.length]); this.bytes.set(bytes, at); progress?.(bytes.length, bytes.length); }
  async reset() {}
}
async function tableFile() { return (await plusTable()).slice(0, 3072); }
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
const readerOf = flash => async (at, size) => flash.slice(at, at + size);

test('fixture partition table is byte-identical to the one read from the device', async () => {
  assert.equal(await sha256(await plusTable()), PLUS.tableSha256);
});

test('the vendor system, an earlier RickyOS and anything else are told apart', async () => {
  const flash = await factoryFlash();
  const table = flash.slice(PLUS.tableOffset, PLUS.tableOffset + 4096);
  assert.equal((await inspectPlus(table, otaRecord(1), readerOf(flash))).kind, 'factory');
  flash.set(plusFirmware(), PLUS.slots[1]);
  assert.equal((await inspectPlus(table, otaRecord(2), readerOf(flash))).kind, 'upgrade');
  flash.set(plusApp('SomethingElse'), PLUS.slots[1]);
  const other = await inspectPlus(table, otaRecord(2), readerOf(flash));
  assert.equal(other.kind, 'other'); assert.equal(other.sameLayout, true);
  flash.fill(0, PLUS.slots[1], PLUS.slots[1] + 4096); // not even an app image
  assert.equal((await inspectPlus(table, otaRecord(2), readerOf(flash))).kind, 'other');
  // A blank boot record starts ota_0.
  assert.equal(plusActiveOffset(otaRecord(0)), PLUS.slots[0]);
  assert.equal((await inspectPlusBackup(await factoryFlash())).kind, 'factory');
});

test('another partition table is a complete install that rewrites the table', async () => {
  const flash = await factoryFlash();
  const table = flash.slice(PLUS.tableOffset, PLUS.tableOffset + 4096);
  table[40] ^= 1;
  const plan = await inspectPlus(table, otaRecord(1), readerOf(flash));
  assert.equal(plan.kind, 'other'); assert.equal(plan.sameLayout, false);
});

test('the boot record always selects ota_1 with a higher sequence', () => {
  for (const current of [0, 1, 2, 7, 8]) {
    const record = plusBootRecord(otaRecord(current));
    assert.equal(plusActiveOffset(record), PLUS.appOffset);
    assert.ok(new DataView(record.buffer).getUint32(0, true) > current);
    assert.ok(record.subarray(4096).every(b => b === 255));
  }
});

test('factory install writes ota_1, the bootloader and the boot record; table and NVS untouched', async () => {
  const flash = await factoryFlash(), before = flash.slice();
  const adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
  const plan = await session.inspectDevice();
  assert.equal(plan.kind, 'factory');
  const firmware = plusFirmware(), boot = plusBootloader();
  const phases = [];
  assert.equal(await session.install(firmware, await release(firmware, boot), () => {}, true, p => phases.push(p), await assetsFor(boot)), true);
  assert.ok(equalBytes(flash.subarray(PLUS.appOffset, PLUS.appOffset + firmware.length), firmware));
  assert.ok(equalBytes(flash.subarray(PLUS.bootloaderOffset, PLUS.bootloaderOffset + boot.length), boot));
  assert.equal(plusActiveOffset(flash.slice(PLUS.otaOffset, PLUS.otaOffset + PLUS.otaBytes)), PLUS.appOffset);
  assert.ok(equalBytes(flash.subarray(0x8000, 0x9000), before.subarray(0x8000, 0x9000)), 'partition table');
  assert.ok(equalBytes(flash.subarray(0x9000, 0xd000), before.subarray(0x9000, 0xd000)), 'NVS');
  assert.ok(equalBytes(flash.subarray(PLUS.slots[0], PLUS.slots[1]), before.subarray(PLUS.slots[0], PLUS.slots[1])), 'ota_0');
  // The boot record goes last.
  assert.deepEqual(adapter.writes.map(([at]) => at), [PLUS.appOffset, PLUS.bootloaderOffset, PLUS.otaOffset]);
});

test('reinstalling over RickyOS skips an identical bootloader', async () => {
  const flash = await factoryFlash(), boot = plusBootloader();
  flash.set(boot, PLUS.bootloaderOffset); flash.fill(255, PLUS.bootloaderOffset + boot.length, 0x8000);
  flash.set(plusFirmware('1.1.9'), PLUS.slots[1]); flash.set(otaRecord(2), PLUS.otaOffset);
  const adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
  assert.equal((await session.inspectDevice()).kind, 'upgrade');
  const firmware = plusFirmware();
  await session.install(firmware, await release(firmware, boot), () => {}, true, () => {}, await assetsFor(boot));
  assert.deepEqual(adapter.writes.map(([at]) => at), [PLUS.appOffset, PLUS.otaOffset]);
  assert.equal(plusActiveOffset(flash.slice(PLUS.otaOffset, PLUS.otaOffset + PLUS.otaBytes)), PLUS.appOffset);
});

test('mismatched firmware, bootloader or device is refused before any write', async () => {
  const firmware = plusFirmware(), boot = plusBootloader();
  const good = await release(firmware, boot);
  // Wrong board tag in the image.
  const pico = plusApp('RickyOS-Plus-1.2', 'RickyOS 1.2.0 CROSSPOINT-BOARD-V1:readpico;', 8192);
  assert.throws(() => checkPlusImage(pico, '1.2.0'), /机型标记/);
  for (const [bad, pattern] of [
    [{ ...good, sha256: '0'.repeat(64) }, /SHA-256/],
    [{ ...good, bootloader: { ...good.bootloader, sha256: '0'.repeat(64) } }, /启动加载器/],
    [{ ...good, bytes: PLUS.slotBytes + 4 }, /正式发行检查/],
    [{ ...good, partitions: { ...good.partitions, sha256: '0'.repeat(64) } }, /分区表/],
  ]) {
    const flash = await factoryFlash(), adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
    await session.inspectDevice();
    await assert.rejects(session.install(firmware, bad, () => {}, true, () => {}, await assetsFor(boot)), pattern);
    assert.equal(adapter.writes.length, 0);
  }
  // A Plus release on a Read Pico session, or the reverse, never writes.
  const flash = await factoryFlash(), adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
  await session.inspectDevice();
  await assert.rejects(session.install(firmware, { ...good, board: 'readpico' }, () => {}, true, () => {}, await assetsFor(boot)),
    /设备不一致/);
  assert.equal(adapter.writes.length, 0);
});

test('the catalog holds at most one release per board and lists each release file', async () => {
  const plus = await release();
  const byBoard = catalogReleases({ schema: 1, product: 'RickyOS', releases: [plus] });
  assert.equal(byBoard[PLUS.board].version, '1.2.0');
  assert.deepEqual(releaseFiles(plus), [plus.file, plus.bootloader.file, plus.partitions.file]);
  assert.throws(() => catalogReleases({ schema: 1, product: 'RickyOS', releases: [plus, plus] }), /最多只能有一份/);
  assert.throws(() => catalogReleases({ schema: 1, product: 'RickyOS', releases: [{ ...plus, board: 'x' }] }), /最多只能有一份/);
  assert.throws(() => checkPlusRelease({ ...plus, mode: 'auto-install' }), /正式发行检查/);
});

async function otherFlash(changeTable) {
  const flash = await factoryFlash();
  flash.set(plusApp('SomeOtherFirmware'), PLUS.slots[0]);
  flash.fill(0x33, PLUS.slots[0] + 4096, PLUS.slots[0] + 0x10000); // the other system's code in ota_0
  if (changeTable) {
    // Another firmware's layout: a different table at 0x8000, junk where the boot record goes.
    flash.fill(0, PLUS.tableOffset, PLUS.tableOffset + 4096); flash.set([0xaa, 0x50, 0, 0x10], PLUS.tableOffset);
    flash.fill(0x77, PLUS.otaOffset, PLUS.otaOffset + PLUS.otaBytes);
  }
  return flash;
}
for (const changeTable of [false, true]) {
  test(`another system is replaced completely (${changeTable ? 'other layout' : 'same layout'})`, async () => {
    const flash = await otherFlash(changeTable), before = flash.slice();
    const adapter = new FakeAdapter(flash), session = new FlashSession(adapter, 'plus');
    const plan = await session.inspectDevice();
    assert.equal(plan.kind, 'other'); assert.equal(plan.sameLayout, !changeTable);
    const firmware = plusFirmware(), boot = plusBootloader();
    assert.equal(await session.install(firmware, await release(firmware, boot), () => {}, true, () => {}, await assetsFor(boot)), true);
    assert.ok(equalBytes(flash.subarray(PLUS.appOffset, PLUS.appOffset + firmware.length), firmware));
    assert.ok(flash.subarray(PLUS.nvsOffset, PLUS.nvsOffset + PLUS.nvsBytes).every(b => b === 255), 'NVS cleared');
    assert.ok(flash.subarray(PLUS.slots[0], PLUS.slots[0] + PLUS.slotBytes).every(b => b === 255), 'ota_0 cleared');
    assert.equal(await sha256(flash.slice(PLUS.tableOffset, PLUS.tableOffset + 4096)), PLUS.tableSha256);
    const ota = flash.slice(PLUS.otaOffset, PLUS.otaOffset + PLUS.otaBytes);
    assert.equal(plusActiveOffset(ota), PLUS.appOffset);
    assert.equal(new DataView(ota.buffer).getUint32(0, true), 2, 'fresh boot record');
    // Untouched: the vendor data partitions RickyOS never reads.
    assert.ok(equalBytes(flash.subarray(0xa80000, 0x1000000), before.subarray(0xa80000, 0x1000000)));
    const order = adapter.writes.map(([at]) => at);
    assert.equal(order[0], PLUS.appOffset);
    assert.equal(order.at(-1), changeTable ? PLUS.tableOffset : PLUS.otaOffset, 'table (when needed) goes last');
    assert.equal(order.includes(PLUS.tableOffset), changeTable);
  });
}
