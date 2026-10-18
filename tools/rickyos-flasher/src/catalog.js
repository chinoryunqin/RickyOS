import { checkRelease, requireThat } from './policy.js';
import { checkPlusRelease, PLUS } from './plus.js';
import { EEGO } from './eego.js';

// releases.json: at most one checked release per board. Read Pico keeps its existing
// entry format; other boards carry their own mode and are checked by their own policy.
export const BOARDS = Object.freeze({ readpico: checkRelease, [PLUS.board]: checkPlusRelease,
  [EEGO.board]: checkRelease });

export function catalogReleases(catalog) {
  requireThat(catalog?.schema === 1 && catalog.product === 'RickyOS' && Array.isArray(catalog.releases),
    '发行目录格式错误。');
  const byBoard = {};
  for (const release of catalog.releases) {
    const check = BOARDS[release?.board];
    requireThat(check && !byBoard[release.board], '发行目录中每个设备最多只能有一份经过验收的版本。');
    byBoard[release.board] = check(release);
  }
  return byBoard;
}

// Files a release publishes, in the order the installer downloads them.
export function releaseFiles(release) {
  if (release.board === PLUS.board) return [release.file, release.bootloader.file, release.partitions.file];
  return [release.file, ...(release.fullInstall?.segments ?? []).map(segment => segment.file)];
}
