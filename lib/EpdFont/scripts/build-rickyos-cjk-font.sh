#!/bin/bash
#
# Generates the RickyOS 12pt CJK header: the shared common set
# (cn_common_chars.txt) plus every GB2312 level-1 hanzi (gb2312_lv1.txt), so
# book titles and author names such as 亨利 render instead of falling back to
# tofu. Read Pico's high-density profile uses only the 12pt face, so the
# product swaps this one font and leaves the shared 8/10/12pt headers (and
# their common interval table) untouched for every other target.
#
# Same source, subset flags and fontconvert options as build-cn-builtin-fonts.sh;
# on the shared characters the glyph bitmaps are byte-identical.
#
#   PYTHON=/path/to/venv/bin/python bash build-rickyos-cjk-font.sh

set -euo pipefail

cd "$(dirname "$0")"

PYTHON="${PYTHON:-python3}"
SOURCE_OTF="../builtinFonts/source/NotoSansSC/NotoSansSC-Regular.otf"
SOURCE_SHA256="2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
CHARSET_FILE="cn_rickyos_chars.txt"
TMP_DIR="instanced_fonts/NotoSansSC"
SUBSET_OTF="$TMP_DIR/NotoSansSC-Regular.rickyos.otf"
FONT_NAME="notosans_cjk_12_rickyos"
OUTPUT_PATH="../builtinFonts/${FONT_NAME}.h"

if [ ! -f "$SOURCE_OTF" ]; then
  echo "Error: $SOURCE_OTF not found; see docs/engineering/chinese-build.md." >&2
  exit 1
fi
"$PYTHON" - "$SOURCE_OTF" "$SOURCE_SHA256" <<'EOF'
import hashlib, sys
digest = hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest()
if digest != sys.argv[2]:
    sys.exit(f'Error: source SHA-256 {digest} differs from the committed fonts\' {sys.argv[2]}')
EOF

# Common set first, then GB2312 level-1 hanzi it lacks, in file order.
"$PYTHON" - cn_common_chars.txt gb2312_lv1.txt "$CHARSET_FILE" <<'EOF'
import sys
common = open(sys.argv[1], encoding='utf-8').read().strip()
seen = set(common)
extra = [c for c in open(sys.argv[2], encoding='utf-8').read()
         if 0x4E00 <= ord(c) <= 0x9FFF and c not in seen and not seen.add(c)]
open(sys.argv[3], 'w', encoding='utf-8').write(common + ''.join(extra))
print(f'{sys.argv[3]}: {len(common)} common + {len(extra)} GB2312 level-1 characters')
EOF

mkdir -p "$TMP_DIR"
"$PYTHON" -m fontTools.subset "$SOURCE_OTF" \
  --output-file="$SUBSET_OTF" \
  --text-file="$CHARSET_FILE" \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2010-2026,U+3000-303F,U+FF00-FF9F,U+FFA1-FFEF,U+FFFD" \
  --layout-features='*' \
  --notdef-outline \
  --recommended-glyphs \
  --no-hinting \
  --drop-tables+=DSIG,GSUB,GPOS

tmp_path="${OUTPUT_PATH}.tmp"
"$PYTHON" fontconvert.py "$FONT_NAME" 12 "$SUBSET_OTF" \
  --2bit \
  --additional-intervals 0x4E00,0x9FFF \
  --additional-intervals 0x3000,0x303F \
  --additional-intervals 0xFF00,0xFFEF \
  > "$tmp_path"
if [ ! -s "$tmp_path" ]; then
  echo "Error: fontconvert.py produced empty $tmp_path" >&2
  rm -f "$tmp_path"
  exit 1
fi
mv "$tmp_path" "$OUTPUT_PATH"
echo "  $(wc -c < "$OUTPUT_PATH") bytes ($(grep -E "Bitmaps\[" "$OUTPUT_PATH" | head -1))"
