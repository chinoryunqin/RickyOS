import { md5, sha256 } from 'hash-wasm';
import { FLASH_BYTES, inspectBackup, requireThat, equalBytes, checkFirmware,
  checkInstallAssets, checkPlanRelease } from './policy.js';

function sectorPad(bytes) {
  const padded = new Uint8Array(Math.ceil(bytes.length / 4096) * 4096).fill(255);
  padded.set(bytes); return padded;
}

export class FlashSession {
  constructor(adapter) { this.adapter = adapter; this.writeStarted = false; this.invalidate(); }
  invalidate() { this.backup = null; this.savedBackupVerified = false; this.plan = null; }
  async backupFlash(progress, cancelled = () => false) {
    this.invalidate();
    // This is browser memory, not ESP heap. A fixed 16 MB buffer plus 64 KiB
    // reads avoids esptool-js's repeated concatenation of a single 16 MB read.
    const bytes = new Uint8Array(FLASH_BYTES);
    for (let at = 0; at < FLASH_BYTES; at += 65536) {
      requireThat(!cancelled(), '已取消备份，没有执行 Flash 写入。');
      const chunk = await this.adapter.read(at, 65536, done => progress(at + done, FLASH_BYTES));
      requireThat(chunk.length === 65536, '备份读取中断。');
      bytes.set(chunk, at);
    }
    requireThat(!cancelled(), '已取消备份，没有执行 Flash 写入。');
    requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(bytes), '完整备份与设备摘要不一致。');
    this.backup = bytes;
    this.backupSha = await sha256(bytes);
    // Recognised factory layouts are eligible only with a matching, accepted
    // source fingerprint in the release. Unknown layouts remain backup-only.
    try { this.plan = await inspectBackup(bytes); this.compatibilityError = null; }
    catch (error) { this.compatibilityError = error.message; }
    return { bytes, sha256: this.backupSha, compatible: Boolean(this.plan) };
  }
  async verifySavedBackup(bytes) {
    this.savedBackupVerified = false;
    requireThat(this.backup && bytes.length === FLASH_BYTES && await sha256(bytes) === this.backupSha,
      '所选文件不是本次完整备份。请保存下载的 16 MB 文件后重新选择。');
    this.savedBackupVerified = true;
  }
  async install(bytes, release, progress, confirmed = false, phase = () => {}, assets = []) {
    requireThat(!this.writeStarted && this.backup && this.plan && this.savedBackupVerified && confirmed,
      '必须重新连接、完成完整备份并校验已保存文件，且确认安装。');
    const firmware = bytes.slice(); // Do not permit caller mutation across awaits.
    release = structuredClone(release);
    const plan = this.plan, backup = this.backup;
    await checkFirmware(firmware, release);
    checkPlanRelease(plan, release);
    const installAssets = assets.map(item => item.slice());
    if (plan.kind === 'factory') await checkInstallAssets(installAssets, release);
    const liveTable = await this.adapter.read(0x8000, 4096);
    const liveOta = await this.adapter.read(0xe000, 8192);
    requireThat(equalBytes(liveTable, plan.table) && equalBytes(liveOta, plan.ota),
      '设备启动信息已变化，禁止写入。请重新备份。');
    requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(backup),
      '设备内容在备份后发生变化，请重新连接并备份。');
    requireThat(this.plan === plan && this.savedBackupVerified, '连接或安装资格已失效。');
    this.writeStarted = true; // Latch before erase/write; failures require a fresh session.
    this.savedBackupVerified = false;
    if (plan.kind === 'factory') {
      await this.installFactory(firmware, release, installAssets, backup, plan, progress, phase);
      return;
    }
    await this.adapter.write(firmware, plan.offset, progress);
    phase('正在校验固件与启动信息，请保持连接');
    requireThat(await this.adapter.digest(plan.offset, firmware.length) === await md5(firmware),
      '写入后的固件校验失败。保持连接，不要重启。');
    requireThat(equalBytes(await this.adapter.read(0x8000, 4096), plan.table) &&
      equalBytes(await this.adapter.read(0xe000, 8192), plan.ota) && this.plan === plan,
      '启动元数据校验失败。保持连接，不要重启。');
    phase('校验通过，正在请求设备重启');
    await this.adapter.reset();
  }
  async installFactory(firmware, release, assets, backup, plan, progress, phase) {
    // Browser-only fixed buffers: one expected 16 MB flash snapshot and a reused
    // 64 KiB erased-sector block. No allocations on the ESP32. Never erase-all:
    // retain NVS and all bytes outside explicitly approved sector ranges.
    const expected = backup.slice(), app = sectorPad(firmware), blank = new Uint8Array(65536).fill(255);
    const tail = 0x10000 + app.length;
    expected.set(app, 0x10000); expected.fill(255, tail);
    const components = assets.map((bytes, i) => ({ bytes: sectorPad(bytes), spec: release.fullInstall.segments[i] }));
    for (const item of components) expected.set(item.bytes, item.spec.offset);
    const total = FLASH_BYTES - 0x10000 + components.reduce((sum, item) => sum + item.bytes.length, 0);
    let done = 0;
    const writeChecked = async (bytes, at) => {
      requireThat(this.plan === plan, '连接已失效。停止安装，不要重启。');
      await this.adapter.write(bytes, at, count => progress(done + count, total));
      requireThat(await this.adapter.digest(at, bytes.length) === await md5(bytes) && this.plan === plan,
        '首次安装组件校验失败。保持 BOOT 模式，不要重启。');
      done += bytes.length; progress(done, total);
    };
    phase('正在安装 RickyOS，准备设备内部空间');
    await writeChecked(app, 0x10000);
    // Clear old factory FAT content and stale app1/FS/coredump data. Otherwise
    // the new layout could interpret old internal data as a valid filesystem.
    for (let at = tail; at < FLASH_BYTES; at += blank.length)
      await writeChecked(blank.subarray(0, Math.min(blank.length, FLASH_BYTES - at)), at);
    phase('正在校验并更新启动组件，请保持连接');
    await writeChecked(components[0].bytes, components[0].spec.offset);
    await writeChecked(components[2].bytes, components[2].spec.offset);
    // Verify staging and NVS before committing the partition table last.
    const beforeCommit = expected.slice(); beforeCommit.set(plan.table, 0x8000);
    requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(beforeCommit) && this.plan === plan,
      '首次安装准备校验失败。未提交新分区，不要重启。');
    await writeChecked(components[1].bytes, components[1].spec.offset);
    phase('正在校验完整安装结果');
    requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(expected) && this.plan === plan,
      '完整安装摘要不一致，不要重启。');
    phase('校验通过，正在请求设备重启');
    await this.adapter.reset();
  }
}
