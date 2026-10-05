// Isolated design prototypes. No serial, network requests or flasher imports.
document.querySelector('[data-open-install]')?.addEventListener('click', event => {
  const panel = document.querySelector('#a-install-panel');
  panel.hidden = !panel.hidden;
  event.currentTarget.setAttribute('aria-expanded', String(!panel.hidden));
  event.currentTarget.textContent = panel.hidden ? '查看安装面板 →' : '收起安装面板 ↑';
  if (!panel.hidden) panel.scrollIntoView({ block: 'start' });
});
function showStage(stage) {
  document.querySelectorAll('[data-stage]').forEach(tab => {
    tab.setAttribute('aria-selected', String(tab.dataset.stage === stage));
  });
  document.querySelectorAll('[role="tabpanel"]').forEach(panel => { panel.hidden = panel.id !== `stage-${stage}`; });
}
document.querySelectorAll('[data-stage]').forEach(tab => tab.addEventListener('click', () => showStage(tab.dataset.stage)));
document.querySelector('#b-confirm')?.addEventListener('change', event => {
  document.querySelector('#preview-next').disabled = !event.target.checked;
});
document.querySelector('#preview-next')?.addEventListener('click', () => showStage('backup'));
document.querySelector('[data-show-finish]')?.addEventListener('click', () => showStage('finish'));
document.querySelector('[data-show-check]')?.addEventListener('click', () => showStage('check'));
