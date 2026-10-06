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
// Firmware is several MB from GitHub Pages; show the download instead of a frozen bar.
async function fetchBytes(file, label, expected) {
  const response = await fetch(`./${file}`, { cache: 'no-store' });
  requireThat(response.ok, `${label}下载失败。`);
  if (!response.body || !expected) return new Uint8Array(await response.arrayBuffer());
  const reader = response.body.getReader(), chunks = [];
  let received = 0;
  progress(`下载${label}`, 0, expected);
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value); received += value.length;
    progress(`下载${label}`, Math.min(received, expected), expected);
  }
  const bytes = new Uint8Array(received);
  let at = 0;
  for (const chunk of chunks) { bytes.set(chunk, at); at += chunk.length; }
  return bytes;
}
function showDone(firstInstall, restarted) {
  $('done-text').textContent = (restarted ?
    'RickyOS 已写入并通过校验，设备正在重启。几秒后屏幕会出现 RickyOS 首页，之后可以拔下数据线。' :
    'RickyOS 已写入并通过校验。请长按电源键几秒重启设备，屏幕会出现 RickyOS 首页。') +
    (firstInstall ? '原系统的内部文件已清理，SD 卡未作改动。' : 'SD 卡未作改动。');
  $('done-hint').hidden = !restarted;
  $('done-dialog').showModal();
}
function step(name) {
  for (const item of ['connect', 'backup', 'install']) {
    const element = $('step-' + item);
    element.classList.toggle('active', item === name);
    if (item === name) element.setAttribute('aria-current', 'step');
    else element.removeAttribute('aria-current');
  }
  $('connect-symbol').hidden = name !== 'connect';
  const headings = { connect: '用 USB 数据线连接设备。', backup: '检查设备当前的系统。', install: '确认后开始安装。' };
  const descriptions = { connect: '在电脑 Chrome 或 Edge 中打开，关闭其他占用串口的窗口。',
    backup: '读取分区和启动信息判断安装方式；需要的话可以先备份完整 Flash。',
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
    kind === 'other' ? '已识别其他系统 · 自动准备完整安装' :
    kind === 'upgrade' ? '已识别兼容系统 · 自动准备更新' : '自动检查当前系统';
  $('install-note').textContent = kind === 'factory' ?
    '首次安装会替换系统、启动组件和分区，清理原厂内部文件。原厂设置不保证沿用；网站不操作 SD 卡，请另行备份卡上的数据。' :
    kind === 'other' ? '完整安装会替换当前系统、启动组件和分区，清理设备内部文件和设置。网站不操作 SD 卡，请另行备份卡上的数据。' :
    kind === 'upgrade' ? '只更新应用，不写启动加载器、分区表和启动记录，不操作 SD 卡。' :
    '检查后自动选择安装方式：已装有 CrossMux 或 RickyOS 的设备只更新应用，原厂或其他系统完整安装。网站不操作 SD 卡。';
  $('write-confirm-text').textContent = kind === 'factory' || kind === 'other' ?
    '我确认安装会替换当前系统并清理设备内部文件，写入期间不拔线、不让电脑休眠。' :
    '我确认安装所选版本，写入期间不拔线、不让电脑休眠。';
}
function updateControls() {
  $('connect').disabled = !connectionAllowed();
  $('model-confirm').disabled = busy || Boolean(session);
  $('disconnect').hidden = !session;
  $('disconnect').disabled = busy;
  $('backup').disabled = busy || Boolean(session?.writeStarted);
  $('backup-opt').disabled = busy;
  $('write-confirm').disabled = busy;
  $('install-button').disabled = busy || (demo ? !session?.plan?.demo :
    Boolean(session?.writeStarted) || !session?.ready || Boolean(installationBlock()) || !$('write-confirm').checked);
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
    if (session) session.ready = false;
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
  backupUrl = null; $('save-backup').hidden = true; $('write-confirm').checked = false;
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
      $('release-description').textContent = '已通过发行检查，连接设备即可安装。';
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
$('backup-opt').addEventListener('change', () => {
  if (!demo) $('backup').textContent = $('backup-opt').checked ? '备份并检查设备' : '检查设备';
});
$('connect').addEventListener('click', () => {
  if (!connectionAllowed()) return;
  // Request permission while the click's transient user activation is still live.
  const selectedPort = !demo ? navigator.serial.requestPort() : null;
  // Handle immediate cancellation even while the wake-lock request is pending.
  selectedPort?.catch(() => {});
  return operate(async () => {
  clearBackup();
  if (demo) {
    session = { ready: false, plan: null };
    $('device-state').textContent = '演示设备';
    $('backup').textContent = '演示：检查设备';
    result('演示第 1 步：机型和容量检查通过。未连接真实串口。');
  } else {
    requireThat(release, '正式固件尚未发布，设备连接暂未开放。');
    requireThat(supported && hashReady && $('model-confirm').checked, '请先确认 Read Pico 机型和浏览器校验能力。');
    const port = await selectedPort;
    // ?trace=1 logs raw serial traffic to the browser console for diagnosis.
    adapter = new SerialAdapter(log, new URLSearchParams(location.search).has('trace'));
    try {
      await adapter.connect(port); session = new FlashSession(adapter);
    } catch (error) {
      await adapter.close().catch(() => {}); adapter = null; throw error;
    }
    $('device-state').textContent = '芯片检查通过';
    result('ESP32-S3 / 16 MB 检查通过。接下来检查设备当前的系统。');
  }
  $('backup-controls').hidden = false; step('backup');
  });
});
$('disconnect').addEventListener('click', () => operate(async () => {
  await adapter?.close(); adapter = null; session = null; clearBackup();
  $('backup-controls').hidden = true; $('progress-region').hidden = true;
  $('device-state').textContent = '未连接'; step('connect'); result('已断开连接。再次安装需要重新连接并检查设备。');
}));
$('backup').addEventListener('click', () => operate(async () => {
  clearBackup();
  if (demo) {
    progress('演示 · 检查设备', 100, 100);
    session.ready = true; session.plan = { demo: true, kind: 'factory' }; showPlan('factory');
    $('final-controls').hidden = false; $('write-confirm').hidden = true;
    $('final-controls').querySelector('.check-row').hidden = true;
    $('install-button').textContent = '演示：安装与校验 →';
    $('install-button').disabled = false;
    result('演示第 2 步：自动识别原厂系统，准备首次安装。真实流程会读取分区布局和应用机型，不需要先刷其他系统。这里没有读取任何设备数据。');
    step('install'); return;
  }
  requireThat(session, '请先连接设备。');
  if ($('backup-opt').checked) {
    cancelled = false; $('cancel').hidden = false; $('cancel').disabled = false;
    const backup = await session.backupFlash((done, total) => progress('读取完整 Flash 备份', done, total), () => cancelled);
    backupUrl = URL.createObjectURL(new Blob([backup.bytes]));
    $('save-backup').href = backupUrl;
    $('save-backup').download = `RickyOS-ReadPico-backup-${new Date().toISOString().replace(/[:.]/g, '-')}.bin`;
    $('save-backup').hidden = false; $('save-backup').click();
    $('backup-help').textContent = `完整 16 MB 备份已下载，并通过设备摘要校验。SHA-256：${backup.sha256}`;
  } else {
    progress('检查分区与启动信息', 0, 1);
    await session.inspectDevice();
    progress('检查分区与启动信息', 1, 1);
  }
  showPlan(session.plan?.kind);
  $('final-controls').hidden = false; step('install');
  const blocked = installationBlock();
  result(blocked ? `已识别当前系统。${blocked}` : '已识别当前系统。请阅读安装说明，确认下方选项后安装。');
}));
$('cancel').addEventListener('click', () => { cancelled = true; $('cancel').disabled = true; });
$('install-button').addEventListener('click', () => operate(async () => {
  if (demo) {
    progress('演示 · 写入与摘要校验', 100, 100);
    $('stage-heading').textContent = '流程演示结束，未执行真实安装。';
    result('演示结束：真实刷写完成后会请求设备重启，并请用户确认首页。没有执行任何真实 Flash 写入。');
    return;
  }
  requireThat(release && session && $('write-confirm').checked, '未满足安装条件。');
  checkPlanRelease(session.plan, release);
  result('正在下载固件。请勿拔线或让电脑休眠。');
  const bytes = await fetchBytes(release.file, '固件', release.bytes);
  const firstInstall = session.plan.kind === 'factory' || session.plan.kind === 'other';
  const assets = [];
  if (firstInstall) for (const spec of release.fullInstall.segments)
    assets.push(await fetchBytes(spec.file, `${spec.role} 组件`, spec.bytes));
  result('正在写入设备，需要几分钟。请勿拔线或让电脑休眠。');
  const restarted = await session.install(bytes, release,
    (done, total) => progress(firstInstall ? '安装 RickyOS 与准备内部空间' : '写入应用分区', done, total), true,
    label => { $('operation').textContent = label; $('result').textContent = label; log(label); }, assets);
  progress('安装完成', 100, 100);
  result(restarted ? 'RickyOS 安装完成并通过校验，设备正在重启。' :
    'RickyOS 安装完成并通过校验。请长按电源键几秒重启设备。');
  await adapter.close().catch(() => {}); adapter = null; session = null; clearBackup();
  $('backup-controls').hidden = true; $('device-state').textContent = restarted ? '正在重启' : '请手动重启';
  $('stage-heading').textContent = '安装完成，设备会出现 RickyOS 首页。';
  showDone(firstInstall, restarted);
}));
$('done-close').addEventListener('click', () => $('done-dialog').close());
$('save-log').addEventListener('click', () => download(logs.join('\n'), 'RickyOS-install-log.txt', 'text/plain;charset=utf-8'));
addEventListener('beforeunload', event => { if (busy) { event.preventDefault(); event.returnValue = ''; } });
if (navigator.serial) navigator.serial.addEventListener('disconnect', event => {
  if (event.target !== adapter?.transport?.device) return;
  // The board leaves USB as it restarts after a verified install; that is not an error.
  if (session?.restarting) return;
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
