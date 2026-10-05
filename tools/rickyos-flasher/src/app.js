import './site.js';
import { SerialAdapter } from './serial.js';
import { FlashSession } from './session.js';
import { checkRelease, checkPlanRelease, FLASH_BYTES, requireThat } from './policy.js';
import { canConnect } from './connection-gate.js';
import { md5, sha256 } from 'hash-wasm';

const $ = id => document.getElementById(id);
const demo = new URLSearchParams(location.search).get('demo') === '1';
let release = null, session = null, adapter = null, busy = false, cancelled = false;
let backupUrl = null, wakeLock = null;
let hashReady = false;
const logs = [];
function log(message) {
  logs.push(String(message));
  if (logs.length > 300) logs.shift();
  $('log').textContent = logs.join('\n');
}
function result(message, error = false) {
  $('result').textContent = message; $('result').classList.toggle('error', error); log(message);
}
function progress(label, done, total) {
  $('progress-region').hidden = false;
  const percentage = Math.min(100, Math.floor(done / total * 100));
  $('operation').textContent = label; $('percent').textContent = `${percentage}%`;
  $('progress').value = percentage;
}
function step(name) {
  for (const item of ['connect', 'backup', 'install']) {
    const element = $('step-' + item);
    element.classList.toggle('active', item === name);
    if (item === name) element.setAttribute('aria-current', 'step');
    else element.removeAttribute('aria-current');
  }
  $('connect-symbol').hidden = name !== 'connect';
  const headings = { connect: '用 USB 数据线连接设备。', backup: '先把备份留在电脑上。', install: '备份校验后，再开始安装。' };
  const descriptions = { connect: '在电脑 Chrome 或 Edge 中打开，关闭其他占用串口的窗口。',
    backup: '完整读取 16 MB Flash，下载后重新选择已保存文件校验。',
    install: '确认安装说明，写入期间保持连接，不让电脑休眠。' };
  $('stage-heading').textContent = headings[name];
  $('stage-description').textContent = descriptions[name];
}
function installationBlock() {
  if (!release) return '正式安装包尚未开放，当前不能安装。';
  try { checkPlanRelease(session?.plan, release); return ''; }
  catch (error) { return error.message; }
}
function showPlan(kind) {
  $('install-path').textContent = kind === 'factory' ? '已识别原厂系统 · 自动准备首次安装' :
    kind === 'upgrade' ? '已识别兼容系统 · 自动准备更新' : '自动检查当前系统';
  $('install-note').textContent = kind === 'factory' ?
    '首次安装会替换系统、启动组件和分区，清理原厂内部文件。原厂设置不保证沿用；网站不操作 SD 卡，请另行备份卡上的数据。' :
    kind === 'upgrade' ? '只更新应用，不写启动加载器、分区表和启动记录，不操作 SD 卡。' :
    '完成备份后会检查当前系统。已装有 CrossMux 或 RickyOS 的设备只更新应用；原厂系统的首次安装还在验收，暂未开放。请另行备份 SD 卡数据。';
  $('write-confirm-text').textContent = kind === 'factory' ?
    '完整 Flash 备份已保存并校验；SD 卡数据已另行备份。我确认首次安装会替换原厂系统并清理内部文件，写入期间不拔线、不让电脑休眠。' :
    '完整备份已保存；我确认安装所选版本，写入期间不拔线、不让电脑休眠。';
}
function updateControls() {
  $('connect').disabled = !connectionAllowed();
  $('model-confirm').disabled = busy || Boolean(session);
  $('disconnect').hidden = !session;
  $('disconnect').disabled = busy;
  $('backup').disabled = busy || Boolean(session?.writeStarted);
  $('saved-backup').disabled = busy;
  $('write-confirm').disabled = busy;
  $('install-button').disabled = busy || (demo ? !session?.plan?.demo :
    Boolean(session?.writeStarted) || !session?.savedBackupVerified || Boolean(installationBlock()) || !$('write-confirm').checked);
}
function connectionAllowed() {
  return canConnect({ demo, release, supported, hashReady,
    confirmed: $('model-confirm').checked, busy, connected: Boolean(session) });
}
async function operate(action) {
  if (busy) return;
  busy = true; updateControls();
  try {
    try { wakeLock = await navigator.wakeLock?.request('screen'); } catch { /* User must keep computer awake. */ }
    await action();
  } catch (error) {
    const afterWrite = session?.writeStarted;
    if (session) session.savedBackupVerified = false;
    result(`${error.name === 'NotFoundError' ? '未选择串口。' : error.message} ${afterWrite ?
      '写入可能已经开始。请保持 BOOT 模式，保存记录，不要拔线或重启。' : '没有执行 Flash 写入。'}`, true);
  } finally {
    await wakeLock?.release().catch(() => {}); wakeLock = null;
    busy = false; $('cancel').hidden = true; updateControls();
  }
}
function download(bytes, name, type = 'application/octet-stream') {
  const url = URL.createObjectURL(new Blob([bytes], { type }));
  const link = document.createElement('a'); link.href = url; link.download = name; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}
function clearBackup() {
  if (backupUrl) URL.revokeObjectURL(backupUrl);
  backupUrl = null; $('save-backup').hidden = true; $('saved-backup-control').hidden = true;
  $('saved-backup').value = ''; $('write-confirm').checked = false;
  $('final-controls').hidden = true;
  showPlan(null);
}
const supported = isSecureContext && 'serial' in navigator;
if (demo) {
  $('demo-banner').hidden = false; $('demo-link').hidden = true;
  $('compatibility').textContent = '仅演示页面步骤。这里的进度不会触发任何真实刷写。';
  $('connect').textContent = '演示：连接设备 →';
} else {
  $('compatibility').textContent = !isSecureContext ? '请使用 HTTPS 地址，或电脑上的 localhost 本地预览。' :
    !supported ? '当前浏览器不支持 USB 串口。请在电脑 Chrome 或 Edge 中打开此页面。' :
      '浏览器支持 USB 串口。连接会使设备进入下载模式，但不会写入 Flash。';
  $('compatibility').classList.toggle('success', supported);
  $('connect').textContent = '正式固件待发布';
}
async function loadReleases() {
  try {
    const response = await fetch('./releases.json', { cache: 'no-store' });
    requireThat(response.ok, '无法获取发行目录。');
    const catalog = await response.json();
    requireThat(catalog.schema === 1 && catalog.product === 'RickyOS' && Array.isArray(catalog.releases) &&
      catalog.releases.length <= 1, '发行目录格式错误。');
    if (catalog.releases.length) release = checkRelease(catalog.releases[0]);
    $('release-tag').textContent = release ? '正式版本可用' : '正式固件 · 待开放';
    if (release) {
      $('release-version').textContent = release.version;
      $('release-description').textContent = '已通过发行检查。完整备份并校验保存文件后，才能安装。';
      if (!demo) $('connect').textContent = '连接并检查设备 →';
    } else if (!demo) {
      $('compatibility').textContent = '网站已开放预览。正式固件发布前，不连接设备、不读取备份、不执行刷写。';
      $('compatibility').classList.remove('success');
      $('stage-heading').textContent = '正式固件，敬请期待。';
      $('stage-description').textContent = '可以先了解系统界面，或体验安全的流程演示。';
    }
  } catch (error) {
    release = null; $('release-tag').textContent = '发行检查未通过'; result(error.message, true);
  }
  updateControls();
}
$('model-confirm').addEventListener('change', updateControls);
$('write-confirm').addEventListener('change', updateControls);
$('connect').addEventListener('click', () => {
  if (!connectionAllowed()) return;
  // Request permission while the click's transient user activation is still live.
  const selectedPort = !demo ? navigator.serial.requestPort() : null;
  // Handle immediate cancellation even while the wake-lock request is pending.
  selectedPort?.catch(() => {});
  return operate(async () => {
  clearBackup();
  if (demo) {
    session = { savedBackupVerified: false, plan: null };
    $('device-state').textContent = '演示设备';
    $('backup').textContent = '演示：保存完整备份';
    result('演示第 1 步：机型和容量检查通过。未连接真实串口。');
  } else {
    requireThat(release, '正式固件尚未发布，设备连接暂未开放。');
    requireThat(supported && hashReady && $('model-confirm').checked, '请先确认 Read Pico 机型和浏览器校验能力。');
    const port = await selectedPort;
    adapter = new SerialAdapter(log);
    try {
      await adapter.connect(port); session = new FlashSession(adapter);
    } catch (error) {
      await adapter.close().catch(() => {}); adapter = null; throw error;
    }
    $('device-state').textContent = '芯片检查通过';
    result('ESP32-S3 / 16 MB 检查通过。完成备份后继续校验分区与应用机型。');
  }
  $('backup-controls').hidden = false; step('backup');
  });
});
$('disconnect').addEventListener('click', () => operate(async () => {
  await adapter?.close(); adapter = null; session = null; clearBackup();
  $('backup-controls').hidden = true; $('progress-region').hidden = true;
  $('device-state').textContent = '未连接'; step('connect'); result('已断开连接。再次安装需要重新备份。');
}));
$('backup').addEventListener('click', () => operate(async () => {
  clearBackup();
  if (demo) {
    progress('演示 · 完整备份流程', 100, 100);
    session.savedBackupVerified = true; session.plan = { demo: true, kind: 'factory' }; showPlan('factory');
    $('final-controls').hidden = false; $('write-confirm').hidden = true;
    $('final-controls').querySelector('.check-row').hidden = true;
    $('install-button').textContent = '演示：安装与校验 →';
    $('install-button').disabled = false;
    result('演示第 2 步：自动识别原厂系统，准备首次安装。真实流程会校验完整备份和原厂版本指纹，不需要先刷其他系统。这里没有读取任何设备数据。');
    step('install'); return;
  }
  requireThat(session, '请先连接设备。');
  cancelled = false; $('cancel').hidden = false; $('cancel').disabled = false;
  const backup = await session.backupFlash((done, total) => progress('读取完整 Flash 备份', done, total), () => cancelled);
  backupUrl = URL.createObjectURL(new Blob([backup.bytes]));
  $('save-backup').href = backupUrl;
  $('save-backup').download = `RickyOS-ReadPico-backup-${new Date().toISOString().replace(/[:.]/g, '-')}.bin`;
  $('save-backup').hidden = false; $('saved-backup-control').hidden = false;
  $('backup-help').textContent = `完整 16 MB 备份已通过设备摘要校验。SHA-256：${backup.sha256}`;
  showPlan(session.plan?.kind);
  result(backup.compatible ? '备份完成，已识别当前系统和分区。请点击保存，再选择刚保存的文件校验；安装还需通过发行包检查。' :
    `备份完成，请保存文件。${session.compatibilityError} 备份成功不等于允许安装。`);
}));
$('cancel').addEventListener('click', () => { cancelled = true; $('cancel').disabled = true; });
$('saved-backup').addEventListener('change', () => operate(async () => {
  const file = $('saved-backup').files[0]; requireThat(file && file.size === FLASH_BYTES, '请选择刚保存的 16 MB 备份文件。');
  await session.verifySavedBackup(new Uint8Array(await file.arrayBuffer()));
  $('final-controls').hidden = false; step('install');
  const blocked = installationBlock();
  result(blocked ? `备份文件已保存并校验。${blocked}` : '备份文件已保存并校验。请阅读安装说明，确认下方选项后可安装。');
}));
$('install-button').addEventListener('click', () => operate(async () => {
  if (demo) {
    progress('演示 · 写入与摘要校验', 100, 100);
    $('stage-heading').textContent = '流程演示结束，未执行真实安装。';
    result('演示结束：真实刷写完成后会请求设备重启，并请用户确认首页。没有执行任何真实 Flash 写入。');
    return;
  }
  requireThat(release && session && $('write-confirm').checked, '未满足安装条件。');
  checkPlanRelease(session.plan, release);
  result('正在下载并校验固件。请勿拔线或让电脑休眠。');
  const response = await fetch(`./${release.file}`, { cache: 'no-store' });
  requireThat(response.ok, '固件下载失败。');
  const bytes = new Uint8Array(await response.arrayBuffer());
  const firstInstall = session.plan.kind === 'factory';
  const assets = [];
  if (firstInstall) for (const spec of release.fullInstall.segments) {
    const part = await fetch(`./${spec.file}`, { cache: 'no-store' });
    requireThat(part.ok, `${spec.role} 组件下载失败。`);
    assets.push(new Uint8Array(await part.arrayBuffer()));
  }
  await session.install(bytes, release, (done, total) => progress(firstInstall ? '安装 RickyOS 与准备内部空间' : '写入应用分区', done, total), true,
    label => { $('operation').textContent = label; log(label); }, assets);
  progress('固件与启动信息校验完成', 100, 100);
  result(firstInstall ? 'RickyOS 首次安装与完整 Flash 摘要校验通过，已请求重启。请确认 RickyOS 首页与 SD 卡文件。原厂内部文件已清理；SD 卡未操作，请保管好原厂备份。' :
    'RickyOS 已写入并通过设备摘要校验，已请求重启。请确认屏幕出现 RickyOS 首页。SD 卡、启动加载器、分区表和启动记录未作写入。');
  await adapter.close(); adapter = null; session = null; clearBackup();
  $('backup-controls').hidden = true; $('device-state').textContent = '已请求重启';
  $('stage-heading').textContent = '校验通过，请确认设备出现 RickyOS 首页。';
}));
$('save-log').addEventListener('click', () => download(logs.join('\n'), 'RickyOS-install-log.txt', 'text/plain;charset=utf-8'));
addEventListener('beforeunload', event => { if (busy) { event.preventDefault(); event.returnValue = ''; } });
if (navigator.serial) navigator.serial.addEventListener('disconnect', event => {
  if (event.target !== adapter?.transport?.device) return;
  session?.invalidate?.(); updateControls();
  if (session) result('设备连接已断开。安装资格已失效，请重新连接并备份。', true);
});
try {
  const empty = new Uint8Array();
  requireThat(await md5(empty) === 'd41d8cd98f00b204e9800998ecf8427e' &&
    await sha256(empty) === 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855', '本地摘要校验能力异常。');
  hashReady = true;
  log('本地 MD5 / SHA-256 校验能力已就绪。尚未连接设备。');
} catch (error) {
  result('浏览器无法执行本地校验，请更换电脑 Chrome / Edge。没有连接设备或执行写入。', true);
}
await loadReleases();
