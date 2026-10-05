import { readFile, readdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const required = ['index.html', 'install.html', 'licenses.html', 'releases.json', 'ota.json', 'third-party-licenses.txt'];
const approvedAsset = /^(?:(?:site|install|rom|esp[a-z0-9]+(?:-rev[0-9]+)?)-[A-Za-z0-9_-]+\.js|site-[A-Za-z0-9_-]+\.css|(?:approved-logo|home-frame)-[A-Za-z0-9_-]+\.png)$/;

export async function checkPreview(directory) {
  const root = resolve(directory);
  const names = [];
  async function inspect(path, prefix = '') {
    for (const entry of await readdir(path, { withFileTypes: true })) {
      const name = prefix + entry.name;
      if (entry.isSymbolicLink()) throw new Error(`预览站不得发布符号链接：${name}`);
      if (entry.isDirectory()) {
        if (name !== 'assets') throw new Error(`预览站不得发布额外目录：${name}`);
        await inspect(resolve(path, entry.name), 'assets/');
      } else if (entry.isFile()) {
        if (!required.includes(name) && !(prefix === 'assets/' && approvedAsset.test(entry.name))) {
          throw new Error(`预览站包含未经审核的文件：${name}`);
        }
        names.push(name);
      } else throw new Error(`预览站包含非常规文件：${name}`);
    }
  }
  await inspect(root);
  for (const name of required) if (!names.includes(name)) throw new Error(`预览站缺少：${name}`);
  const catalog = JSON.parse(await readFile(resolve(root, 'releases.json'), 'utf8'));
  if (catalog.schema !== 1 || catalog.product !== 'RickyOS' ||
      !Array.isArray(catalog.releases) || catalog.releases.length !== 0) {
    throw new Error('此次仅发布网站预览：发行目录必须为空，不得上传任何固件。');
  }
  const ota = JSON.parse(await readFile(resolve(root, 'ota.json'), 'utf8'));
  if (ota.schema !== 1 || ota.product !== 'RickyOS' || ota.board !== 'readpico' ||
      ota.channel !== 'stable' || ota.status !== 'no_update' || Object.keys(ota).length !== 5) {
    throw new Error('预览站的设备 OTA 必须明确无更新，不得附带镜像或下载地址。');
  }
  return { files: names.length, firmware: 0 };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const report = await checkPreview(process.argv[2] ?? resolve(import.meta.dirname, '../dist'));
  console.log(`预览发布检查通过：${report.files} 个网站文件，${report.firmware} 个固件。`);
}
