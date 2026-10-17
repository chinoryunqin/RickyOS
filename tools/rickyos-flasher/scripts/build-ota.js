import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { requireThat } from '../src/policy.js';
import { catalogReleases } from '../src/catalog.js';
import { PLUS } from '../src/plus.js';
import { EEGO } from '../src/eego.js';

// The web catalog is the sole source of truth. Each device gets a small flat derivative
// so it never buffers full-install profiles on its limited heap. The Read Pico's stays at
// ota.json (every shipped Read Pico release reads it); other boards get <dir>/ota.json.
export const OTA_DIRS = Object.freeze({ readpico: '', [PLUS.board]: 'plus/', [EEGO.board]: 'eego/' });

export function otaManifest(catalog, board = 'readpico') {
  const release = catalogReleases(catalog)[board];
  const manifest = { schema: 1, product: 'RickyOS', board, channel: 'stable', status: 'no_update' };
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
  const release = catalogReleases(catalog)[board];
  if (!release) return '';
  return [release.version, ...(release.notes ?? [])].join('\n') + '\n';
}

// Files to write into dist/: the Read Pico's always; another board's only once the catalog
// has its release, so the site never names a device before it ships.
export function otaFiles(catalog) {
  const releases = catalogReleases(catalog), files = [];
  for (const [board, dir] of Object.entries(OTA_DIRS)) {
    if (board !== 'readpico' && !releases[board]) continue;
    files.push([`${dir}ota.json`, JSON.stringify(otaManifest(catalog, board), null, 2) + '\n']);
    files.push([`${dir}ota-notes.txt`, otaNotes(catalog, board)]);
  }
  return files;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const root = resolve(import.meta.dirname, '..');
  const catalog = JSON.parse(await readFile(resolve(root, 'public/releases.json'), 'utf8'));
  for (const [path, text] of otaFiles(catalog)) {
    await mkdir(dirname(resolve(root, 'dist', path)), { recursive: true });
    await writeFile(resolve(root, 'dist', path), text);
  }
}
