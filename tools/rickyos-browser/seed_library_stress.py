#!/usr/bin/env python3
"""Create readable, synthetic books on this checkout's PRIVATE simulator card.

Never copies personal books, synthesizes library.idx, modifies sources/categories,
deletes existing content, or connects to a device. Import via the real firmware UI.
"""
import argparse
from collections import Counter
import io
import json
from pathlib import Path
import struct
import time
from xml.sax.saxutils import escape
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = 'rickyos-library-stress-v1'
SUBJECTS = ('山间来信', '城市散步', '纸上花园', '夜航笔记', '时间的书房', '阅读与生活', '学习手记', '工作记录')
PARAGRAPHS = (
    '这是一份由程序生成的压力测试读物，不是正式出版物，也不包含用户的私人书籍。',
    '清晨推开窗，远处的树叶在风中轻轻摇动。把一页书读完，再去散步，普通的一天也有值得记录的片刻。',
    '书库负责保存用户选择的书籍引用，存储负责浏览文件。文件夹中的其他资料不应在进入页面时被反复解析。',
    '测试包含中文标题、长文件名、多层目录、相似书名与不同格式，用于检查收录、翻页、筛选和打开图书的行为。',
)


def cover_png(seed):
    """Small valid grayscale cover; generated image bytes, not edited user media."""
    width, height = 120, 180
    pixels = b''.join(b'\0' + bytes(32 + ((x // 12 + y // 18 + seed) % 8) * 28 for x in range(width))
                      for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


def epub(number, title):
    author = f'测试作者 {number % 24 + 1:02d}'
    chapters = 12 if number % 100 == 0 else 4
    image = number % 20 == 0
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_DEFLATED) as book:
        book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        book.writestr('META-INF/container.xml', '<?xml version="1.0"?><container version="1.0" '
                      'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
                      '<rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
                      '</rootfiles></container>')
        manifest = ''.join(f'<item id="c{i}" href="chapter{i}.xhtml" media-type="application/xhtml+xml"/>'
                           for i in range(1, chapters + 1))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(1, chapters + 1))
        if image:
            manifest += '<item id="cover" href="cover.png" media-type="image/png"/>'
            book.writestr('OEBPS/cover.png', cover_png(number))
        metadata = (f'<dc:identifier id="id">urn:rickyos:stress:{number}</dc:identifier>'
                    f'<dc:title>{escape(title)}</dc:title><dc:creator>{author}</dc:creator>'
                    '<dc:language>zh-CN</dc:language>')
        if image:
            metadata += '<meta name="cover" content="cover"/>'
        book.writestr('OEBPS/content.opf', '<?xml version="1.0" encoding="utf-8"?>'
                      '<package version="2.0" unique-identifier="id" xmlns="http://www.idpf.org/2007/opf">'
                      '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">' + metadata + '</metadata>'
                      '<manifest><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
                      + manifest + '</manifest><spine toc="ncx">' + spine + '</spine></package>')
        nav = ''.join(f'<navPoint id="p{i}" playOrder="{i}"><navLabel><text>第 {i} 章</text></navLabel>'
                      f'<content src="chapter{i}.xhtml"/></navPoint>' for i in range(1, chapters + 1))
        book.writestr('OEBPS/toc.ncx', '<?xml version="1.0" encoding="utf-8"?>'
                      '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
                      f'<head/><docTitle><text>{escape(title)}</text></docTitle><navMap>{nav}</navMap></ncx>')
        for chapter in range(1, chapters + 1):
            body = ''.join(f'<p>第 {number:04d} 册，第 {chapter} 章，第 {i + 1} 段。'
                           f'{PARAGRAPHS[i % len(PARAGRAPHS)]}</p>' for i in range(36))
            if image and chapter == 1:
                body = '<p><img src="cover.png" alt="测试封面"/></p>' + body
            book.writestr(f'OEBPS/chapter{chapter}.xhtml', '<?xml version="1.0" encoding="utf-8"?>'
                          '<html xmlns="http://www.w3.org/1999/xhtml"><head>'
                          f'<title>第 {chapter} 章</title></head><body><h1>第 {chapter} 章</h1>{body}</body></html>')
    return buffer.getvalue()


def same_book(path, data):
    existing = path.read_bytes()
    if existing == data:
        return True
    if path.suffix != '.epub':
        return False
    # zipfile timestamps are not book content. A second run validates every
    # archive entry without rewriting a previously generated EPUB.
    try:
        with zipfile.ZipFile(io.BytesIO(existing)) as old, zipfile.ZipFile(io.BytesIO(data)) as new:
            return old.namelist() == new.namelist() and all(old.read(name) == new.read(name) for name in new.namelist())
    except (zipfile.BadZipFile, KeyError):
        return False


def generated_book(number):
    subject = SUBJECTS[(number - 1) % len(SUBJECTS)]
    title = f'测试 {number:04d} · {subject}'
    if number % 17 == 0:
        title += '——一个用于检查长书名排版和截断的安静下午（修订版）'
    extension = ('.epub', '.txt', '.md')[(number - 1) % 3]
    if extension == '.epub':
        data = epub(number, title)
    else:
        lines = [title, '', '合成测试读物 · 不含用户资料', '']
        paragraphs = 1800 if number % 100 == 0 else 160
        for i in range(paragraphs):
            if i % 40 == 0:
                lines.extend([f'## 第 {i // 40 + 1} 章' if extension == '.md' else f'第 {i // 40 + 1} 章', ''])
            lines.append(f'{number:04d}-{i + 1:04d} {PARAGRAPHS[i % len(PARAGRAPHS)]}')
        if extension == '.md':
            lines.extend(['', '| 项目 | 内容 |', '| --- | --- |', '| 类型 | 压力测试 |', '| 编号 | ' + str(number) + ' |'])
        data = ('\n\n'.join(lines) + '\n').encode('utf-8')
    return title + extension, data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state-dir', type=Path, default=ROOT / '.cache/library-storage-preview')
    parser.add_argument('--books', type=int, default=3000)
    parser.add_argument('--flat-books', type=int, default=2000)
    args = parser.parse_args()
    state = args.state_dir.resolve()
    # Intentionally refuse arbitrary /Volumes paths or SD cards. The tool may
    # only populate the private preview cache owned by this checkout.
    if not state.is_relative_to(ROOT / '.cache') or not (state / 'preview.lock').is_file():
        parser.error('target must be an initialized private preview inside this checkout/.cache')
    sd = state / 'sd'
    if sd.is_symlink() or not (sd / '.crosspoint/settings.json').is_file():
        parser.error('missing private simulator settings or unsafe SD link')
    if not 1 <= args.books <= 4000 or not 0 <= args.flat_books <= args.books:
        parser.error('use 1..4000 books and a flat-books count within that total')
    destination = sd / 'books/压力测试'
    if any(p.is_symlink() for p in (sd / 'books', destination)):
        parser.error('refusing linked book paths')
    started = time.monotonic()
    counts = Counter()
    total_bytes = added = 0
    for number in range(1, args.books + 1):
        folder = (destination / '单层目录' if number <= args.flat_books else
                  destination / '分目录' / f'分组 {(number - args.flat_books - 1) // 50 + 1:02d}')
        if any(parent.is_symlink() for parent in (folder, folder.parent)):
            parser.error('refusing linked stress-data paths')
        folder.mkdir(parents=True, exist_ok=True)
        name, data = generated_book(number)
        path = folder / name
        # Re-running verifies existing fixtures; never overwrites any file.
        if path.exists():
            if path.is_symlink() or not same_book(path, data):
                raise RuntimeError(f'existing file differs, left untouched: {path}')
        else:
            with path.open('xb') as output:
                output.write(data)
            added += 1
        counts[path.suffix] += 1
        total_bytes += len(data)
    report = {'generator': GENERATOR, 'books': args.books, 'flat_books': args.flat_books,
              'formats': dict(counts), 'bytes': total_bytes, 'added': added,
              'source': '/books/压力测试', 'elapsed_seconds': round(time.monotonic() - started, 3),
              'note': 'Synthetic readable files only; use real firmware import, no fabricated catalog.'}
    (state / 'stress-data.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
