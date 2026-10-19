import './lab.css';
import { md5, sha256 } from 'hash-wasm';
import { SerialAdapter } from './serial.js';
import { LabFlashSession, checkLabManifest, checkLabFirmware } from './lab-session.js';
import { requireThat } from './policy.js';

const $ = id => document.getElementById(id);
let manifest = null, adapter = null, session = null, busy = false, wakeLock = null;
let backupUrl = null;
const logs = [];
function log(message) {
  logs.push(String(message));
  if (logs.length > 300) logs.shift();
  $('log').textContent = logs.join('\n');
}
function result(message, error = false) {
  $('result').textContent = message;
  $('result').classList.toggle('error', error);
  log(message);
}
function controls() {
  $('model').disabled = busy || Boolean(adapter);
  $('connect').disabled = busy || Boolean(adapter) || !manifest || !$('model').checked || !navigator.serial;
  $('disconnect').disabled = busy || !adapter;
  $('backup').disabled = busy || !session?.ready || session?.writeStarted;
  $('confirm').disabled = busy || !session?.ready;
  $('install').disabled = busy || !session?.ready || session?.writeStarted || !$('confirm').checked;
}
function progress(label, done, total) {
  $('progress-region').hidden = false;
  $('operation').textContent = label;
  const percent = total > 0 ? Math.min(100, Math.floor(done / total * 100)) : 0;
  $('progress').value = percent; $('percent').textContent = `${percent}%`;
}
async function operate(action) {
  if (busy) return;
  busy = true; controls();
  try {
    try { wakeLock = await navigator.wakeLock?.request('screen'); } catch {}
    await action();
  } catch (error) {
    session?.invalidate();
    result(error.message + (session?.writeStarted ? ' 本次写入未确认完成，请保持连接。' : ''), true);
  } finally {
    await wakeLock?.release().catch(() => {}); wakeLock = null;
    busy = false; controls();
  }
}
function download(data, name, type) {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const link = document.createElement('a'); link.href = url; link.download = name; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}
async function fetchFirmware() {
  const response = await fetch(`./${manifest.file}`, { cache: 'no-store' });
  requireThat(response.ok, '测试固件下载失败，请稍后重试。');
  const reader = response.body?.getReader();
  const bytes = new Uint8Array(manifest.bytes);
  let done = 0;
  if (reader) {
    while (true) {
      const chunk = await reader.read();
      if (chunk.done) break;
      requireThat(done + chunk.value.length <= bytes.length, '下载文件超出预期大小。');
      bytes.set(chunk.value, done); done += chunk.value.length;
      progress('下载测试固件', done, bytes.length);
    }
  } else {
    const data = new Uint8Array(await response.arrayBuffer());
    requireThat(data.length === bytes.length, '下载文件长度错误。');
    bytes.set(data); done = data.length;
  }
  requireThat(done === bytes.length, '测试固件下载不完整。');
  await checkLabFirmware(bytes, manifest);
  return bytes;
}
$('model').addEventListener('change', controls);
$('confirm').addEventListener('change', controls);
$('connect').addEventListener('click', () => {
  if ($('connect').disabled) return;
  const port = navigator.serial.requestPort(); // Keep the chooser in the user's click.
  port.catch(() => {});
  operate(async () => {
    adapter = new SerialAdapter(log);
    try {
      await adapter.connect(await port);
      session = new LabFlashSession(adapter, manifest);
      await session.inspectDevice();
    } catch (error) {
      await adapter.close().catch(() => {}); adapter = null; session = null;
      throw error;
    }
    $('installation').hidden = false;
    result('A4 更新检查通过。可先保存备份，然后确认安装本测试版。');
  });
});
$('disconnect').addEventListener('click', () => operate(async () => {
  session?.invalidate(); await adapter?.close(); adapter = session = null;
  $('installation').hidden = true; $('confirm').checked = false;
  result('已断开。下次安装需要重新连接检查。');
}));
$('backup').addEventListener('click', () => operate(async () => {
  const backup = await session.backupFlash((done, total) => progress('保存完整备份', done, total));
  if (backupUrl) URL.revokeObjectURL(backupUrl);
  backupUrl = URL.createObjectURL(new Blob([backup.bytes]));
  $('save-backup').href = backupUrl;
  $('save-backup').download = `RickyOS-A4-backup-${new Date().toISOString().replace(/[:.]/g, '-')}.bin`;
  $('save-backup').hidden = false; $('save-backup').click();
  result(`完整备份已校验并下载。SHA-256：${backup.sha256}`);
}));
$('install').addEventListener('click', () => operate(async () => {
  requireThat($('confirm').checked && session?.ready, '请先检查设备并确认安装。');
  const bytes = await fetchFirmware();
  result('正在安装测试版，请保持连接。');
  const restarted = await session.install(bytes, manifest,
    (done, total) => progress('写入测试固件', done, total), true,
    label => { $('operation').textContent = label; result(label); });
  progress('安装及校验完成', 1, 1);
  result(restarted ? '测试版已安装并通过校验，设备正在重启。' : '测试版已安装并通过校验，请手动重启设备。');
  await adapter.close().catch(() => {}); adapter = session = null;
  $('installation').hidden = true; $('confirm').checked = false;
}));
$('save-log').addEventListener('click', () => download(logs.join('\n'), 'RickyOS-A4-test-log.txt', 'text/plain;charset=utf-8'));
addEventListener('beforeunload', event => { if (busy) { event.preventDefault(); event.returnValue = ''; } });
navigator.serial?.addEventListener('disconnect', event => {
  if (event.target !== adapter?.transport?.device || session?.restarting) return;
  session?.invalidate(); controls(); result('设备连接断开，请重新连接检查。', true);
});
try {
  requireThat(await md5(new Uint8Array()) === 'd41d8cd98f00b204e9800998ecf8427e' &&
    await sha256(new Uint8Array()) === 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
  '浏览器摘要校验初始化失败。');
  const response = await fetch('./candidate.json', { cache: 'no-store' });
  requireThat(response.ok, '测试固件信息加载失败。');
  manifest = checkLabManifest(await response.json());
  $('build-version').textContent = `EEGO A4 · ${manifest.version}`;
  $('firmware-link').href = `./${manifest.file}`; $('firmware-link').hidden = false;
  $('support').textContent = navigator.serial && isSecureContext ? '已准备好，请确认机型后连接设备。' :
    '请用电脑上的 Chrome 或 Edge 打开此 HTTPS 页面，手机浏览器不能通过 USB 刷写。';
  log(`测试构建：${manifest.build}；SHA-256：${manifest.sha256}`);
} catch (error) { manifest = null; result(error.message, true); }
controls();
