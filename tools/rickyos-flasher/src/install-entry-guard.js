// Connection diagnostics only: never enable controls or access the serial port.
(() => {
  const canonical = new URL(location.href);
  if (canonical.hostname === 'rickyaistudio.com' || canonical.hostname === 'www.rickyaistudio.com') {
    canonical.protocol = 'https:';
    canonical.hostname = 'rickyaistudio.com';
    if (canonical.href !== location.href) {
      location.replace(canonical.href);
      return;
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    const connect = document.getElementById('connect');
    const confirm = document.getElementById('model-confirm');
    const state = document.getElementById('device-state');
    const result = document.getElementById('result');
    const release = document.getElementById('release-tag');
    const log = document.getElementById('log');
    const hint = document.getElementById('connection-help');
    const retry = document.getElementById('reload-installer');
    if (!connect || !confirm || !hint || !retry) return;
    let loaded = false;
    setTimeout(() => { loaded = true; update(); }, 15000);

    function update() {
      let message = '';
      let recoverable = false;
      const idle = state?.textContent.trim() === '未连接';
      if (idle && connect.disabled) {
        if (!isSecureContext) {
          message = '当前页面不是安全连接，浏览器禁止使用 USB 串口。请打开 https://rickyaistudio.com/install.html。';
          recoverable = true;
        } else if (!('serial' in navigator)) {
          message = '当前浏览器不支持 USB 串口。请复制本页地址，在电脑最新版 Chrome 或 Edge 中打开（不是 Safari 或微信内置浏览器）。';
        } else if (result?.classList.contains('error')) {
          message = result.textContent.trim();
          recoverable = true;
        } else if (release?.textContent.includes('未通过')) {
          message = '发行目录检查失败，尚未连接设备。请重新加载安装页面。';
          recoverable = true;
        } else if (!log?.textContent.includes('校验能力已就绪')) {
          message = loaded ? '本地校验组件未能就绪，尚未连接设备。请重新加载安装页面；仍失败请更新 Chrome 或 Edge。' : '正在初始化本地校验组件，请稍候。尚未连接设备。';
          recoverable = loaded;
        } else if (release?.textContent.includes('正在')) {
          message = loaded ? '发行信息加载超时，尚未连接设备。请重新加载安装页面。' : '正在获取发行信息，请稍候。';
          recoverable = loaded;
        } else if (!confirm.checked) {
          message = '请先勾选上方机型确认，之后即可连接设备。';
        } else if (loaded) {
          message = '连接入口初始化未完成，尚未连接设备。请重新加载安装页面。';
          recoverable = true;
        }
      }
      if (hint.textContent !== message) hint.textContent = message;
      hint.hidden = !message;
      retry.hidden = !recoverable;
    }

    retry.addEventListener('click', () => {
      // Offer reload only before connection, never interrupt a read or write.
      if (state?.textContent.trim() !== '未连接') return;
      const target = new URL(location.href);
      target.protocol = 'https:';
      target.searchParams.set('reload', Date.now().toString());
      location.replace(target.href);
    });
    confirm.addEventListener('change', update);
    new MutationObserver(update).observe(document.getElementById('install'), {
      subtree: true, childList: true, characterData: true, attributes: true,
      attributeFilter: ['disabled', 'class'],
    });
    update();
  });
})();
