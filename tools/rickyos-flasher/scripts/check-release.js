import { readFile, readdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { checkRelease, checkFirmware, checkInstallAssets, requireThat } from '../src/policy.js';

const root = resolve(import.meta.dirname, '../public');
const catalog = JSON.parse(await readFile(resolve(root, 'releases.json'), 'utf8'));
requireThat(catalog.schema === 1 && catalog.product === 'RickyOS' && Array.isArray(catalog.releases) &&
  catalog.releases.length <= 1, '发行目录必须为空或只有一份经过验收的固定版本。');
async function filesIn(path, prefix = '') {
  let files = [];
  for (const entry of await readdir(path, { withFileTypes: true })) {
    requireThat(!entry.isSymbolicLink(), '公开资源不得使用符号链接。');
    const relative = `${prefix}${entry.name}`;
    if (entry.isDirectory()) files = files.concat(await filesIn(resolve(path, entry.name), relative + '/'));
    else files.push(relative);
  }
  return files;
}
const paths = catalog.releases.flatMap(item => {
  checkRelease(item);
  return [item.file, ...(item.fullInstall?.segments ?? []).map(segment => segment.file)];
});
requireThat(new Set(paths).size === paths.length, '发行包文件路径不能重复。');
const listed = new Set(paths);
for (const path of await filesIn(root)) {
  requireThat(path === 'releases.json' || listed.has(path), `未登记的公开资源 ${path}，停止构建。`);
}
for (const release of catalog.releases) {
  await checkFirmware(new Uint8Array(await readFile(resolve(root, release.file))), release);
  if (release.mode === 'auto-install') {
    const assets = await Promise.all(release.fullInstall.segments.map(item => readFile(resolve(root, item.file))));
    await checkInstallAssets(assets.map(item => new Uint8Array(item)), release);
  }
}
console.log(catalog.releases.length ? '发行镜像校验通过。' : '发行目录为空：只构建网站，不附带固件。');
