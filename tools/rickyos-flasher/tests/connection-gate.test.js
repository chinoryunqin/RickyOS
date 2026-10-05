import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { canConnect } from '../src/connection-gate.js';

const ready = { demo: false, release: {}, supported: true, hashReady: true,
  confirmed: true, busy: false, connected: false };
test('empty or rejected release keeps real USB connection closed even after model confirmation', () => {
  assert.equal(canConnect({ ...ready, release: null }), false);
  assert.equal(canConnect({ ...ready, release: undefined }), false);
  assert.equal(canConnect(ready), true);
});
test('live connection still requires browser, hash, confirmation and idle session', () => {
  for (const property of ['supported', 'hashReady', 'confirmed']) {
    assert.equal(canConnect({ ...ready, [property]: false }), false);
  }
  assert.equal(canConnect({ ...ready, busy: true }), false);
  assert.equal(canConnect({ ...ready, connected: true }), false);
});
test('demo remains available without a release, Web Serial or hashing capability', () => {
  const demo = { ...ready, demo: true, release: null, supported: false, hashReady: false };
  assert.equal(canConnect(demo), true);
  assert.equal(canConnect({ ...demo, confirmed: false }), false);
  assert.equal(canConnect({ ...demo, busy: true }), false);
  assert.equal(canConnect({ ...demo, connected: true }), false);
});
test('controller gates the permission request and button through the same connection policy', async () => {
  const app = await readFile(new URL('../src/app.js', import.meta.url), 'utf8');
  assert.match(app, /\$\('connect'\)\.disabled = !connectionAllowed\(\)/);
  assert.match(app, /addEventListener\('click', \(\) => \{\s*if \(!connectionAllowed\(\)\) return;[\s\S]*?navigator\.serial\.requestPort\(\)/);
  assert.match(app, /requireThat\(release, '正式固件尚未发布/);
});
