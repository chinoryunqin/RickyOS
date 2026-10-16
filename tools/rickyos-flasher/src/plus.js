import { sha256 } from 'hash-wasm';
import { requireThat, contains, otaCrc, checkNotes, FLASH_BYTES } from './policy.js';

// Metalio E-Ink4-Plus (ESP32-S31). Verified on hardware 2026-10-17: the vendor ships
// this exact partition table (Metalio-E-Ink4-Plus 2.0.61 in ota_0, ota_1 empty), and
// RickyOS keeps it. The installer never rewrites the table or NVS: it writes the
// bootloader RickyOS's IDF needs (the vendor's leaves the PSRAM cache unconfigured),
// RickyOS into ota_1, and last the boot record that selects ota_1.
export const PLUS = Object.freeze({
  board: 'metalio-plus-s31',
  chipName: 'ESP32-S31',
  chipId: 32,
  bootloaderOffset: 0x2000,
  bootloaderMax: 0x6000,          // up to the partition table at 0x8000
  tableOffset: 0x8000,
  tableSha256: '6428c34dea4556ac826952221e9c99094c6963f21613b5db03e309f7a8d09ebb',
  otaOffset: 0xd000,
  otaBytes: 0x2000,
  slots: [0x80000, 0x580000],
  slotBytes: 0x500000,
  appOffset: 0x580000,             // ota_1
  factoryProject: 'Metalio-E-Ink4-Plus',
  rickyProjectPrefix: 'RickyOS-Plus',
});
export const PLUS_TAG = `CROSSPOINT-BOARD-V1:${PLUS.board};`;

export function isPlusRelease(release) { return release?.board === PLUS.board; }

export function checkPlusRelease(release) {
  requireThat(release?.approved === true && release.hardwareAccepted === true && release.mode === 'plus-install' &&
    release.board === PLUS.board && release.chipId === PLUS.chipId && release.flashBytes === FLASH_BYTES &&
    typeof release.version === 'string' && /^\d+\.\d+\.\d+$/.test(release.version) &&
    Number.isInteger(release.bytes) && release.bytes >= 24 && release.bytes % 4 === 0 &&
    Math.ceil(release.bytes / 4096) * 4096 <= PLUS.slotBytes &&
    /^[a-f0-9]{64}$/.test(release.sha256) && /^firmware\/[A-Za-z0-9._-]+\.bin$/.test(release.file),
    '这份固件尚未通过正式发行检查，安装入口保持关闭。');
  checkNotes(release.notes);
  const boot = release.bootloader;
  requireThat(boot?.offset === PLUS.bootloaderOffset && Number.isInteger(boot.bytes) && boot.bytes >= 24 &&
    boot.bytes <= PLUS.bootloaderMax && boot.bytes % 4 === 0 && /^[a-f0-9]{64}$/.test(boot.sha256) &&
    /^firmware\/[A-Za-z0-9._-]+\.bin$/.test(boot.file) && boot.file !== release.file,
    '启动加载器的发行信息不合法。');
  return release;
}

function checkS31Header(bytes, what) {
  requireThat(bytes.length >= 24 && bytes[0] === 0xe9 && bytes[1] >= 1 && bytes[1] <= 16 &&
    new DataView(bytes.buffer, bytes.byteOffset).getUint16(12, true) === PLUS.chipId, `不是有效的 ESP32-S31 ${what}。`);
}

export function checkPlusImage(bytes, version) {
  checkS31Header(bytes, '应用镜像');
  requireThat(contains(bytes, PLUS_TAG), '镜像缺少这台设备的机型标记。');
  const text = new TextDecoder('latin1').decode(bytes);
  const tags = [...text.matchAll(/CROSSPOINT-BOARD-V1:([\w-]+);/g)].map(m => m[1]);
  requireThat(tags.every(tag => tag === PLUS.board), '镜像包含其他机型标记。');
  requireThat(contains(bytes, 'RickyOS') && contains(bytes, version), '固件品牌或版本不匹配。');
}

export async function checkPlusFirmware(bytes, release) {
  checkPlusRelease(release);
  requireThat(bytes.length === release.bytes && await sha256(bytes) === release.sha256,
    '固件大小或 SHA-256 不匹配，禁止写入。');
  checkPlusImage(bytes, release.version);
}

export async function checkPlusBootloader(bytes, release) {
  checkPlusRelease(release);
  requireThat(bytes instanceof Uint8Array && bytes.length === release.bootloader.bytes &&
    await sha256(bytes) === release.bootloader.sha256, '启动加载器大小或 SHA-256 错误。');
  checkS31Header(bytes, '启动加载器');
}

// The boot record that wins: ESP-IDF picks the valid record with the highest sequence,
// slot (seq - 1) % 2. Records with sequence 0/0xffffffff or a bad CRC do not vote.
function validSequences(ota) {
  requireThat(ota.length === PLUS.otaBytes, '启动记录读取不完整。');
  const seqs = [];
  for (const at of [0, 4096]) {
    const view = new DataView(ota.buffer, ota.byteOffset + at, 32);
    const seq = view.getUint32(0, true);
    if (seq > 0 && seq < 0xffffffff && view.getUint32(28, true) === otaCrc(ota.subarray(at, at + 4))) seqs.push(seq);
  }
  return seqs;
}

export function plusActiveOffset(ota) {
  const seqs = validSequences(ota);
  // A blank record means the bootloader starts the first app slot.
  return seqs.length ? PLUS.slots[(Math.max(...seqs) - 1) % 2] : PLUS.slots[0];
}

// A complete 8 KiB boot-record area selecting ota_1: one record, the other sector erased.
// State 0xffffffff (undefined) boots directly; RickyOS builds have app rollback off.
export function plusBootRecord(ota) {
  const seqs = validSequences(ota);
  let seq = Math.max(0, ...seqs) + 1;
  if ((seq - 1) % 2 !== 1) seq += 1;
  requireThat(seq < 0xffffffff, '启动记录序号异常，停止。');
  const out = new Uint8Array(PLUS.otaBytes).fill(255);
  const view = new DataView(out.buffer);
  view.setUint32(0, seq, true);
  view.setUint32(24, 0xffffffff, true);
  view.setUint32(28, otaCrc(out.subarray(0, 4)), true);
  return out;
}

function projectOf(head) {
  const view = new DataView(head.buffer, head.byteOffset);
  if (view.getUint32(32, true) !== 0xabcd5432) return '';
  return new TextDecoder().decode(head.subarray(80, 112)).split('\0')[0];
}

// Decide from the partition table, boot records and the head of the app that boots.
// Only the verified table is accepted; the vendor system or an earlier RickyOS install
// get the same writes. Anything else stops: guessing a layout could brick the board.
export async function inspectPlus(table, ota, readAt) {
  requireThat(table.length === 4096 && await sha256(table) === PLUS.tableSha256,
    '设备分区与已验证的布局不一致，不能安装。请保存记录并联系 Ricky AI Studio。');
  const active = plusActiveOffset(ota);
  const head = await readAt(active, 4096);
  checkS31Header(head, '应用镜像');
  const project = projectOf(head);
  if (project === PLUS.factoryProject) return { kind: 'factory', device: 'plus', table, ota, active };
  if (project.startsWith(PLUS.rickyProjectPrefix)) return { kind: 'upgrade', device: 'plus', table, ota, active };
  throw new Error('没有识别到原厂系统或 RickyOS，不能自动安装。请保存记录并联系 Ricky AI Studio。');
}

export async function inspectPlusBackup(bytes) {
  requireThat(bytes.length === FLASH_BYTES, '完整 Flash 备份必须为 16 MB。');
  const table = bytes.slice(PLUS.tableOffset, PLUS.tableOffset + 4096);
  const ota = bytes.slice(PLUS.otaOffset, PLUS.otaOffset + PLUS.otaBytes);
  return inspectPlus(table, ota, async (at, size) => bytes.slice(at, at + size));
}
