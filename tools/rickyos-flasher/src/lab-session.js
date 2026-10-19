import { md5, sha256 } from 'hash-wasm';
import { FlashSession } from './session.js';
import { FLASH_BYTES, SLOT_BYTES, RELEASE_RESERVE, requireThat, checkImage, equalBytes, hex } from './policy.js';

// Explicit, separate test channel. Never turn an untested candidate into an
// approved/hardwareAccepted production release to get through the installer.
export function checkLabManifest(manifest) {
  requireThat(manifest?.schema === 1 && manifest.product === 'RickyOS' && manifest.channel === 'test' &&
    manifest.board === 'eego_a4' && manifest.approved === false && manifest.hardwareAccepted === false && manifest.chipId === 9 &&
    manifest.flashBytes === FLASH_BYTES && /^\d+\.\d+\.\d+(?:-test\.[1-9]\d*)?$/.test(manifest.version) &&
    typeof manifest.build === 'string' && /^[a-z0-9-]{1,80}$/.test(manifest.build) &&
    /^firmware\/[A-Za-z0-9._-]+\.bin$/.test(manifest.file) &&
    Number.isInteger(manifest.bytes) && manifest.bytes >= 1024 && manifest.bytes % 4 === 0 &&
    Math.ceil(manifest.bytes / 4096) * 4096 <= SLOT_BYTES - RELEASE_RESERVE &&
    /^[a-f0-9]{64}$/.test(manifest.sha256) && Array.isArray(manifest.compatibleAppElfSha256) &&
    manifest.compatibleAppElfSha256.length > 0 && manifest.compatibleAppElfSha256.length <= 8 &&
    manifest.compatibleAppElfSha256.every(value => /^[a-f0-9]{64}$/.test(value)), '测试固件信息不完整，已停止。');
  return manifest;
}

export async function checkLabFirmware(bytes, manifest) {
  checkLabManifest(manifest);
  requireThat(bytes instanceof Uint8Array && bytes.length === manifest.bytes &&
    await sha256(bytes) === manifest.sha256, '测试固件大小或摘要不匹配，禁止写入。');
  checkImage(bytes, true, manifest.version, manifest.board);
}

export class LabFlashSession extends FlashSession {
  constructor(adapter, manifest) {
    super(adapter, 'eego');
    this.offer = structuredClone(checkLabManifest(manifest));
  }

  async inspectDevice() {
    const plan = await super.inspectDevice();
    try { await this.checkExistingRickyOS(plan); }
    catch (error) { this.invalidate(); throw error; }
    return plan;
  }

  async checkExistingRickyOS(plan) {
    requireThat(plan?.kind === 'upgrade' && [0x10000, 0x650000].includes(plan.offset),
      '测试页只更新已安装 RickyOS 的 A4，请用正式安装页完成首次安装。');
    // Pin supported A4 builds by their ESP-IDF app descriptor's ELF identity.
    // Project names are build-directory names, not a reliable product identity.
    // esp_app_desc_t begins at image offset 32; its ELF SHA starts at +144.
    const head = await this.adapter.read(plan.offset, 256);
    requireThat(head.length === 256 && new DataView(head.buffer, head.byteOffset).getUint32(32, true) === 0xabcd5432,
      '未识别到有效的 RickyOS 应用，请重新检查设备。');
    requireThat(this.offer.compatibleAppElfSha256.includes(hex(head.subarray(176, 208))),
      '没有识别到本测试页支持的 A4 版本。请先用正式安装页更新到 1.2.2。');
  }

  async backupFlash(progress, cancelled) {
    const result = await super.backupFlash(progress, cancelled);
    // The legacy whole-backup classifier defaults to the Read Pico tag. Use
    // this channel's A4 header/identity check after verifying the full backup.
    try { await this.inspectDevice(); }
    catch (error) { this.invalidate(); throw error; }
    this.backup = result.bytes;
    this.backupSha = result.sha256;
    return { ...result, compatible: true };
  }

  async install(bytes, manifest, progress, confirmed = false, phase = () => {}) {
    requireThat(confirmed && this.ready && this.plan && !this.writeStarted,
      '请先连接并检查 A4，再确认安装测试版。');
    const firmware = bytes.slice(), offer = structuredClone(manifest);
    requireThat(offer.sha256 === this.offer.sha256 && offer.build === this.offer.build,
      '测试固件信息已变化，请重新连接。');
    await checkLabFirmware(firmware, offer);
    const plan = this.plan;
    await this.checkExistingRickyOS(plan);
    requireThat(equalBytes(await this.adapter.read(0x8000, 4096), plan.table) &&
      equalBytes(await this.adapter.read(0xe000, 8192), plan.ota), '启动信息已变化，请重新连接检查。');
    if (this.backup) requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(this.backup),
      '设备内容在备份后变化，请重新连接检查。');
    requireThat(this.ready && this.plan === plan, '连接已失效，请重新连接。');
    this.writeStarted = true;
    this.ready = false;
    // Same application-only path as the production web flasher. No complete
    // installation, raw-address UI, bootloader/table writes, or erase-all.
    await this.adapter.write(firmware, plan.offset, progress);
    phase('正在校验写入结果，请保持连接');
    requireThat(await this.adapter.digest(plan.offset, firmware.length) === await md5(firmware),
      '写入校验失败。请保持连接，不要重启。');
    requireThat(equalBytes(await this.adapter.read(0x8000, 4096), plan.table) &&
      equalBytes(await this.adapter.read(0xe000, 8192), plan.ota) && this.plan === plan,
      '启动信息校验失败。请保持连接，不要重启。');
    phase('校验通过，正在重启');
    return this.requestRestart();
  }
}
