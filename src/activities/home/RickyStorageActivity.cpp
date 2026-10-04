#include "RickyStorageActivity.h"
#ifdef RICKYOS_PRODUCT
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "FileBrowserActivity.h"
#include "activities/settings/FontLibraryActivity.h"
#include "components/RickyPageUi.h"
#include "components/UITheme.h"

void RickyStorageActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  drawPageHeader(Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, tr(STR_RICKY_STORAGE));
}
void RickyStorageActivity::drawFooter() {
  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_OPEN), true);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
void RickyStorageActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto content = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(content.y), static_cast<int16_t>(content.x),
                  static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height),
                  static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width)});
  const auto& theme = screen.theme();
  screen.insetContent(fui::Insets{theme.spaceSm, theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(6, theme.spaceLg);
  const int lineHeight = screen.target().lineHeight(theme.bodyText.font);
  auto title = screen.takeTop(screen.target().lineHeight(theme.titleText.font), gap);
  auto device = title;
  device.x += title.width * 2 / 3;
  device.width = title.width / 3;
  auto right = theme.smallText;
  right.align = fui::TextAlign::Right;
  screen.target().text(device, tr(STR_SD_CARD), right);
  title.width -= device.width + gap;
  screen.target().text(title, tr(STR_RICKY_STORAGE), theme.titleText);
  const int selected = RickyPageUi::syncNav(nav, listCount());
  if (folderMissing) {
    auto warning = theme.smallText;
    warning.maxLines = 2;
    screen.target().text(screen.takeBottom(screen.target().lineHeight(warning.font) * 2, gap),
                         tr(STR_RICKY_FOLDER_MISSING), warning);
  }
  const auto transfer = screen.takeBottom(lineHeight + gap * 2, 0);
  const auto allFiles = screen.takeBottom(lineHeight + gap * 2, gap);
  const auto body = screen.body();
  const StrId labels[] = {StrId::STR_RICKY_BOOK_FILES, StrId::STR_FONT, StrId::STR_RICKY_IMAGES,
                          StrId::STR_RICKY_DOWNLOADS};
  constexpr RickyPageUi::Icon icons[] = {RickyPageUi::Icon::Book, RickyPageUi::Icon::Font, RickyPageUi::Icon::Image,
                                         RickyPageUi::Icon::Download};
  for (int i = 0; i < 4; ++i) {
    const auto rect = RickyPageLayout::cell(Rect{body.x, body.y, body.width, body.height}, i, 4, gap,
                                            content.width > content.height ? 4 : 2);
    const auto box = RickyPageUi::uiRect(rect);
    screen.frame().hit(box, ACTION_ROW, i, fui::InputTouch);
    const int size = std::min(lineHeight, rect.height / 4);
    const int top = rect.y + std::max(6, (rect.height - size - lineHeight - gap / 2) / 2);
    RickyPageUi::icon(screen.target(), Rect{rect.x, top, size, size}, icons[i]);
    auto label = theme.bodyText;
    label.bold = showMainTabContentSelection() && selected == i;
    screen.target().text(RickyPageUi::uiRect(Rect{rect.x, top + size + gap / 2, rect.width, lineHeight}),
                         I18N.get(labels[i]), label);
    screen.target().fill(fui::Rect{box.x, static_cast<int16_t>(box.bottom() - 2), box.width, 2},
                         fui::Paint::dither(fui::Color::DarkGray));
  }
  for (int i = 4; i < 6; ++i) {
    const auto rect = i == 4 ? allFiles : transfer;
    screen.frame().hit(rect, ACTION_ROW, i, fui::InputTouch);
    const int top = rect.y + (rect.height - lineHeight) / 2;
    RickyPageUi::icon(screen.target(), Rect{rect.x, top, lineHeight, lineHeight},
                      i == 4 ? RickyPageUi::Icon::Folder : RickyPageUi::Icon::Upload);
    auto text = theme.bodyText;
    text.bold = showMainTabContentSelection() && selected == i;
    screen.target().text(
        fui::Rect{static_cast<int16_t>(rect.x + lineHeight + gap), static_cast<int16_t>(top),
                  static_cast<int16_t>(rect.width - lineHeight - gap * 2), static_cast<int16_t>(lineHeight)},
        i == 4 ? tr(STR_RICKY_BROWSE_FILES) : tr(STR_RICKY_UPLOAD_FILES), text);
    const auto black = fui::Paint::solid(fui::Color::Black);
    screen.target().line({static_cast<int16_t>(rect.right() - 12), static_cast<int16_t>(top + lineHeight / 2 - 6)},
                         {static_cast<int16_t>(rect.right() - 6), static_cast<int16_t>(top + lineHeight / 2)}, 2,
                         black);
    screen.target().line({static_cast<int16_t>(rect.right() - 6), static_cast<int16_t>(top + lineHeight / 2)},
                         {static_cast<int16_t>(rect.right() - 12), static_cast<int16_t>(top + lineHeight / 2 + 6)}, 2,
                         black);
    if (i == 4)
      screen.target().fill(fui::Rect{rect.x, static_cast<int16_t>(rect.bottom() - 2), rect.width, 2},
                           fui::Paint::dither(fui::Color::DarkGray));
  }
}
void RickyStorageActivity::activateIndex(int index) {
  app.clearTapFlash();
  folderMissing = false;
  if (index == 1) {
    startActivityForResultWith<FontLibraryActivity>([](const ActivityResult&) {});
  } else if (index == 5) {
    activityManager.goToFileTransfer();
  } else {
    // Existing cards may use any folder scheme: never create or move their files.
    const char* path = "/";
    if (index == 0) path = "/books";
    if (index == 2)
      path = Storage.exists("/images") ? "/images" : Storage.exists("/pictures") ? "/pictures" : "/AirPage";
    if (index == 3) path = Storage.exists("/downloads") ? "/downloads" : "/Downloads";
    if (index < 4 && !Storage.exists(path)) {
      folderMissing = true;
      requestUpdate();
      return;
    }
    startActivityForResultWith<FileBrowserActivity>([](const ActivityResult&) {}, path);
  }
}
#endif
