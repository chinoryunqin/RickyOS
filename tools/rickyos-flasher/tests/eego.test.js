import test from 'node:test';
import assert from 'node:assert/strict';
import { otaFiles, otaManifest } from '../scripts/build-ota.js';
import { catalogReleases } from '../src/catalog.js';
import { EEGO, EEGO_TAG, checkEegoImage, checkEegoRelease } from '../src/eego.js';

const catalog = (...releases) => ({ schema: 1, product: 'RickyOS', releases });
const eego = { approved: true, hardwareAccepted: true, mode: 'ota-only', board: 'eego_a4', chipId: 9,
  flashBytes: 0x1000000, version: '1.2.2', bytes: 4096, sha256: 'b'.repeat(64),
  file: 'firmware/RickyOS-EEGO-A4-1.2.2.bin', notes: ['新增猫猫主题'] };
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
  assert.equal(files['eego/ota-notes.txt'], '1.2.2\n新增猫猫主题\n');
  assert.equal(JSON.parse(files['ota.json']).board, 'readpico');
  assert.deepEqual(otaManifest(catalog(pico, eego)), otaManifest(catalog(pico)));
});

test('no A4 files before the catalog has an A4 release', () => {
  assert.ok(!Object.fromEntries(otaFiles(catalog(pico)))['eego/ota.json']);
});

test('an A4 release is OTA-only and names its own board and file', () => {
  assert.equal(catalogReleases(catalog(eego))[EEGO.board], eego);
  for (const change of [{ mode: 'auto-install' }, { board: 'readpico' }, { chipId: 32 }, { version: '1.2.2-dev' },
    { file: 'firmware/RickyOS-1.2.2.bin' }, { file: 'firmware/RickyOS-EEGO-A4-1.2.1.bin' }, { bytes: 0x640000 },
    { bootloader: { offset: 0 } }, { fullInstall: { segments: [] } }]) {
    assert.throws(() => checkEegoRelease({ ...eego, ...change }));
  }
});

test('an A4 image must carry only the A4 board tag and the release version', () => {
  checkEegoImage(image(EEGO_TAG), '1.2.2');
  assert.throws(() => checkEegoImage(image('CROSSPOINT-BOARD-V1:readpico;'), '1.2.2'));
  assert.throws(() => checkEegoImage(image(`${EEGO_TAG} CROSSPOINT-BOARD-V1:readpico;`), '1.2.2'));
  assert.throws(() => checkEegoImage(image(EEGO_TAG, '1.2.2-dev'), '1.2.2'));
});
