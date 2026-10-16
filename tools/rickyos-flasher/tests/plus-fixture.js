import { md5 } from 'hash-wasm';
import { FLASH_BYTES, otaCrc } from '../src/policy.js';
import { PLUS, PLUS_TAG } from '../src/plus.js';

// Partition tables as ESP-IDF encodes them: the vendor's (Metalio partitions.csv) and the
// RickyOS / Read Pico layout the installer moves boards to.
const VENDOR = [
  [1, 2, 0x9000, 0x4000, 'nvs'], [1, 0, 0xd000, 0x2000, 'otadata'], [1, 1, 0xf000, 0x1000, 'phy_init'],
  [1, 130, 0x10000, 0x64000, 'model'], [0, 16, 0x80000, 0x500000, 'ota_0'], [0, 17, 0x580000, 0x500000, 'ota_1'],
  [1, 130, 0xa80000, 0x64000, 'resources'], [1, 64, 0xae4000, 0x500000, 'font_data'],
  [1, 3, 0xfe4000, 0x10000, 'coredump'],
];
const RICKY = [
  [1, 2, 0x9000, 0x5000, 'nvs'], [1, 0, 0xe000, 0x2000, 'otadata'],
  [0, 16, 0x10000, 0x640000, 'app0'], [0, 17, 0x650000, 0x640000, 'app1'],
  [1, 130, 0xc90000, 0x360000, 'spiffs'], [1, 3, 0xff0000, 0x10000, 'coredump'],
];
export const vendorTable = () => encodeTable(VENDOR);
export const rickyTable = () => encodeTable(RICKY);
async function encodeTable(ENTRIES) {
  const table = new Uint8Array(4096).fill(255), view = new DataView(table.buffer);
  ENTRIES.forEach(([type, subtype, offset, size, label], i) => {
    const at = i * 32;
    view.setUint16(at, 0x50aa, true); table[at + 2] = type; table[at + 3] = subtype;
    view.setUint32(at + 4, offset, true); view.setUint32(at + 8, size, true);
    table.fill(0, at + 12, at + 32); table.set(new TextEncoder().encode(label), at + 12);
  });
  const end = ENTRIES.length * 32;
  table[end] = 0xeb; table[end + 1] = 0xeb;
  const digest = await md5(table.subarray(0, end));
  for (let i = 0; i < 16; i++) table[end + 16 + i] = parseInt(digest.slice(i * 2, i * 2 + 2), 16);
  return table;
}
export function otaRecord(seq) { // a valid record for slot (seq - 1) % 2
  const ota = new Uint8Array(PLUS.otaBytes).fill(255);
  if (seq) {
    const view = new DataView(ota.buffer);
    view.setUint32(0, seq, true); view.setUint32(24, 2, true); view.setUint32(28, otaCrc(ota.subarray(0, 4)), true);
  }
  return ota;
}
// A minimal ESP32-S31 app image: header, app descriptor with the project name, strings.
export function plusApp(project, extra = '', length = 4096) {
  const bytes = new Uint8Array(length);
  bytes[0] = 0xe9; bytes[1] = 1; new DataView(bytes.buffer).setUint16(12, PLUS.chipId, true);
  new DataView(bytes.buffer).setUint32(32, 0xabcd5432, true);
  bytes.set(new TextEncoder().encode(project), 80);
  bytes.set(new TextEncoder().encode(extra), 512);
  return bytes;
}
export const plusFirmware = (version = '1.2.0') => plusApp('RickyOS-Plus-1.2', `RickyOS ${version} ${PLUS_TAG}`, 8192);
export function plusBootloader() {
  const bytes = new Uint8Array(19600).fill(7);
  bytes[0] = 0xe9; bytes[1] = 3; new DataView(bytes.buffer).setUint16(12, PLUS.chipId, true);
  return bytes;
}
// A factory-fresh board: vendor bootloader, the vendor table, the vendor app in ota_0.
export async function factoryFlash() {
  const flash = new Uint8Array(FLASH_BYTES).fill(255);
  flash.set(plusBootloader().fill(3, 24), PLUS.bootloaderOffset);
  flash.set(await vendorTable(), PLUS.tableOffset);
  flash.set(otaRecord(1), PLUS.vendorOtaOffset);
  flash.set(plusApp(PLUS.factoryProject, 'vendor 2.0.61'), PLUS.vendorSlots[0]);
  flash.fill(0x5a, 0x9000, 0xd000);  // vendor NVS
  flash.fill(0x44, 0xae4000, 0xae5000); // vendor font_data
  return flash;
}
// RickyOS already on the RickyOS layout, running from app0 (seq 1) or app1 (seq 2).
export async function rickyFlash(seq = 1) {
  const flash = new Uint8Array(FLASH_BYTES).fill(255);
  flash.set(plusBootloader(), PLUS.bootloaderOffset);
  flash.set(await rickyTable(), PLUS.tableOffset);
  flash.set(otaRecord(seq), PLUS.otaOffset);
  flash.set(plusFirmware('1.1.9'), seq === 2 ? 0x650000 : PLUS.appOffset);
  flash.fill(0x5a, PLUS.nvsOffset, PLUS.nvsOffset + 0x1000); // RickyOS settings
  return flash;
}
