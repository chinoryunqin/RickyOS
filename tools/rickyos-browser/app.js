'use strict';
const screen = document.querySelector('#screen');
const token = document.querySelector('meta[name="ricky-token"]').content;
const notice = document.querySelector('#notice');
let etag = '', imageUrl = '', inputReady = false, nativeWidth = 684, nativeHeight = 1216;
let pointer = null, gestureQueue = Promise.resolve();

async function post(path, body = {}) {
  const response = await fetch(path, {method: 'POST', headers: {'Content-Type': 'application/json', 'X-Ricky-Token': token}, body: JSON.stringify(body)});
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '操作失败，请稍后重试。');
  return data;
}
function input(data) {
  if (!inputReady) { notice.textContent = '等待模拟器启动完成…'; return; }
  // Preserve browser gesture ordering without a held state surviving a lost connection.
  gestureQueue = gestureQueue.then(() => post('/api/input', data)).catch(error => {notice.textContent = error.message;});
  notice.textContent = '';
}
document.querySelectorAll('[data-key]').forEach(button => {
  button.addEventListener('click', () => input({type: 'key', key: button.dataset.key}));
});
const keyMap = {Escape: 'BACK', Enter: 'ENTER', ArrowUp: 'UP', ArrowDown: 'DOWN', ArrowLeft: 'LEFT', ArrowRight: 'RIGHT', p: 'POWER', P: 'POWER', s: 'SLEEP', S: 'SLEEP'};
document.addEventListener('keydown', event => {
  if (event.repeat || event.ctrlKey || event.metaKey || event.altKey || event.target.closest('button,a,input,textarea,select')) return;
  if (keyMap[event.key]) { event.preventDefault(); input({type: 'key', key: keyMap[event.key]}); }
});
function normalized(event) {
  const rect = screen.getBoundingClientRect();
  return {x: Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width)), y: Math.max(0, Math.min(1, (event.clientY - rect.top) / rect.height))};
}
screen.addEventListener('pointerdown', event => {
  if (pointer || event.button !== 0) return;
  screen.focus({preventScroll: true});
  pointer = {id: event.pointerId, start: normalized(event), at: performance.now()};
  screen.setPointerCapture(event.pointerId);
  event.preventDefault();
});
screen.addEventListener('pointerup', event => {
  if (!pointer || pointer.id !== event.pointerId) return;
  const end = normalized(event);
  const moved = Math.hypot((end.x - pointer.start.x) * nativeWidth, (end.y - pointer.start.y) * nativeHeight) > 28;
  const duration = Math.round(Math.max(moved ? 90 : 80, Math.min(650, performance.now() - pointer.at)));
  input({type: 'touch', x1: pointer.start.x, y1: pointer.start.y, x2: end.x, y2: end.y, duration});
  pointer = null;
});
screen.addEventListener('pointercancel', () => {pointer = null;});
screen.addEventListener('lostpointercapture', () => {pointer = null;});
screen.addEventListener('contextmenu', event => event.preventDefault());
document.querySelector('#scale').addEventListener('click', event => {
  const original = document.querySelector('#stage').classList.toggle('original');
  event.target.setAttribute('aria-pressed', String(original));
  event.target.textContent = original ? '适应窗口' : '原始尺寸';
  screen.style.width = original ? nativeWidth + 'px' : '';
});
document.querySelector('#restart').addEventListener('click', async () => {
  try { await post('/api/restart'); etag = ''; notice.textContent = '已重新启动，演示设置和书籍保留。'; }
  catch (error) {notice.textContent = error.message;}
});
document.querySelector('#rebuild').addEventListener('click', async () => {
  try { await post('/api/rebuild'); notice.textContent = '正在编译最新界面，完成后自动加载。不会刷写设备。'; }
  catch (error) {notice.textContent = error.message;}
});

async function updateFrame() {
  try {
    const response = await fetch('/api/frame', {headers: etag ? {'If-None-Match': etag} : {}});
    if (response.ok) {
      const nextUrl = URL.createObjectURL(await response.blob());
      nativeWidth = Number(response.headers.get('X-Frame-Width'));
      nativeHeight = Number(response.headers.get('X-Frame-Height'));
      const oldUrl = imageUrl;
      screen.onload = () => { if (oldUrl) URL.revokeObjectURL(oldUrl); document.querySelector('#loading').classList.add('hidden'); };
      screen.width = nativeWidth; screen.height = nativeHeight;
      screen.src = nextUrl; imageUrl = nextUrl;
      if (document.querySelector('#stage').classList.contains('original')) screen.style.width = nativeWidth + 'px';
      etag = response.headers.get('ETag');
      document.querySelector('#dimensions').textContent = `${nativeWidth} × ${nativeHeight}`;
      document.querySelector('#frame-status').textContent = '实时画面 · ' + new Date().toLocaleTimeString('zh-CN', {hour12: false});
    }
  } catch (_) { /* Status loop reports disconnection; retain the last valid frame. */ }
  setTimeout(updateFrame, document.hidden ? 1000 : 160);
}
let wasBuilding = false;
async function updateStatus() {
  try {
    const response = await fetch('/api/status');
    const data = await response.json();
    inputReady = data.alive && data.input_ready;
    if (notice.textContent === '重新打开“RickyOS 浏览器预览.command”后刷新本页。') notice.textContent = '';
    document.querySelector('#status').textContent = data.building ? '正在更新预览…' : data.alive ? '模拟器运行中' : '模拟器已停止';
    document.querySelector('#status-dot').classList.toggle('live', data.alive);
    document.querySelector('#activity').textContent = inputReady ? '684 × 1216 · 原生固件渲染' : '正在启动';
    document.querySelector('#build-time').textContent = '模拟器编译：' + data.binary_updated;
    document.querySelector('#rebuild').disabled = data.building;
    document.querySelector('#restart').disabled = data.building;
    if (data.error) notice.textContent = data.error;
    else if (wasBuilding && !data.building) {etag = ''; notice.textContent = '最新界面已加载。';}
    wasBuilding = data.building;
  } catch (_) {
    inputReady = false;
    document.querySelector('#status').textContent = '本地服务已断开';
    document.querySelector('#status-dot').classList.remove('live');
    notice.textContent = '重新打开“RickyOS 浏览器预览.command”后刷新本页。';
  }
  setTimeout(updateStatus, 1000);
}
updateFrame(); updateStatus();
