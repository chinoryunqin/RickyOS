import test from 'node:test';
import assert from 'node:assert/strict';
import { otaFiles, otaManifest } from '../scripts/build-ota.js';
import { catalogReleases } from '../src/catalog.js';
import { EEGO, EEGO_TAG } from '../src/eego.js';
import { checkImage, checkPlanRelease, checkRelease } from '../src/policy.js';

const catalog = (...releases) => ({ schema: 1, product: 'RickyOS', releases });
const segments = [
  { role: 'bootloader', offset: 0, file: 'firmware/RickyOS-EEGO-A4-1.2.2-bootloader.bin', bytes: 18720, sha256: 'c'.repeat(64) },
  { role: 'partitions', offset: 32768, file: 'firmware/RickyOS-EEGO-A4-1.2.2-partitions.bin', bytes: 3072, sha256: 'd'.repeat(64) },
  { role: 'boot_app0', offset: 57344, file: 'firmware/RickyOS-EEGO-A4-1.2.2-boot_app0.bin', bytes: 8192, sha256: 'e'.repeat(64) },
];
const eego = { approved: true, hardwareAccepted: true, mode: 'auto-install', board: 'eego_a4', chipId: 9,
  flashBytes: 0x1000000, version: '1.2.2', bytes: 4096, sha256: 'b'.repeat(64),
  file: 'firmware/RickyOS-EEGO-A4-1.2.2.bin', notes: ['新增猫猫主题'],
  fullInstall: { approved: true, hardwareAccepted: true, factoryLayouts: [], otherSystems: true, segments } };
const pico = { approved: true, hardwareAccepted: true, mode: 'app-upgrade', board: 'readpico', chipId: 9,
  flashBytes: 0x1000000, version: '1.2.2', bytes: 4096, sha256: 'a'.repeat(64), file: 'firmware/RickyOS-1.2.2.bin' };

function image(tag, version = '1.2.2') {
  const text = new TextEncoder().encode(`RickyOS ${version} ${tag}`);
  const bytes = new Uint8Array(4096);
  bytes[0] = 0xe9; bytes[1] = 4; new DataView(bytes.buffer).setUint16(12, 9, true);
  bytes.set(text, 64);
  return bytes;
}

test('the A4 gets its own eego/ota.json and the Read Pico offer is unchanged', () => {
  const files = Object.fromEntries(otaFiles(catalog(pico, eego)));
  const offer = JSON.parse(files['eego/ota.json']);
  assert.equal(offer.board, EEGO.board);
  assert.equal(offer.file, eego.file);
  assert.ok(!('fullInstall' in offer));
  assert.equal(files['eego/ota-notes.txt'], '1.2.2\n新增猫猫主题\n');
  assert.deepEqual(otaManifest(catalog(pico, eego)), otaManifest(catalog(pico)));
});

test('no A4 files before the catalog has an A4 release', () => {
  assert.ok(!Object.fromEntries(otaFiles(catalog(pico)))['eego/ota.json']);
});

test('an A4 release installs other systems only and never names a factory layout', () => {
  assert.equal(catalogReleases(catalog(eego))[EEGO.board], eego);
  for (const change of [{ chipId: 32 }, { version: '1.2.2-dev' }, { bytes: 0x640000 },
    { fullInstall: { ...eego.fullInstall, otherSystems: false } },
    { fullInstall: { ...eego.fullInstall, factoryLayouts: ['mindreset-factory-4m-v1'] } }]) {
    assert.throws(() => checkRelease({ ...eego, ...change }));
  }
});

test('a Read Pico factory system is refused for the A4; other systems get the complete install', () => {
  assert.throws(() => checkPlanRelease({ kind: 'factory', layout: 'mindreset-factory-4m-v1' }, eego));
  checkPlanRelease({ kind: 'other' }, eego);
  checkPlanRelease({ kind: 'upgrade' }, eego);
});

test('an A4 image must carry only the A4 board tag; a Read Pico image is not one', () => {
  checkImage(image(EEGO_TAG), true, '1.2.2', 'eego_a4');
  assert.throws(() => checkImage(image('CROSSPOINT-BOARD-V1:readpico;'), true, '1.2.2', 'eego_a4'));
  assert.throws(() => checkImage(image(`${EEGO_TAG} CROSSPOINT-BOARD-V1:readpico;`), true, '1.2.2', 'eego_a4'));
  assert.throws(() => checkImage(image(EEGO_TAG), true, '1.2.2', 'readpico'));
});
