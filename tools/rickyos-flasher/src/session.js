import { md5, sha256 } from 'hash-wasm';
import { FLASH_BYTES, inspectBackup, inspectHeaders, requireThat, equalBytes, checkFirmware,
  checkInstallAssets, checkPlanRelease } from './policy.js';

function sectorPad(bytes) {
  const padded = new Uint8Array(Math.ceil(bytes.length / 4096) * 4096).fill(255);
  padded.set(bytes); return padded;
}

export class FlashSession {
  constructor(adapter) { this.adapter = adapter; this.writeStarted = false; this.invalidate(); }
  invalidate() { this.backup = null; this.ready = false; this.plan = null; }
  // The quick path: read only the partition table, boot records and app headers
  // (a few KiB) to decide how to install. No backup is taken.
  async inspectDevice() {
    this.invalidate();
    const table = await this.adapter.read(0x8000, 4096), ota = await this.adapter.read(0xe000, 8192);
    this.plan = await inspectHeaders(table, ota, (at, size) => this.adapter.read(at, size));
    this.ready = true;
    return this.plan;
  }
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
    // Recognised factory layouts are eligible only when the release accepts that
    // layout. Unknown layouts remain backup-only.
    try { this.plan = await inspectBackup(bytes); this.compatibilityError = null; }
    catch (error) { this.compatibilityError = error.message; }
    this.ready = Boolean(this.plan);
    return { bytes, sha256: this.backupSha, compatible: Boolean(this.plan) };
  }
  async install(bytes, release, progress, confirmed = false, phase = () => {}, assets = []) {
    requireThat(!this.writeStarted && this.plan && this.ready && confirmed,
      '请重新连接并检查设备，再确认安装。');
    const firmware = bytes.slice(); // Do not permit caller mutation across awaits.
    release = structuredClone(release);
    const plan = this.plan, backup = this.backup;
    await checkFirmware(firmware, release);
    checkPlanRelease(plan, release);
    const installAssets = assets.map(item => item.slice());
    const fullInstall = plan.kind === 'factory' || plan.kind === 'other';
    if (fullInstall) await checkInstallAssets(installAssets, release);
    const liveTable = await this.adapter.read(0x8000, 4096);
    const liveOta = await this.adapter.read(0xe000, 8192);
    requireThat(equalBytes(liveTable, plan.table) && equalBytes(liveOta, plan.ota),
      '设备启动信息已变化，禁止写入。请重新备份。');
    if (backup) requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(backup),
      '设备内容在备份后发生变化，请重新连接并备份。');
    requireThat(this.plan === plan && this.ready, '连接或安装资格已失效。');
    this.writeStarted = true; // Latch before erase/write; failures require a fresh session.
    this.ready = false;
    if (fullInstall) {
      return this.installFactory(firmware, release, installAssets, backup, plan, progress, phase);
    }
    await this.adapter.write(firmware, plan.offset, progress);
    phase('正在校验固件与启动信息，请保持连接');
    requireThat(await this.adapter.digest(plan.offset, firmware.length) === await md5(firmware),
      '写入后的固件校验失败。保持连接，不要重启。');
    requireThat(equalBytes(await this.adapter.read(0x8000, 4096), plan.table) &&
      equalBytes(await this.adapter.read(0xe000, 8192), plan.ota) && this.plan === plan,
      '启动元数据校验失败。保持连接，不要重启。');
    phase('校验通过，正在请求设备重启');
    return this.requestRestart();
  }
  // Everything is written and verified by now. The board leaves USB as it restarts, so
  // a failed request is not an install failure: the user restarts it by hand instead.
  async requestRestart() {
    this.restarting = true;
    try { await this.adapter.reset(); return true; } catch { return false; }
  }
  async installFactory(firmware, release, assets, backup, plan, progress, phase) {
    // Browser-only buffers and a reused 64 KiB erased-sector block; no
    // allocations on the ESP32. Never erase-all: retain NVS and all bytes outside
    // explicitly approved sector ranges. Every write is checked by the chip's own
    // digest; with a backup the whole flash is also checked against the expected image.
    const app = sectorPad(firmware), blank = new Uint8Array(65536).fill(255);
    const tail = 0x10000 + app.length;
    // Another system's bytes at 0x9000 are not RickyOS settings; start NVS empty.
    // The factory NVS is kept, as before.
    const clearNvs = plan.kind === 'other';
    const components = assets.map((bytes, i) => ({ bytes: sectorPad(bytes), spec: release.fullInstall.segments[i] }));
    const expected = backup?.slice();
    if (expected) {
      expected.set(app, 0x10000); expected.fill(255, tail);
      if (clearNvs) expected.fill(255, 0x9000, 0xe000);
      for (const item of components) expected.set(item.bytes, item.spec.offset);
    }
    const total = FLASH_BYTES - 0x10000 + (clearNvs ? 0x5000 : 0) +
      components.reduce((sum, item) => sum + item.bytes.length, 0);
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
    if (clearNvs) await writeChecked(blank.subarray(0, 0x5000), 0x9000);
    phase('正在校验并更新启动组件，请保持连接');
    await writeChecked(components[0].bytes, components[0].spec.offset);
    await writeChecked(components[2].bytes, components[2].spec.offset);
    // Verify staging and NVS before committing the partition table last.
    if (expected) {
      const beforeCommit = expected.slice(); beforeCommit.set(plan.table, 0x8000);
      requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(beforeCommit) && this.plan === plan,
        '首次安装准备校验失败。未提交新分区，不要重启。');
    }
    await writeChecked(components[1].bytes, components[1].spec.offset);
    if (expected) {
      phase('正在校验完整安装结果');
      requireThat(await this.adapter.digest(0, FLASH_BYTES) === await md5(expected) && this.plan === plan,
        '完整安装摘要不一致，不要重启。');
    }
    phase('校验通过，正在请求设备重启');
    return this.requestRestart();
  }
}
