#include "RecoveringHtmlStream.h"

#include <cstring>

#include "Epub/htmlEntities.h"

namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool nameChar(char c) { return alpha(c) || digit(c) || c == '_' || c == ':' || c == '-' || c == '.'; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool in(const char* name, const char* list) {
  const size_t size = strlen(name);
  for (const char* p = list; *p;) {
    const char* end = strchr(p, ' ');
    const size_t length = end ? static_cast<size_t>(end - p) : strlen(p);
    if (length == size && memcmp(p, name, size) == 0) return true;
    p += length;
    if (*p) ++p;
  }
  return false;
}
bool xmlCharacter(uint32_t c) {
  return c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) ||
         (c >= 0x10000 && c <= 0x10FFFF);
}
}  // namespace

bool RecoveringHtmlStream::flush() {
  if (ok_ && outputSize_) ok_ = sink_(context_, output_, outputSize_);
  outputSize_ = 0;
  return ok_;
}
void RecoveringHtmlStream::emit(const char* bytes, size_t size) {
  for (size_t i = 0; i < size && ok_; ++i) {
    output_[outputSize_++] = bytes[i];
    if (outputSize_ == sizeof(output_)) flush();
  }
}
void RecoveringHtmlStream::emit(const char* s) { emit(s, strlen(s)); }
void RecoveringHtmlStream::escaped(char c) {
  switch (c) {
    case '&':
      emit("&amp;");
      break;
    case '<':
      emit("&lt;");
      break;
    case '>':
      emit("&gt;");
      break;
    case '"':
      emit("&quot;");
      break;
    default:
      emit(&c, 1);
      break;
  }
}

void RecoveringHtmlStream::entity(const char* bytes, size_t size) {
  if (const char* decoded = lookupHtmlEntity(bytes, size)) {
    for (; *decoded; ++decoded) escaped(*decoded);
    return;
  }
  // Numeric references remain references (not markup). Invalid Unicode values
  // become a visible replacement glyph, never a fatal XML error or silent drop.
  if (size >= 4 && bytes[1] == '#' && bytes[size - 1] == ';') {
    size_t i = 2;
    const bool hex = bytes[i] == 'x' || bytes[i] == 'X';
    if (hex) ++i;
    const size_t first = i;
    uint32_t cp = 0;
    bool valid = true;
    for (; i + 1 < size; ++i) {
      const char c = lower(bytes[i]);
      const unsigned n = digit(c) ? c - '0' : (hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : 99);
      const unsigned base = hex ? 16 : 10;
      if (n >= base || cp > (0x10FFFFU - n) / base) {
        valid = false;
        break;
      }
      cp = cp * base + n;
    }
    if (valid && i > first) {
      if (xmlCharacter(cp))
        emit(bytes, size);
      else
        emit("\xEF\xBF\xBD");
      return;
    }
  }
  // Unknown names and broken references stay visible, including their '&'.
  for (size_t i = 0; i < size; ++i) escaped(bytes[i]);
}
void RecoveringHtmlStream::flushEntity() {
  if (entitySize_) entity(entity_, entitySize_);
  entitySize_ = 0;
}
void RecoveringHtmlStream::text(char c) {
  if (entitySize_) {
    if (c == ';') {
      entity_[entitySize_++] = c;
      flushEntity();
      return;
    }
    if ((alpha(c) || digit(c) || c == '#') && entitySize_ + 1 < sizeof(entity_)) {
      entity_[entitySize_++] = c;
      return;
    }
    flushEntity();
  }
  if (c == '&')
    entity_[entitySize_++] = c;
  else
    escaped(c);
}
void RecoveringHtmlStream::value(const char* s, size_t size) {
  // Attribute values share entity semantics without sharing the text accumulator.
  for (size_t i = 0; i < size;) {
    if (s[i] == '&') {
      size_t end = i + 1;
      while (end < size && end - i < sizeof(entity_) - 1 && (alpha(s[end]) || digit(s[end]) || s[end] == '#')) ++end;
      if (end < size && s[end] == ';') {
        entity(s + i, end + 1 - i);
        i = end + 1;
        continue;
      }
    }
    escaped(s[i++]);
  }
}
void RecoveringHtmlStream::closeTo(size_t target) {
  while (depth_ > target) {
    emit("</");
    emit(names_[--depth_]);
    emit(">");
  }
}
void RecoveringHtmlStream::tokenLiteral() {
  for (size_t i = 0; i < tokenSize_; ++i) text(token_[i]);
  tokenSize_ = 0;
  quote_ = 0;
}

void RecoveringHtmlStream::tag() {
  size_t i = 1;
  const bool closing = token_[i] == '/';
  if (closing) ++i;
  const size_t start = i;
  while (i < tokenSize_ && nameChar(token_[i])) ++i;
  const size_t length = i - start;
  if (!length || length >= NAME_BYTES || (!alpha(token_[start]) && token_[start] != '_')) {
    tokenLiteral();
    return;
  }
  char name[NAME_BYTES] = {};
  for (size_t n = 0; n < length; ++n) name[n] = lower(token_[start + n]);
  if (rawName_[0]) {
    if ((closing && strcmp(rawName_, name) == 0) || (strcmp(rawName_, "head") == 0 && strcmp(name, "body") == 0)) {
      rawName_[0] = 0;
    }
    return;
  }
  if (in(name, "html body")) return;  // The adapter owns the document envelope.
  if (!closing && in(name, "head script style")) {
    memcpy(rawName_, name, sizeof(name));
    return;
  }
  if (closing) {
    for (size_t d = depth_; d > 0; --d) {
      if (strcmp(names_[d - 1], name) == 0) {
        closeTo(d - 1);
        break;
      }
    }
    return;  // Stray end tags cannot prevent the next paragraph from being read.
  }
  // Common optional end tags. These bounded scope walks avoid closing a cell or
  // list item belonging to a parent table/list when the nested one starts.
  const bool block = in(name, "p div h1 h2 h3 h4 h5 h6 blockquote ul ol table hr section article");
  for (size_t d = depth_; d > 0; --d) {
    const char* open = names_[d - 1];
    if ((block && strcmp(open, "p") == 0) || (strcmp(name, "li") == 0 && strcmp(open, "li") == 0) ||
        (in(name, "td th") && in(open, "td th")) || (strcmp(name, "tr") == 0 && strcmp(open, "tr") == 0)) {
      closeTo(d - 1);
      break;
    }
    if (in(open, "table ul ol")) break;
  }
  // At extreme depth flatten markup, not text; the output stack stays bounded.
  if (depth_ == MAX_DEPTH) return;
  emit("<");
  emit(name);
  size_t attributeCount = 0;
  while (i + 1 < tokenSize_ && ok_) {
    while (i < tokenSize_ && space(token_[i])) ++i;
    if (i >= tokenSize_ || token_[i] == '/' || token_[i] == '>') break;
    const size_t a = i;
    while (i < tokenSize_ && nameChar(token_[i])) ++i;
    const size_t aSize = i - a;
    if (!aSize) {
      ++i;
      continue;
    }
    const bool validName = aSize < NAME_BYTES && (alpha(token_[a]) || token_[a] == '_');
    // Reuse the next attribute slot only for a valid, nonduplicate name.
    bool duplicate = !validName || attributeCount == MAX_ATTRIBUTES;
    if (!duplicate) {
      auto& dest = attributes_[attributeCount];
      for (size_t n = 0; n < aSize; ++n) dest[n] = lower(token_[a + n]);
      dest[aSize] = 0;
      for (size_t n = 0; n < attributeCount; ++n)
        if (strcmp(dest, attributes_[n]) == 0) duplicate = true;
    }
    while (i < tokenSize_ && space(token_[i])) ++i;
    size_t v = i, vSize = 0;
    if (i < tokenSize_ && token_[i] == '=') {
      ++i;
      while (i < tokenSize_ && space(token_[i])) ++i;
      const char q = i < tokenSize_ && (token_[i] == '\'' || token_[i] == '"') ? token_[i++] : 0;
      v = i;
      while (i < tokenSize_ && (q ? token_[i] != q : !space(token_[i]) && token_[i] != '>')) ++i;
      vSize = i - v;
      if (q && i < tokenSize_) ++i;
      if (!q && vSize && token_[v + vSize - 1] == '/' && i + 1 == tokenSize_) --vSize;
    }
    if (!duplicate) {
      emit(" ");
      emit(attributes_[attributeCount++]);
      emit("=\"");
      value(token_ + v, vSize);
      emit("\"");
    }
  }
  const bool empty = in(name, "area base br col embed hr img input link meta param source track wbr") ||
                     (tokenSize_ > 1 && token_[tokenSize_ - 2] == '/');
  if (empty)
    emit("/>");
  else {
    emit(">");
    memcpy(names_[depth_++], name, sizeof(name));
  }
}

void RecoveringHtmlStream::consume(char c) {
  if (mode_ == Mode::Comment) {
    if (c == '>' && tail_ >= 2) {
      mode_ = Mode::Text;
      tail_ = 0;
    } else
      tail_ = c == '-' ? (tail_ < 2 ? tail_ + 1 : 2) : 0;
    return;
  }
  if (mode_ == Mode::Cdata) {
    if (c == ']') {
      if (tail_ == 2)
        escaped(']');
      else
        ++tail_;
    } else if (c == '>' && tail_ == 2) {
      tail_ = 0;
      mode_ = Mode::Text;
    } else {
      while (tail_) {
        escaped(']');
        --tail_;
      }
      escaped(c);
    }
    return;
  }
  if (mode_ == Mode::Declaration) {
    if (quote_) {
      if (c == quote_) quote_ = 0;
    } else if (c == '"' || c == '\'')
      quote_ = c;
    else if (c == '[')
      ++declarationDepth_;
    else if (c == ']' && declarationDepth_)
      --declarationDepth_;
    else if (c == '>' && !declarationDepth_)
      mode_ = Mode::Text;
    return;
  }
  if (mode_ == Mode::Text || mode_ == Mode::Raw) {
    if (c == '<') {
      flushEntity();
      tokenSize_ = 0;
      token_[tokenSize_++] = c;
      mode_ = Mode::Tag;
      quote_ = 0;
    } else if (!rawName_[0])
      text(c);
    return;
  }
  // A long or unfinished markup token is rendered literally instead of retaining
  // an unbounded buffer or consuming the remainder of a chapter as one tag.
  if (tokenSize_ + 1 >= sizeof(token_)) {
    if (!rawName_[0])
      tokenLiteral();
    else
      tokenSize_ = 0;
    mode_ = rawName_[0] ? Mode::Raw : Mode::Text;
    consume(c);
    return;
  }
  if (tokenSize_ == 1 && !alpha(c) && c != '/' && c != '!' && c != '?' && c != '_') {
    if (!rawName_[0]) tokenLiteral();
    mode_ = rawName_[0] ? Mode::Raw : Mode::Text;
    consume(c);
    return;
  }
  if (c == '<' && !quote_) {
    if (!rawName_[0]) tokenLiteral();
    tokenSize_ = 0;
    token_[tokenSize_++] = c;
    return;
  }
  token_[tokenSize_++] = c;
  token_[tokenSize_] = 0;
  if (tokenSize_ == 4 && memcmp(token_, "<!--", 4) == 0 && !rawName_[0]) {
    mode_ = Mode::Comment;
    tail_ = 0;
    tokenSize_ = 0;
    return;
  }
  if (tokenSize_ == 9 && memcmp(token_, "<![CDATA[", 9) == 0 && !rawName_[0]) {
    mode_ = Mode::Cdata;
    tail_ = 0;
    tokenSize_ = 0;
    return;
  }
  if (tokenSize_ >= 2 && (token_[1] == '!' || token_[1] == '?') &&
      !(tokenSize_ <= 9 &&
        (strncmp("<!--", token_, tokenSize_) == 0 || strncmp("<![CDATA[", token_, tokenSize_) == 0))) {
    mode_ = Mode::Declaration;
    quote_ = 0;
    declarationDepth_ = 0;
    // Reprocess the declaration prefix so quotes/brackets have correct scope.
    for (size_t n = 2; n < tokenSize_; ++n) consume(token_[n]);
    tokenSize_ = 0;
    return;
  }
  if (quote_) {
    if (c == quote_) quote_ = 0;
  } else if (c == '\'' || c == '"')
    quote_ = c;
  else if (c == '>') {
    tag();
    tokenSize_ = 0;
    mode_ = rawName_[0] ? Mode::Raw : Mode::Text;
  }
}

bool RecoveringHtmlStream::write(const char* bytes, size_t size, bool final) {
  if (finished_ || !ok_) return false;
  if (!started_) {
    emit("<html><body>");
    started_ = true;
  }
  // Validate UTF-8 before tokenization, carrying incomplete sequences across
  // chunks. Preserve every valid Unicode character; replace corrupt bytes only.
  for (size_t i = 0; i < size && ok_; ++i) {
    const auto c = static_cast<uint8_t>(bytes[i]);
    if (utf8Expected_) {
      if ((c & 0xC0) == 0x80) {
        utf8_[utf8Size_++] = bytes[i];
        codepoint_ = (codepoint_ << 6) | (c & 0x3F);
        if (utf8Size_ == utf8Expected_) {
          const uint32_t min = utf8Expected_ == 2 ? 0x80 : utf8Expected_ == 3 ? 0x800 : 0x10000;
          if (codepoint_ >= min && xmlCharacter(codepoint_))
            for (size_t n = 0; n < utf8Size_; ++n) consume(utf8_[n]);
          else {
            consume('\xEF');
            consume('\xBF');
            consume('\xBD');
          }
          utf8Expected_ = utf8Size_ = 0;
        }
        continue;
      }
      consume('\xEF');
      consume('\xBF');
      consume('\xBD');
      utf8Expected_ = utf8Size_ = 0;
      // The non-continuation byte still belongs to the source; process it below.
    }
    if (c < 0x80) {
      if (xmlCharacter(c))
        consume(bytes[i]);
      else {
        consume('\xEF');
        consume('\xBF');
        consume('\xBD');
      }
    } else if (c >= 0xC2 && c <= 0xF4) {
      utf8Expected_ = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
      utf8_[0] = bytes[i];
      utf8Size_ = 1;
      codepoint_ = c & (utf8Expected_ == 2 ? 0x1F : utf8Expected_ == 3 ? 0x0F : 7);
    } else {
      consume('\xEF');
      consume('\xBF');
      consume('\xBD');
    }
  }
  if (final && ok_) {
    if (utf8Expected_) {
      consume('\xEF');
      consume('\xBF');
      consume('\xBD');
    }
    if (mode_ == Mode::Tag && !rawName_[0]) tokenLiteral();
    if (mode_ == Mode::Cdata)
      while (tail_) {
        escaped(']');
        --tail_;
      }
    flushEntity();
    closeTo(0);
    emit("</body></html>");
    finished_ = true;
  }
  return flush();
}
