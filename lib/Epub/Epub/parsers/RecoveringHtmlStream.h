#pragma once

#include <cstddef>
#include <cstdint>

// Bounded HTML-to-XML recovery for damaged chapter markup. Only used after the
// strict parser fails. No DOM, chapter-sized strings, DTD or network resolution.
class RecoveringHtmlStream {
 public:
  using Sink = bool (*)(void*, const char*, size_t);
  RecoveringHtmlStream(Sink sink, void* context) : sink_(sink), context_(context) {}
  bool write(const char* bytes, size_t size, bool final);

 private:
  static constexpr size_t TOKEN_BYTES = 2048;
  static constexpr size_t NAME_BYTES = 32;
  static constexpr size_t MAX_DEPTH = 32;
  static constexpr size_t MAX_ATTRIBUTES = 24;
  enum class Mode : uint8_t { Text, Tag, Comment, Declaration, Cdata, Raw };
  Sink sink_;
  void* context_;
  Mode mode_ = Mode::Text;
  char token_[TOKEN_BYTES] = {};
  size_t tokenSize_ = 0;
  char names_[MAX_DEPTH][NAME_BYTES] = {};
  size_t depth_ = 0;
  char attributes_[MAX_ATTRIBUTES][NAME_BYTES] = {};
  char rawName_[NAME_BYTES] = {};
  char entity_[40] = {};
  size_t entitySize_ = 0;
  char output_[512] = {};
  size_t outputSize_ = 0;
  char utf8_[4] = {};
  uint8_t utf8Size_ = 0, utf8Expected_ = 0;
  uint32_t codepoint_ = 0;
  char quote_ = 0;
  unsigned declarationDepth_ = 0;
  unsigned tail_ = 0;
  bool started_ = false, finished_ = false, ok_ = true;
  bool flush();
  void emit(const char* bytes, size_t size);
  void emit(const char* text);
  void escaped(char byte);
  void text(char byte);
  void flushEntity();
  void entity(const char* bytes, size_t size);
  void value(const char* bytes, size_t size);
  void consume(char byte);
  void tokenLiteral();
  void tag();
  void closeTo(size_t depth);
};
