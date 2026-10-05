import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
let text = 'RickyOS Web Flasher — third-party runtime licenses\n\n';
for (const [name, file] of [['esptool-js', 'LICENSE'], ['hash-wasm', 'LICENSE'],
  ['pako', 'LICENSE'], ['atob-lite', 'LICENSE.md']]) {
  text += `=== ${name} ===\n${await readFile(resolve(root, 'node_modules', name, file), 'utf8')}\n\n`;
}
// Generated release output, never hand-edited or shipped without license texts.
await writeFile(resolve(root, 'dist/third-party-licenses.txt'), text.trimEnd() + '\n');
