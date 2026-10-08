import test from 'node:test';
import assert from 'node:assert/strict';
import { otaManifest } from '../scripts/build-ota.js';

const catalog = release => ({ schema: 1, product: 'RickyOS', releases: release ? [release] : [] });
const accepted = { approved: true, hardwareAccepted: true, mode: 'app-upgrade', board: 'readpico',
  chipId: 9, flashBytes: 0x1000000, version: '1.1.1', bytes: 4096,
  sha256: 'a'.repeat(64), file: 'firmware/RickyOS-1.6.5-pico.13.bin' };
test('empty catalog emits an explicit owned stable no-update response', () => {
  assert.deepEqual(otaManifest(catalog()), { schema: 1, product: 'RickyOS', board: 'readpico',
    channel: 'stable', status: 'no_update' });
});
test('OTA offer uses the same version, bytes, checksum and file as the web installer', () => {
  const result = otaManifest(catalog(accepted));
  for (const field of ['version', 'bytes', 'sha256', 'file', 'approved', 'hardwareAccepted', 'chipId', 'flashBytes']) {
    assert.equal(result[field], accepted[field]);
  }
  assert.equal(result.status, 'update_available');
  assert.equal(result.channel, 'stable');
  assert.ok(!('fullInstall' in result));
});
test('OTA generation refuses unaccepted, development, wrong-board and wrong-brand releases', () => {
  for (const change of [{approved:false}, {hardwareAccepted:false}, {version:'1.6.5-readpico-rc'},
    {version:'1.1.1-dev'}, {version:'1.6.5-rickyos-pico.13'}, {board:'x4pro'}, {sha256:'not-a-digest'},
    {file:'https://crossmux.com/firmware.bin'}, {bytes:0x640000}]) {
    assert.throws(() => otaManifest(catalog({...accepted, ...change})));
  }
  assert.throws(() => otaManifest({...catalog(), product:'CrossMux'}));
});
test('OTA generation respects device fixed-buffer limits and refuses multiple offers', () => {
  assert.throws(() => otaManifest(catalog({...accepted, version:'4294967296.0.0'})));
  assert.throws(() => otaManifest(catalog({...accepted, file:`firmware/${'a'.repeat(150)}.bin`})));
  assert.throws(() => otaManifest({...catalog(), releases:[accepted, accepted]}));
});
test('notes travel in their own file, never as an ota.json key old devices refuse', async () => {
  const { otaNotes } = await import('../scripts/build-ota.js');
  const notes = ['待机新增日历时钟', '短按侧键待机，长按关机'];
  const offer = { ...accepted, notes };
  assert.ok(!('notes' in otaManifest(catalog(offer))));
  assert.equal(otaNotes(catalog(offer)), '1.1.1\n待机新增日历时钟\n短按侧键待机，长按关机\n');
  assert.equal(otaNotes(catalog(accepted)), '1.1.1\n');
  assert.equal(otaNotes(catalog()), '');
  for (const bad of [Array(9).fill('a'), ['一'.repeat(33)], ['two\nlines'], [' padded'], [''], 'text']) {
    assert.throws(() => otaNotes(catalog({ ...accepted, notes: bad })));
  }
  assert.doesNotThrow(() => otaNotes(catalog({ ...accepted, notes: ['一'.repeat(32)] })));
});
