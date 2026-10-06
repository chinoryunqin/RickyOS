import { md5, sha256 } from 'hash-wasm';

export const FLASH_BYTES = 0x1000000;
export const SLOT_BYTES = 0x640000;
export const RELEASE_RESERVE = 512 * 1024;
export const TAG = 'CROSSPOINT-BOARD-V1:readpico;';
const encoder = new TextEncoder();
const partitions = [
  [1, 2, 0x9000, 0x5000, 'nvs'], [1, 0, 0xe000, 0x2000, 'otadata'],
  [0, 16, 0x10000, SLOT_BYTES, 'app0'], [0, 17, 0x650000, SLOT_BYTES, 'app1'],
  [1, 130, 0xc90000, 0x360000, 'spiffs'], [1, 3, 0xff0000, 0x10000, 'coredump'],
];
// MindReset/read_pico_firmware/partitions_16M.csv, checked 2026-10-04.
// Older/unrecognised layouts require a separately verified profile, not guessing.
const factoryPartitions = [
  [1, 2, 0x9000, 0x5000, 'nvs'], [1, 1, 0xe000, 0x1000, 'phy_init'],
  [0, 0, 0x10000, 0x400000, 'factory'], [1, 129, 0x410000, 0x500000, 'storage'],
];
export function requireThat(condition, message) {
  if (!condition) throw new Error(message);
}
export function equalBytes(a, b) {
  return a.length === b.length && a.every((value, i) => value === b[i]);
}
export function hex(bytes) {
  return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
}
export function contains(bytes, text) {
  const needle = encoder.encode(text);
  outer: for (let i = 0; i <= bytes.length - needle.length; i++) {
    if (bytes[i] !== needle[0]) continue;
    for (let j = 1; j < needle.length; j++) if (bytes[i + j] !== needle[j]) continue outer;
    return true;
  }
  return false;
}
export function checkSecurity(chip, size, info, secureDownloadMode) {
  requireThat(chip === 'ESP32-S3' && size === '16MB', '设备必须是 ESP32-S3 / 16 MB。');
  requireThat(info?.chipId === 9 && Number.isInteger(info.flags) &&
    info.flashCryptCnt === 0 && info.parsedFlags?.SECURE_BOOT_EN === false &&
    info.parsedFlags?.SECURE_DOWNLOAD_ENABLE === false && secureDownloadMode === false,
    '安全启动、Flash 加密或安全下载状态不符合要求，停止。');
}
async function checkPartitionLayout(table, expected) {
  requireThat(table.length === 4096, '分区表读取不完整。');
  const view = new DataView(table.buffer, table.byteOffset, table.byteLength);
  for (const [i, entry] of expected.entries()) {
    const at = i * 32;
    const label = new TextDecoder().decode(table.subarray(at + 12, at + 28)).split('\0')[0];
    const actual = [view.getUint8(at + 2), view.getUint8(at + 3),
      view.getUint32(at + 4, true), view.getUint32(at + 8, true), label];
    requireThat(view.getUint16(at, true) === 0x50aa && view.getUint32(at + 28, true) === 0 &&
      JSON.stringify(actual) === JSON.stringify(entry),
      '设备分区与已验证布局不匹配，不能安装。');
  }
  const end = expected.length * 32;
  requireThat(table[end] === 0xeb && table[end + 1] === 0xeb &&
    table.subarray(end + 2, end + 16).every(b => b === 255) &&
    await md5(table.subarray(0, end)) === hex(table.subarray(end + 16, end + 32)) &&
    table.subarray(end + 32).every(b => b === 255), '分区表 MD5 或结束标记错误。');
}
export const checkPartitions = table => checkPartitionLayout(table, partitions);
export const checkFactoryPartitions = table => checkPartitionLayout(table, factoryPartitions);
// Equivalent to binascii.crc32(sequence_bytes, 0xffffffff), as used by
// ESP-IDF OTA records and the previously validated private installation tool.
export function otaCrc(bytes) {
  let crc = 0;
  for (const b of bytes) {
    crc ^= b;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
export function activeOffset(ota) {
  requireThat(ota.length === 8192, '启动记录读取不完整。');
  const seqs = [];
  for (const at of [0, 4096]) {
    const record = ota.subarray(at, at + 32);
    const view = new DataView(record.buffer, record.byteOffset, record.byteLength);
    const seq = view.getUint32(0, true), state = view.getUint32(24, true);
    // Like the ESP-IDF bootloader, a record with sequence 0/0xffffffff or a bad
    // CRC is not a vote. Arduino's boot_app0.bin ships such a second record
    // (sequence 0, CRC 0xffffffff), so devices never updated over the air have it.
    if (!(seq > 0 && seq < 0xffffffff && view.getUint32(28, true) === otaCrc(record.subarray(0, 4)))) continue;
    requireThat(state === 2 || state === 0xffffffff, '启动槽待验证、失效或存在回滚状态，停止。');
    seqs.push(seq);
  }
  requireThat(seqs.length > 0, '没有明确的有效启动槽。');
  return [0x10000, 0x650000][(Math.max(...seqs) - 1) % 2];
}
export function checkImage(bytes, product = false, version = '') {
  requireThat(bytes.length >= 24 && bytes[0] === 0xe9 && bytes[1] >= 1 && bytes[1] <= 16 &&
    new DataView(bytes.buffer, bytes.byteOffset).getUint16(12, true) === 9, '不是有效的 ESP32-S3 应用镜像。');
  requireThat(contains(bytes, TAG), '镜像缺少 Read Pico 机型标记。');
  // Reject conflicting board tags rather than accepting any matching substring.
  const text = new TextDecoder('latin1').decode(bytes);
  const tags = [...text.matchAll(/CROSSPOINT-BOARD-V1:([\w-]+);/g)].map(m => m[1]);
  requireThat(tags.every(tag => tag === 'readpico'), '镜像包含其他机型标记。');
  if (product) requireThat(contains(bytes, 'RickyOS') && contains(bytes, version), '固件品牌或版本不匹配。');
}
export function checkRelease(release) {
  requireThat(release?.approved === true && release.hardwareAccepted === true &&
    ['app-upgrade', 'auto-install'].includes(release.mode) && release.board === 'readpico' && release.chipId === 9 &&
    release.flashBytes === FLASH_BYTES && typeof release.version === 'string' &&
    /^\d+\.\d+\.\d+-rickyos-pico\.\d+$/.test(release.version) &&
    Number.isInteger(release.bytes) && release.bytes >= 24 && release.bytes % 4 === 0 &&
    Math.ceil(release.bytes / 4096) * 4096 <= SLOT_BYTES - RELEASE_RESERVE &&
    /^[a-f0-9]{64}$/.test(release.sha256) &&
    /^firmware\/[A-Za-z0-9._-]+\.bin$/.test(release.file),
    '这份固件尚未通过正式发行检查，安装入口保持关闭。');
  if (release.mode === 'auto-install') {
    checkFullInstall(release.fullInstall);
    requireThat(!release.fullInstall.segments.some(item => item.file === release.file), '应用和启动组件不能使用同一个文件。');
  } else requireThat(release.fullInstall === undefined, '仅更新应用的旧格式不能附带未经校验的首次安装组件。');
  return release;
}
export function checkFullInstall(bundle) {
  const specs = [['bootloader', 0, 24, 0x8000], ['partitions', 0x8000, 3072, 4096],
    ['boot_app0', 0xe000, 8192, 8192]];
  // Factory images are built from MindReset's open source, so devices need not
  // share an app digest. Eligibility is the verified factory layout plus the
  // Read_Pico app descriptor (inspectBackup) and a saved, re-verified full backup.
  requireThat(bundle?.approved === true && bundle.hardwareAccepted === true &&
    Array.isArray(bundle.segments) && bundle.segments.length === 3 &&
    Array.isArray(bundle.factoryLayouts) && bundle.factoryLayouts.length > 0 &&
    bundle.factoryLayouts.every(layout => layout === 'mindreset-factory-4m-v1') &&
    [undefined, true, false].includes(bundle.otherSystems),
    '首次安装包尚未通过硬件验收，不能迁移原厂系统。');
  for (const [i, [role, offset, min, max]] of specs.entries()) {
    const item = bundle.segments[i];
    requireThat(item?.role === role && item.offset === offset && Number.isInteger(item.bytes) &&
      item.bytes >= min && item.bytes <= max && item.bytes % 4 === 0 &&
      (role !== 'partitions' || [3072, 4096].includes(item.bytes)) &&
      /^[a-f0-9]{64}$/.test(item.sha256) && /^firmware\/[A-Za-z0-9._-]+\.bin$/.test(item.file),
      '首次安装组件的类型、位置、大小或摘要不合法。');
  }
  requireThat(new Set(bundle.segments.map(item => item.file)).size === 3, '安装组件文件不能重复。');
}
export async function checkInstallAssets(assets, release) {
  checkRelease(release);
  requireThat(release.mode === 'auto-install' && Array.isArray(assets) && assets.length === 3,
    '缺少完整的首次安装组件。');
  for (const [i, spec] of release.fullInstall.segments.entries()) {
    requireThat(assets[i] instanceof Uint8Array && assets[i].length === spec.bytes &&
      await sha256(assets[i]) === spec.sha256, `${spec.role} 组件大小或 SHA-256 错误。`);
  }
  const boot = assets[0];
  requireThat(boot[0] === 0xe9 && boot[1] >= 1 && boot[1] <= 16 &&
    new DataView(boot.buffer, boot.byteOffset).getUint16(12, true) === 9,
    '启动加载器不是 ESP32-S3 镜像。');
  const table = new Uint8Array(4096).fill(255); table.set(assets[1]);
  await checkPartitions(table);
  requireThat(activeOffset(assets[2]) === 0x10000, '首次安装启动记录必须明确选择首个应用槽。');
}
export function checkPlanRelease(plan, release) {
  checkRelease(release);
  requireThat(plan && ['upgrade', 'factory', 'other'].includes(plan.kind), '没有识别出可安装的设备。');
  if (plan.kind === 'other') {
    requireThat(release.mode === 'auto-install' && release.fullInstall.otherSystems === true,
      '该发行包不包含完整安装组件，不能替换当前系统。');
  }
  if (plan.kind === 'factory') {
    requireThat(release.mode === 'auto-install', '该发行包不包含原厂首次安装组件。');
    requireThat(release.fullInstall.factoryLayouts.includes(plan.layout),
      '识别到原厂系统，但这种分区布局尚未通过安装验收。请保存备份并联系 Ricky AI Studio。');
  }
}
export async function checkFirmware(bytes, release) {
  checkRelease(release);
  requireThat(bytes.length === release.bytes && await sha256(bytes) === release.sha256,
    '固件大小或 SHA-256 不匹配，禁止写入。');
  checkImage(bytes, true, release.version);
}
export async function inspectBackup(bytes) {
  requireThat(bytes.length === FLASH_BYTES, '完整 Flash 备份必须为 16 MB。');
  const table = bytes.slice(0x8000, 0x9000), ota = bytes.slice(0xe000, 0x10000);
  try { return await classifyBackup(bytes, table, ota); }
  catch {
    // Any other system (community firmware, a damaged install, an unknown
    // layout) gets the complete install: every region RickyOS uses is rewritten.
    return { kind: 'other', table, ota, offset: 0x10000 };
  }
}
function checkFactoryApp(app) {
  const view = new DataView(app.buffer, app.byteOffset);
  const project = new TextDecoder().decode(app.subarray(80, 112)).split('\0')[0];
  requireThat(app[0] === 0xe9 && app[1] >= 1 && app[1] <= 16 && view.getUint16(12, true) === 9 &&
    view.getUint32(28, true) >= 256 && view.getUint32(32, true) === 0xabcd5432 && project === 'Read_Pico',
    '没有识别到受支持的 Read Pico 原厂应用。');
}
// Without a backup, classify from the partition table, boot records and the
// first 4 KiB of the app that boots. The user confirms the model; a RickyOS or
// CrossMux layout gets the app-only update, the factory layout and anything
// else the complete install.
export async function inspectHeaders(table, ota, readAt) {
  try {
    if (table[66] === 0 && table[67] === 0) {
      await checkFactoryPartitions(table);
      checkFactoryApp(await readAt(0x10000, 4096));
      return { kind: 'factory', layout: 'mindreset-factory-4m-v1', table, ota, offset: 0x10000 };
    }
    await checkPartitions(table);
    const offset = activeOffset(ota), head = await readAt(offset, 4096);
    requireThat(head[0] === 0xe9 && head[1] >= 1 && head[1] <= 16 &&
      new DataView(head.buffer, head.byteOffset).getUint16(12, true) === 9, '不是有效的 ESP32-S3 应用镜像。');
    return { kind: 'upgrade', table, ota, offset };
  } catch {
    return { kind: 'other', table, ota, offset: 0x10000 };
  }
}
async function classifyBackup(bytes, table, ota) {
  // Classify by app subtype, then validate the entire table. A damaged upgrade
  // must never be treated as the factory system.
  if (table[66] === 0 && table[67] === 0) {
    await checkFactoryPartitions(table);
    const app = bytes.subarray(0x10000, 0x410000);
    checkFactoryApp(app);
    return { kind: 'factory', layout: 'mindreset-factory-4m-v1', table, ota, offset: 0x10000,
      tableSha256: await sha256(table), bootloaderSha256: await sha256(bytes.subarray(0, 0x8000)),
      appSha256: await sha256(app) };
  }
  await checkPartitions(table);
  const offset = activeOffset(ota);
  checkImage(bytes.subarray(offset, offset + SLOT_BYTES));
  return { kind: 'upgrade', table, ota, offset };
}
