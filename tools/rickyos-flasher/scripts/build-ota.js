import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { checkRelease, requireThat } from '../src/policy.js';

// The web catalog is the sole source of truth. The device gets a small flat
// derivative so it never buffers full-install profiles on its limited heap.
export function otaManifest(catalog) {
  requireThat(catalog.schema === 1 && catalog.product === 'RickyOS' && Array.isArray(catalog.releases) &&
    catalog.releases.length <= 1, 'OTA 来源必须是 RickyOS 的唯一发行目录。');
  const manifest = { schema: 1, product: 'RickyOS', board: 'readpico', channel: 'stable', status: 'no_update' };
  if (!catalog.releases.length) return manifest;
  const release = checkRelease(catalog.releases[0]);
  requireThat(release.version.length < 48 && release.file.length < 128, 'OTA 版本或文件名超过设备的固定缓冲。');
  requireThat(release.version.match(/\d+/g).every(part => Number(part) <= 0xffffffff),
    'OTA 版本数字超过设备支持范围。');
  return { ...manifest, status: 'update_available', version: release.version, file: release.file,
    bytes: release.bytes, sha256: release.sha256, approved: release.approved,
    hardwareAccepted: release.hardwareAccepted, chipId: release.chipId, flashBytes: release.flashBytes };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const root = resolve(import.meta.dirname, '..');
  const catalog = JSON.parse(await readFile(resolve(root, 'public/releases.json'), 'utf8'));
  await writeFile(resolve(root, 'dist/ota.json'), JSON.stringify(otaManifest(catalog), null, 2) + '\n');
}
