import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const root = new URL('../', import.meta.url);
const read = path => readFile(new URL(path, root), 'utf8');
test('product homepage links to real installer without loading serial controller', async () => {
  const html = await read('index.html'), js = await read('src/site.js');
  assert.match(html, /class="concept-a"/);
  assert.match(html, /href="\.\/install\.html"/);
  assert.match(html, /src="\.\/src\/site\.js"/);
  assert.doesNotMatch(html, /src="[^"\n]*app\.js"/);
  assert.doesNotMatch(js, /esptool|navigator\.serial|requestPort|FlashSession/);
});
test('installer preserves every controller element and all stage indicators', async () => {
  const html = await read('install.html'), js = await read('src/app.js');
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
  assert.equal(ids.length, new Set(ids).size, 'Duplicate element ID');
  for (const match of js.matchAll(/\$\('([^']+)'\)/g)) assert.ok(ids.includes(match[1]), `Missing controller element ${match[1]}`);
  for (const id of ['step-connect', 'step-backup', 'step-install']) assert.ok(ids.includes(id));
  assert.match(html, /<ol class="install-steps"/);
  assert.doesNotMatch(html, /role="tab"|data-stage|data-show-finish|preview-next/);
});
test('production pages use real native screenshot and contain no prototype controls', async () => {
  for (const file of ['index.html', 'install.html']) {
    const html = await read(file);
    assert.match(html, /src="\.\/src\/assets\/home-frame\.png"/);
    assert.match(html, /原生模拟器/);
    assert.doesNotMatch(html, /design-preview|concepts\.js|proposal-bar|查看方案 [AB]|预览检查后的界面/);
  }
});
test('all production page links are relative and license page is reachable', async () => {
  for (const file of ['index.html', 'install.html', 'licenses.html']) {
    const html = await read(file);
    assert.doesNotMatch(html, /(?:src|href)="http:\/\/127\.0\.0\.1|(?:src|href)="\//);
  }
  assert.match(await read('index.html'), /href="\.\/licenses\.html"/);
  assert.match(await read('install.html'), /href="\.\/licenses\.html"/);
  assert.match(await read('licenses.html'), /href="\.\/install\.html"/);
});
test('installer is built as a separate entry and keeps release and risk gates visible', async () => {
  assert.match(await read('vite.config.js'), /install: resolve\(import\.meta\.dirname, 'install\.html'\)/);
  const html = await read('install.html');
  assert.match(html, /id="install-button"[^>]*disabled/);
  assert.match(html, /id="model-confirm"/);
  assert.match(html, /id="write-confirm"/);
  assert.match(html, /正式安装包尚未开放/);
  assert.match(html, /完整 Flash 备份也不含 SD 卡/);
});
