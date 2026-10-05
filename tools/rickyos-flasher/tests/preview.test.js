import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, mkdir, writeFile, symlink } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { checkPreview } from '../scripts/check-preview.js';

async function fixture() {
  const directory = await mkdtemp(join(tmpdir(), 'rickyos-preview-test-'));
  for (const file of ['index.html', 'install.html', 'licenses.html', 'third-party-licenses.txt']) {
    await writeFile(join(directory, file), 'test fixture');
  }
  await writeFile(join(directory, 'releases.json'), JSON.stringify({ schema: 1, product: 'RickyOS', releases: [] }));
  await writeFile(join(directory, 'ota.json'), JSON.stringify({ schema: 1, product: 'RickyOS',
    board: 'readpico', channel: 'stable', status: 'no_update' }));
  await mkdir(join(directory, 'assets'));
  await writeFile(join(directory, 'assets/site-test.js'), '// test fixture');
  return directory;
}
test('preview deployment accepts only an empty catalog and website files', async () => {
  assert.deepEqual(await checkPreview(await fixture()), { files: 7, firmware: 0 });
});
test('preview deployment rejects an OTA offer even if the web catalog is empty', async () => {
  const directory = await fixture();
  await writeFile(join(directory, 'ota.json'), JSON.stringify({ schema: 1, product: 'RickyOS',
    board: 'readpico', channel: 'stable', status: 'update_available' }));
  await assert.rejects(checkPreview(directory), /设备 OTA 必须明确无更新/);
});
test('preview deployment rejects a nonempty firmware catalog', async () => {
  const directory = await fixture();
  await writeFile(join(directory, 'releases.json'), JSON.stringify({ schema: 1, product: 'RickyOS', releases: [{}] }));
  await assert.rejects(checkPreview(directory), /发行目录必须为空/);
});
test('preview deployment rejects binaries, source maps and unexpected photos', async () => {
  for (const file of ['firmware.bin', 'assets/private-photo.png', 'assets/site-test.js.map']) {
    const directory = await fixture();
    await writeFile(join(directory, file), 'must not publish');
    await assert.rejects(checkPreview(directory), /未经审核的文件/);
  }
});
test('preview deployment rejects symlinks and source directories', async () => {
  const directory = await fixture();
  await symlink(join(directory, 'index.html'), join(directory, 'alias.html'));
  await assert.rejects(checkPreview(directory), /符号链接/);
  const other = await fixture();
  await mkdir(join(other, 'src'));
  await assert.rejects(checkPreview(other), /额外目录/);
});
