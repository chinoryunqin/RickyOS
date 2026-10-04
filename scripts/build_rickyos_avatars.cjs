#!/usr/bin/env node
/** Default RickyOS avatars: original line-art SVGs in the logo's style
 * (smooth thick outlines, solid black hair, closed smiling eyes) -> 160 px
 * 1-bpp Flash resources. Smaller avatars are area-sampled at draw time.
 * Developer-only dependency: sharp 0.35.4 (librsvg 2.62.91).
 * Run: NODE_PATH=/path/to/node_modules node scripts/build_rickyos_avatars.cjs [--out header.h] [--preview sheet.png]
 * Writes the SVG sources to src/images/sources/rickyos/avatars/ and the header.
 */
const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '..');
const sourceDir = path.join(root, 'src/images/sources/rickyos/avatars');
const SIZE = 160;
const THRESHOLD = 128;
const INK = 'fill="#000"';
const LINE = 'fill="none" stroke="#000" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"';
const FACE = 'fill="#fff" stroke="#000" stroke-width="6" stroke-linejoin="round"';

// Shared parts on a 200x200 canvas; the firmware masks the result to a circle.
const parts = {
  face: (cy = 112, rx = 48, ry = 56) => `<ellipse cx="100" cy="${cy}" rx="${rx}" ry="${ry}" ${FACE}/>`,
  ears: (cy = 116, x = 52) =>
    `<path d="M${x} ${cy - 12} C${x - 16} ${cy - 16} ${x - 18} ${cy + 14} ${x + 1} ${cy + 14}" ${FACE}/>` +
    `<path d="M${200 - x} ${cy - 12} C${216 - x} ${cy - 16} ${218 - x} ${cy + 14} ${199 - x} ${cy + 14}" ${FACE}/>`,
  // Closed crescent eyes, the logo's signature expression.
  eyes: (y = 114, spread = 21, w = 9) =>
    `<path d="M${100 - spread - w} ${y} Q${100 - spread} ${y - 11} ${100 - spread + w} ${y}" ${LINE}/>` +
    `<path d="M${100 + spread - w} ${y} Q${100 + spread} ${y - 11} ${100 + spread + w} ${y}" ${LINE}/>`,
  smile: (y = 142, w = 14) => `<path d="M${100 - w} ${y} Q100 ${y + 11} ${100 + w} ${y}" ${LINE}/>`,
  glasses: (y = 100, round = false) => {
    const r = round ? 13 : 8;
    return `<rect x="63" y="${y}" width="32" height="27" rx="${r}" ${LINE.replace('6', '5')}/>` +
      `<rect x="105" y="${y}" width="32" height="27" rx="${r}" ${LINE.replace('6', '5')}/>` +
      `<path d="M95 ${y + 11} Q100 ${y + 7} 105 ${y + 11}" ${LINE.replace('6', '5')}/>`;
  },
  blush: (y = 130) => `<path d="M64 ${y} l6 -3 M130 ${y - 3} l6 3" ${LINE.replace('6', '4')}/>`,
  lines: (y = 120) => `<path d="M58 ${y - 2} l-5 -4 M142 ${y - 2} l5 -4" ${LINE.replace('6', '4')}/>`,
};

const AVATARS = [
  ['girl', 'Girl', () => [
    `<circle cx="44" cy="92" r="17" ${INK}/>`, `<circle cx="156" cy="92" r="17" ${INK}/>`,
    parts.ears(120, 54), parts.face(116, 46, 52),
    `<path d="M54 116 C48 70 74 52 100 52 C126 52 152 70 146 116 C140 98 128 86 112 84 C104 92 92 96 80 92 C70 98 60 104 54 116 Z" ${INK}/>`,
    parts.eyes(118, 19, 8), parts.blush(134), parts.smile(144, 11)]],
  ['boy', 'Boy', () => [
    parts.ears(120, 54), parts.face(116, 46, 52),
    `<path d="M54 112 C46 76 62 54 84 50 L88 38 L98 48 L108 36 L114 50 C136 54 154 74 146 112 C142 96 134 86 124 84 L120 94 L110 84 L100 94 L90 84 L80 94 L72 88 C62 96 58 104 54 112 Z" ${INK}/>`,
    parts.eyes(118, 19, 8), parts.blush(134), parts.smile(144, 11)]],
  ['woman', 'Young woman', () => [
    `<path d="M48 110 C40 54 74 40 100 40 C130 40 162 56 152 112 C150 150 160 178 168 200 L32 200 C40 178 50 150 48 110 Z" ${INK}/>`,
    parts.face(112, 46, 55),
    `<path d="M54 104 C54 66 78 50 104 50 C128 50 148 66 146 98 C132 90 118 76 110 64 C98 80 76 94 54 104 Z" ${INK}/>`,
    parts.eyes(114), parts.smile(142, 12)]],
  ['man', 'Young man', () => [
    parts.ears(), parts.face(),
    `<path d="M52 110 C42 60 78 42 108 44 C140 46 160 68 150 106 C146 92 140 84 130 80 C124 92 112 96 96 94 C110 88 118 80 120 72 C100 84 78 86 64 84 C58 92 54 100 52 110 Z" ${INK}/>`,
    parts.eyes(), parts.smile()]],
  ['mid_woman', 'Middle-aged woman', () => [
    `<path d="M46 122 C38 64 70 44 100 44 C132 44 162 64 154 122 C156 140 150 156 140 162 L60 162 C50 156 44 140 46 122 Z" ${INK}/>`,
    parts.face(112, 46, 55),
    `<path d="M54 106 C52 70 76 52 100 52 C124 52 148 68 146 106 C140 88 126 78 100 76 C76 78 60 88 54 106 Z" ${INK}/>`,
    `<path d="M86 54 Q80 66 70 78" fill="none" stroke="#fff" stroke-width="4" stroke-linecap="round"/>`,
    parts.glasses(), parts.eyes(114, 21, 7), parts.smile(142, 12),
    `<circle cx="50" cy="136" r="5" ${INK}/>`, `<circle cx="150" cy="136" r="5" ${INK}/>`]],
  ['mid_man', 'Middle-aged man', () => [
    parts.ears(), parts.face(),
    `<path d="M52 108 C46 70 70 52 100 52 C130 52 154 70 148 108 C144 96 140 88 132 84 C122 74 110 70 100 72 C90 70 78 74 68 84 C60 88 56 96 52 108 Z" ${INK}/>`,
    parts.glasses(), parts.eyes(114, 21, 7),
    `<path d="M80 134 Q90 128 100 133 Q110 128 120 134 Q110 138 100 136 Q90 138 80 134 Z" ${INK}/>`,
    parts.smile(146, 10)]],
  ['old_woman', 'Older woman', () => [
    `<circle cx="100" cy="54" r="17" ${FACE}/>`, `<path d="M90 48 Q100 43 110 48 M88 57 Q100 53 112 57" ${LINE.replace('6', '4')}/>`,
    parts.ears(), parts.face(),
    // Silver hair: outlined, with a few strands, so it reads lighter than black.
    `<path d="M52 108 C46 66 72 54 100 54 C128 54 154 66 148 108 C140 90 124 80 100 80 C76 80 60 90 52 108 Z" ${FACE}/>`,
    `<path d="M72 66 Q84 74 100 72 M128 66 Q116 74 100 72 M60 92 Q72 80 86 78 M140 92 Q128 80 114 78" ${LINE.replace('6', '4')}/>`,
    parts.glasses(98, true), parts.eyes(112, 21, 7), parts.lines(116), parts.smile(142, 12)]],
  ['old_man', 'Older man', () => [
    parts.ears(), parts.face(110, 48, 54),
    // White beard over the lower face, from the cheeks around the chin.
    `<path d="M54 118 C54 166 78 186 100 186 C122 186 146 166 146 118 C134 134 118 140 100 138 C82 140 66 134 54 118 Z" ${FACE}/>`,
    `<path d="M52 108 C46 92 50 80 58 76 C60 88 62 96 64 106 M148 108 C154 92 150 80 142 76 C140 88 138 96 136 106" ${FACE}/>`,
    `<path d="M80 72 Q100 66 120 72 M86 82 Q100 78 114 82" ${LINE.replace('6', '4')}/>`,
    parts.glasses(98, true), parts.eyes(112, 21, 7), parts.lines(116),
    `<path d="M80 134 Q100 126 120 134 Q100 142 80 134 Z" ${FACE}/>`,
    `<path d="M76 160 Q86 168 92 176 M124 160 Q114 168 108 176" ${LINE.replace('6', '4')}/>`]],
];

function svgFor(draw) {
  // The 168-unit window around the heads makes them fill the avatar circle.
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="16 30 168 168" width="200" height="200">` +
    `<rect x="0" y="0" width="200" height="200" fill="#fff"/>${draw().join('')}</svg>\n`;
}

async function raster(svg, size) {
  const { data, info } = await sharp(Buffer.from(svg.replace('width="200" height="200"', `width="${size}" height="${size}"`)))
    .flatten({ background: '#fff' }).greyscale().raw().toBuffer({ resolveWithObject: true });
  if (info.width !== size || info.height !== size || info.channels !== 1)
    throw new Error(`bad raster: ${info.width}x${info.height}x${info.channels}, wanted ${size}`);
  return data;
}

async function main() {
  const args = process.argv.slice(2);
  let output = path.join(root, 'src/components/icons/rickyAvatars.h');
  let preview;
  for (let i = 0; i < args.length; ++i) {
    if (args[i] === '--out' && args[i + 1]) output = path.resolve(args[++i]);
    else if (args[i] === '--preview' && args[i + 1]) preview = path.resolve(args[++i]);
    else throw new Error('Usage: build_rickyos_avatars.cjs [--out header.h] [--preview sheet.png]');
  }
  fs.mkdirSync(sourceDir, { recursive: true });
  const rowBytes = SIZE / 8;
  const lines = ['#pragma once', '', '#include <cstdint>', '',
    '// Generated by scripts/build_rickyos_avatars.cjs from the line-art SVGs in',
    '// src/images/sources/rickyos/avatars/. Edit the generator and regenerate; never these bytes.',
    `// ${SIZE}x${SIZE}, MSB-first, 1 = paper, 0 = ink. Area-sampled down at draw time.`, '',
    'namespace RickyAvatars {', `inline constexpr int SIZE = ${SIZE};`];
  const gallery = [];
  for (const [index, [name, label, draw]] of AVATARS.entries()) {
    const svg = svgFor(draw);
    fs.writeFileSync(path.join(sourceDir, `${name}.svg`), svg);
    const data = await raster(svg, SIZE);
    const bits = Buffer.alloc(rowBytes * SIZE, 255);
    for (let y = 0; y < SIZE; ++y) for (let x = 0; x < SIZE; ++x) {
      if (data[y * SIZE + x] < THRESHOLD) bits[y * rowBytes + (x >> 3)] &= ~(0x80 >> (x & 7));
    }
    lines.push(`// ${label}`, `inline constexpr uint8_t ${name}[] = {`);
    for (let offset = 0; offset < bits.length; offset += 19) {  // clang-format packs 19 per 120-column line
      lines.push('    ' + [...bits.subarray(offset, offset + 19)]
        .map(byte => '0x' + byte.toString(16).padStart(2, '0').toUpperCase()).join(', ') + ',');
    }
    lines.push('};');
    if (preview) {
      for (const [row, size] of [[0, SIZE], [1, 86]]) {
        const raw = await raster(svg, size);
        const ink = Buffer.from(raw.map(v => (v < THRESHOLD ? 0 : 255)));
        const mask = Buffer.from(`<svg width="${size}" height="${size}"><circle cx="${size / 2}" cy="${size / 2}" r="${size / 2 - 1}" fill="#fff"/></svg>`);
        const disc = await sharp(ink, { raw: { width: size, height: size, channels: 1 } })
          .composite([{ input: mask, blend: 'dest-in' }]).flatten({ background: '#fff' })
          .png().toBuffer();
        gallery.push({ input: disc, left: 20 + index * (SIZE + 20) + (SIZE - size) / 2, top: 20 + row * (SIZE + 20) });
      }
    }
  }
  lines.push(`inline constexpr const uint8_t* ALL[] = {${AVATARS.map(a => a[0]).join(', ')}};`);
  lines.push(`inline constexpr int COUNT = ${AVATARS.length};`, '}  // namespace RickyAvatars', '');
  fs.writeFileSync(output, lines.join('\n'));
  if (preview) {
    await sharp({ create: { width: 20 + AVATARS.length * (SIZE + 20), height: 40 + 2 * (SIZE + 20), channels: 3, background: '#ddd' } })
      .composite(gallery).png().toFile(preview);
  }
  console.log(`wrote ${output}: ${AVATARS.length} avatars`);
}

main().catch(error => { console.error(error); process.exit(1); });
