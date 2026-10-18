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
test('production pages label their device images and contain no prototype controls', async () => {
  const home = await read('rickyos.html'), install = await read('install.html');
  // Device images are real photos; the pages say so.
  assert.match(home, /src="\.\/src\/assets\/home\/pico-landscape\.jpg"/);
  assert.match(home, /alt="[^"]*小纸 Read Pico/);
  assert.match(install, /src="\.\/src\/assets\/device-pico\.jpg"/);
  assert.match(install, /实拍照片/);
  for (const html of [home, install])
    assert.doesNotMatch(html, /design-preview|concepts\.js|proposal-bar|查看方案 [AB]|预览检查后的界面/);
});
test('all production page links are relative and license page is reachable', async () => {
  for (const file of ['index.html', 'rickyos.html', 'install.html', 'licenses.html', 'developers.html', 'verse.html']) {
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
  assert.match(html, /网站不操作 SD 卡/);
  assert.match(html, /id="backup-opt"/);
});

test('site navigation is the studio, RickyOS, developer program, RickyVerse and one install button', async () => {
  for (const file of ['index.html', 'rickyos.html', 'developers.html', 'verse.html']) {
    const html = await read(file);
    const nav = html.match(/<nav class="h-nav"[^>]*>(.*?)<\/nav>/s)?.[1] ?? '';
    const links = [...nav.matchAll(/href="([^"]+)"/g)].map(match => match[1]);
    assert.deepEqual(links, ['./rickyos.html', './developers.html', './verse.html', './27.html', './install.html'], file);
    assert.match(html, /class="h-brand" href="\.\/">RICKY AI STUDIO<\/a>/);
    assert.doesNotMatch(html, /data-brand-logo|assets\/home\/(?:studio|duo|developers|verse)\.jpg/);
    assert.match(html, /href="\.\/licenses\.html"/);
    assert.match(html, /src="\.\/src\/site\.js"/);
    assert.doesNotMatch(html, /src="[^"\n]*app\.js"/);
  }
  assert.match(await read('vite.config.js'), /rickyos: resolve[\s\S]*developers: resolve[\s\S]*verse: resolve/);
  assert.match(await read('index.html'), /<title>Ricky AI Studio<\/title>/);
  assert.match(await read('index.html'), /href="\.\/rickyos\.html"/);
  // The RickyOS 27 teaser stays as published; it links back to the installer.
  assert.match(await read('27.html'), /src="\.\/src\/r27\.css"|href="\.\/src\/r27\.css"/);
  assert.match(await read('27.html'), /href="\.\/install\.html"/);
});
test('installer offers Read Pico and the Plus (the A4 once released), Read Pico chosen first', async () => {
  for (const file of ['index.html', 'rickyos.html', 'developers.html', 'verse.html', 'install.html'])
    assert.doesNotMatch(await read(file), /不愿意透露姓名|一位朋友|正在赶来|f-emblem|dp-friend/, file);
  const html = await read('install.html');
  const picker = html.match(/<section class="device-picker"[\s\S]*?<\/section>/)?.[0] ?? '';
  assert.equal((picker.match(/class="dp-option/g) ?? []).length, 4);
  assert.deepEqual([...picker.matchAll(/data-device="([\w-]+)"/g)].map(m => m[1]), ['readpico', 'plus', 'eego']);
  // The A4 stays hidden until the catalog has its release (app.js unhides it).
  assert.match(picker, /data-device="eego"[^>]*hidden/);
  assert.equal((picker.match(/dp-active/g) ?? []).length, 1);
  assert.match(picker, /dp-active[^>]*>[\s\S]*?Read Pico/);
  assert.equal((picker.match(/aria-disabled="true"/g) ?? []).length, 1);
  assert.match(html, /id="model-name"/);
});
