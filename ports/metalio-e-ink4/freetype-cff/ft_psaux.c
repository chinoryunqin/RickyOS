/* RickyOS: optional FreeType CFF module; see provenance.json and LICENSE.TXT. */
#define FT2_BUILD_LIBRARY
#include <ft2build.h>
#include FT_FREETYPE_H
#if FREETYPE_MAJOR != 2 || FREETYPE_MINOR != 13 || FREETYPE_PATCH != 3
#error "RickyOS CFF extension must match the pinned FreeType 2.13.3 core"
#endif
#include "src/psaux/psaux.c"
