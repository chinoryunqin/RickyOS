import { sha256 } from 'hash-wasm';
import { requireThat, contains, checkNotes, FLASH_BYTES, SLOT_BYTES, RELEASE_RESERVE } from './policy.js';

// EEGO A4 (ESP32-S3 N16R8, 768x552 UC8279C). Over-the-air updates only: the A4 fetches
// eego/ota.json (COS ota/eego/ first, then the site) and its image shares firmware/.
// The web installer does not offer it — its factory flash layout has not been checked on
// hardware — so a release here carries no bootloader, table or install segments.
export const EEGO = Object.freeze({ board: 'eego_a4', chipId: 9 });
export const EEGO_TAG = `CROSSPOINT-BOARD-V1:${EEGO.board};`;
const FILE = /^firmware\/RickyOS-EEGO-A4-\d+\.\d+\.\d+\.bin$/;

export function isEegoRelease(release) { return release?.board === EEGO.board; }

export function checkEegoRelease(release) {
  requireThat(release?.approved === true && release.hardwareAccepted === true && release.mode === 'ota-only' &&
    release.board === EEGO.board && release.chipId === EEGO.chipId && release.flashBytes === FLASH_BYTES &&
    typeof release.version === 'string' && /^\d+\.\d+\.\d+$/.test(release.version) &&
    Number.isInteger(release.bytes) && release.bytes >= 24 && release.bytes % 4 === 0 &&
    Math.ceil(release.bytes / 4096) * 4096 <= SLOT_BYTES - RELEASE_RESERVE &&
    /^[a-f0-9]{64}$/.test(release.sha256) && FILE.test(release.file) &&
    release.file.includes(`-${release.version}.bin`) &&
    release.fullInstall === undefined && release.bootloader === undefined && release.partitions === undefined,
    '这份 EEGO A4 固件尚未通过正式发行检查。');
  checkNotes(release.notes);
  return release;
}

export function checkEegoImage(bytes, version) {
  requireThat(bytes.length >= 24 && bytes[0] === 0xe9 && bytes[1] >= 1 && bytes[1] <= 16 &&
    new DataView(bytes.buffer, bytes.byteOffset).getUint16(12, true) === EEGO.chipId, '不是有效的 ESP32-S3 应用镜像。');
  requireThat(contains(bytes, EEGO_TAG), '镜像缺少 EEGO A4 的机型标记。');
  const text = new TextDecoder('latin1').decode(bytes);
  const tags = [...text.matchAll(/CROSSPOINT-BOARD-V1:([\w-]+);/g)].map(m => m[1]);
  requireThat(tags.every(tag => tag === EEGO.board), '镜像包含其他机型标记。');
  requireThat(contains(bytes, 'RickyOS') && contains(bytes, version) && !contains(bytes, `${version}-dev`),
    '固件品牌或版本不匹配。');
}

export async function checkEegoFirmware(bytes, release) {
  checkEegoRelease(release);
  requireThat(bytes.length === release.bytes && await sha256(bytes) === release.sha256,
    '固件大小或 SHA-256 不匹配。');
  checkEegoImage(bytes, release.version);
}
