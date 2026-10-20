#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "TestSupport.h"
#include "UserGuide.h"

int main() {
  // Fresh cards, repeated home entries, both languages and resets never seed.
  for (bool loaded : {false, true}) {
    for (bool chinese : {false, true}) {
      for (int boot = 0; boot < 3; ++boot) {
        UserGuide::prepare(loaded);
        UserGuide::installIfPending(chinese);
        UserGuide::installIfPending(!chinese);
        assert(guide_test::files.empty());
        assert(RECENT_BOOKS.books.empty());
        assert(RECENT_BOOKS.persisted.empty());
      }
    }
  }

  // Never delete, replace or re-register existing books, guides or corrupt state.
  guide_test::files = {
      {"/CrossMux用户手册.epub", {'u', 's', 'e', 'r'}}, {"/CrossMux User Guide.epub", {'b', 'o', 'o', 'k'}},
      {"/books/my.epub", {'e', 'p', 'u', 'b'}},         {"/.crosspoint/recent.json", {'b', 'a', 'd'}},
      {"/.crosspoint/user-guide.checked", {'1', '\n'}}, {"/.crosspoint/user-guide.epub.tmp", {'t', 'm', 'p'}}};
  RECENT_BOOKS.books.push_back({"/books/my.epub", "My book", "Author", "cover"});
  RECENT_BOOKS.persisted = RECENT_BOOKS.books;
  const auto before = guide_test::files;
  for (bool loaded : {false, true}) {
    UserGuide::prepare(loaded);
    UserGuide::installIfPending(true);
    UserGuide::installIfPending(false);
    assert(guide_test::files == before);
    assert(RECENT_BOOKS.books.size() == 1);
    assert(RECENT_BOOKS.persisted.size() == 1);
    assert(RECENT_BOOKS.books.front().path == "/books/my.epub");
    assert(RECENT_BOOKS.persisted.front().path == "/books/my.epub");
  }
}
