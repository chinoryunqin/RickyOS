import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { checkRelease, requireThat } from '../src/policy.js';

// The web catalog is the sole source of truth. The device gets a small flat
// derivative so it never buffers full-install profiles on its limited heap.
function releaseForBoard(catalog, board) {
  requireThat(catalog.schema === 1 && catalog.product === 'RickyOS' && Array.isArray(catalog.releases) &&
    catalog.releases.length <= 1, 'OTA 来源必须是 RickyOS 的唯一发行目录。');
  requireThat(board === 'readpico' || board === 'metalio_eink4', '不支持这款设备的 OTA。');
  const release = board === 'readpico' ? catalog.releases[0] : catalog.metalioEink4;
  return release == null ? null : checkRelease(release, board);
}

export function otaManifest(catalog, board = 'readpico') {
  const manifest = { schema: 1, product: 'RickyOS', board, channel: 'stable', status: 'no_update' };
  const release = releaseForBoard(catalog, board);
  if (!release) return manifest;
  requireThat(release.version.length < 48 && release.file.length < 128, 'OTA 版本或文件名超过设备的固定缓冲。');
  requireThat(release.version.match(/\d+/g).every(part => Number(part) <= 0xffffffff),
    'OTA 版本数字超过设备支持范围。');
  return { ...manifest, status: 'update_available', version: release.version, file: release.file,
    bytes: release.bytes, sha256: release.sha256, approved: release.approved,
    hardwareAccepted: release.hardwareAccepted, chipId: release.chipId, flashBytes: release.flashBytes };
}

// What's new for the offered version, fetched separately: ota.json keeps the exact keys
// devices from 1.1.2 accept (their parser refuses unknown ones). The first line names
// the version, so notes never show against a different offer.
export function otaNotes(catalog, board = 'readpico') {
  const release = releaseForBoard(catalog, board);
  if (!release) return '';
  return [release.version, ...(release.notes ?? [])].join('\n') + '\n';
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const root = resolve(import.meta.dirname, '..');
  const catalog = JSON.parse(await readFile(resolve(root, 'public/releases.json'), 'utf8'));
  await writeFile(resolve(root, 'dist/ota.json'), JSON.stringify(otaManifest(catalog), null, 2) + '\n');
  await writeFile(resolve(root, 'dist/ota-notes.txt'), otaNotes(catalog));
  const metalio = resolve(root, 'dist/metalio-eink4');
  await mkdir(metalio, { recursive: true });
  await writeFile(resolve(metalio, 'ota.json'), JSON.stringify(otaManifest(catalog, 'metalio_eink4'), null, 2) + '\n');
  await writeFile(resolve(metalio, 'ota-notes.txt'), otaNotes(catalog, 'metalio_eink4'));
}
