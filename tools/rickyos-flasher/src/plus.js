import { sha256 } from 'hash-wasm';
import { requireThat, contains, otaCrc, checkNotes, checkPartitions, activeOffset, FLASH_BYTES, SLOT_BYTES,
  RELEASE_RESERVE } from './policy.js';

// Metalio E-Ink4-Plus (ESP32-S31). RickyOS runs it on the Read Pico's partition layout
// (byte-identical table: app0 0x10000 / app1 0x650000, 6.25 MiB each, boot record at
// 0xe000). The vendor table (verified on hardware 2026-10-17: Metalio-E-Ink4-Plus 2.0.61
// in ota_0) gave RickyOS a 5 MiB slot and ~6 MB of partitions it never reads, so every
// board not already on the RickyOS layout gets the complete install: RickyOS into app0,
// the rest of the flash and NVS cleared, the bootloader RickyOS's IDF needs (the
// vendor's leaves the PSRAM cache unconfigured), a boot record selecting app0, and the
// partition table last. A board already running RickyOS on this layout only gets the app.
export const PLUS = Object.freeze({
  board: 'metalio-plus-s31',
  chipName: 'ESP32-S31',
  chipId: 32,
  bootloaderOffset: 0x2000,        // the S31 ROM reserves the first two sectors
  bootloaderMax: 0x6000,           // up to the partition table at 0x8000
  tableOffset: 0x8000,
  nvsOffset: 0x9000,
  nvsBytes: 0x5000,
  otaOffset: 0xe000,
  otaBytes: 0x2000,
  appOffset: 0x10000,              // app0
  slotBytes: SLOT_BYTES,
  vendorTableSha256: '6428c34dea4556ac826952221e9c99094c6963f21613b5db03e309f7a8d09ebb',
  vendorOtaOffset: 0xd000,
  vendorSlots: [0x80000, 0x580000],
  factoryProject: 'Metalio-E-Ink4-Plus',
  rickyProjectPrefix: 'RickyOS-Plus',
});
export const PLUS_TAG = `CROSSPOINT-BOARD-V1:${PLUS.board};`;
const FILE = /^firmware\/[A-Za-z0-9._-]+\.bin$/;

export function isPlusRelease(release) { return release?.board === PLUS.board; }

export function checkPlusRelease(release) {
  requireThat(release?.approved === true && release.hardwareAccepted === true && release.mode === 'plus-install' &&
    release.board === PLUS.board && release.chipId === PLUS.chipId && release.flashBytes === FLASH_BYTES &&
    typeof release.version === 'string' && /^\d+\.\d+\.\d+$/.test(release.version) &&
    Number.isInteger(release.bytes) && release.bytes >= 24 && release.bytes % 4 === 0 &&
    // The same headroom rule as the Read Pico, so the image can later be offered over the air.
    Math.ceil(release.bytes / 4096) * 4096 <= SLOT_BYTES - RELEASE_RESERVE &&
    /^[a-f0-9]{64}$/.test(release.sha256) && FILE.test(release.file),
    '这份固件尚未通过正式发行检查，安装入口保持关闭。');
  checkNotes(release.notes);
  const boot = release.bootloader, table = release.partitions;
  requireThat(boot?.offset === PLUS.bootloaderOffset && Number.isInteger(boot.bytes) && boot.bytes >= 24 &&
    boot.bytes <= PLUS.bootloaderMax && boot.bytes % 4 === 0 && /^[a-f0-9]{64}$/.test(boot.sha256) &&
    FILE.test(boot.file), '启动加载器的发行信息不合法。');
  requireThat(table?.offset === PLUS.tableOffset && [3072, 4096].includes(table.bytes) &&
    /^[a-f0-9]{64}$/.test(table.sha256) && FILE.test(table.file), '分区表的发行信息不合法。');
  requireThat(new Set([release.file, boot.file, table.file]).size === 3, '安装组件文件不能重复。');
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

// The release's table must be the RickyOS layout; returns it as the full 4 KiB sector.
export async function checkPlusTable(bytes, release) {
  checkPlusRelease(release);
  requireThat(bytes instanceof Uint8Array && bytes.length === release.partitions.bytes &&
    await sha256(bytes) === release.partitions.sha256, '分区表大小或 SHA-256 错误。');
  const sector = new Uint8Array(4096).fill(255); sector.set(bytes);
  await checkPartitions(sector);
  return sector;
}

// A fresh 8 KiB boot-record area selecting app0: sequence 1, state undefined (boots
// directly; RickyOS builds have app rollback off), the second sector erased.
export function plusBootRecord() {
  const out = new Uint8Array(PLUS.otaBytes).fill(255);
  const view = new DataView(out.buffer);
  view.setUint32(0, 1, true);
  view.setUint32(24, 0xffffffff, true);
  view.setUint32(28, otaCrc(out.subarray(0, 4)), true);
  return out;
}

function projectOf(head) {
  checkS31Header(head, '应用镜像');
  const view = new DataView(head.buffer, head.byteOffset);
  if (view.getUint32(32, true) !== 0xabcd5432) return '';
  return new TextDecoder().decode(head.subarray(80, 112)).split('\0')[0];
}

// The vendor's own boot-record rule, for telling its system apart in the install text.
function vendorActive(ota) {
  let best = 0;
  for (const at of [0, 4096]) {
    const view = new DataView(ota.buffer, ota.byteOffset + at, 32);
    const seq = view.getUint32(0, true);
    if (seq > 0 && seq < 0xffffffff && view.getUint32(28, true) === otaCrc(ota.subarray(at, at + 4))) best = Math.max(best, seq);
  }
  return best ? PLUS.vendorSlots[(best - 1) % 2] : PLUS.vendorSlots[0];
}

// table: 0x8000, ota: the RickyOS boot record at 0xe000. "upgrade" only for RickyOS on
// the RickyOS layout; the vendor system is named for the install text; all else "other".
export async function inspectPlus(table, ota, readAt) {
  requireThat(table.length === 4096 && ota.length === PLUS.otaBytes, '分区或启动记录读取不完整。');
  try {
    await checkPartitions(table);
    const offset = activeOffset(ota);
    if (projectOf(await readAt(offset, 4096)).startsWith(PLUS.rickyProjectPrefix))
      return { kind: 'upgrade', device: 'plus', table, ota, offset };
  } catch { /* not RickyOS on the RickyOS layout */ }
  if (await sha256(table) === PLUS.vendorTableSha256) {
    try {
      const vendorOta = await readAt(PLUS.vendorOtaOffset, PLUS.otaBytes);
      if (projectOf(await readAt(vendorActive(vendorOta), 4096)) === PLUS.factoryProject)
        return { kind: 'factory', device: 'plus', table, ota };
    } catch { /* not the vendor app */ }
  }
  return { kind: 'other', device: 'plus', table, ota };
}

export async function inspectPlusBackup(bytes) {
  requireThat(bytes.length === FLASH_BYTES, '完整 Flash 备份必须为 16 MB。');
  return inspectPlus(bytes.slice(PLUS.tableOffset, PLUS.tableOffset + 4096),
    bytes.slice(PLUS.otaOffset, PLUS.otaOffset + PLUS.otaBytes), async (at, size) => bytes.slice(at, at + size));
}
