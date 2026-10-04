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
  auto& target = screen.target();
  screen.insetContent(fui::Insets{theme.spaceSm, theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int gap = std::max<int>(12, theme.spaceSm);
  const int bodyHeight = target.lineHeight(theme.bodyText.font);
  const int smallHeight = target.lineHeight(theme.smallText.font);
  const auto black = fui::Paint::solid(fui::Color::Black);
  RickyPageUi::Bold bold(renderer, 1);
  auto small = theme.smallText;
  small.maxLines = 1;
  auto strong = small;
  strong.bold = true;
  const int selected = RickyPageUi::syncNav(nav, listCount());
  const bool showSelection = showMainTabContentSelection();

  auto title = screen.takeTop(target.lineHeight(theme.titleText.font), gap * 2);
  auto device = title;
  device.x += title.width * 2 / 3;
  device.width = title.width / 3;
  auto right = small;
  right.align = fui::TextAlign::Right;
  target.text(device, tr(STR_SD_CARD), right);
  title.width -= device.width + gap;
  auto pageTitle = theme.titleText;
  pageTitle.bold = true;
  {
    RickyPageUi::Bold heading(renderer, 2);
    target.text(title, tr(STR_RICKY_STORAGE), pageTitle);
  }

  // Fixed parts first; leftover height widens cells, rows and section gaps
  // together so the page fills evenly instead of stacking at the top.
  const int iconSize = smallHeight;
  const bool wide = content.width > content.height;
  const int columns = wide ? 4 : 2;
  const int gridRows = 4 / columns;
  const int cellBase = iconSize + gap + bodyHeight + gap * 2;
  const int rowBase = bodyHeight + gap * 2;
  const int needed = cellBase * gridRows + gap * 2 * (gridRows - 1) + rowBase * 2 + gap * 2;
  const int extra = std::clamp((screen.body().height - needed) / 6, 0, gap * 3);
  const int section = gap * 2 + extra;

  if (folderMissing) {
    auto warning = small;
    warning.maxLines = 2;
    target.text(screen.takeBottom(smallHeight * 2, gap), tr(STR_RICKY_FOLDER_MISSING), warning);
  }

  // Four content folders: icon and name over a hairline.
  const StrId labels[] = {StrId::STR_RICKY_BOOK_FILES, StrId::STR_FONT, StrId::STR_RICKY_IMAGES,
                          StrId::STR_RICKY_DOWNLOADS};
  constexpr RickyPageUi::Icon icons[] = {RickyPageUi::Icon::Book, RickyPageUi::Icon::Font, RickyPageUi::Icon::Image,
                                         RickyPageUi::Icon::Download};
  const int cellHeight = cellBase + extra;
  const auto grid = screen.takeTop(cellHeight * gridRows + gap * 2 * (gridRows - 1), section);
  for (int i = 0; i < 4; ++i) {
    const auto rect = RickyPageLayout::cell(Rect{grid.x, grid.y, grid.width, grid.height}, i, 4, gap * 2, columns);
    const auto box = RickyPageUi::uiRect(rect);
    screen.frame().hit(box, ACTION_ROW, i, fui::InputTouch);
    const bool active = showSelection && selected == i;
    const int top = rect.y + extra / 2;
    RickyPageUi::icon(target, Rect{rect.x, top, iconSize, iconSize}, icons[i]);
    auto label = theme.bodyText;
    label.maxLines = 1;
    label.bold = active;
    target.text(RickyPageUi::uiRect(Rect{rect.x, top + iconSize + gap, rect.width, bodyHeight}), I18N.get(labels[i]),
                label);
    target.fill(fui::Rect{box.x, static_cast<int16_t>(box.bottom() - (active ? 3 : 1)), box.width,
                          static_cast<int16_t>(active ? 3 : 1)},
                black);
  }

  // Whole-card actions as list rows with a chevron, divided by hairlines.
  const int rowHeight = rowBase + extra;
  for (int i = 4; i < 6; ++i) {
    const auto rect = screen.takeTop(rowHeight, 0);
    screen.frame().hit(rect, ACTION_ROW, i, fui::InputTouch);
    const int top = rect.y + (rect.height - iconSize) / 2;
    RickyPageUi::icon(target, Rect{rect.x, top, iconSize, iconSize},
                      i == 4 ? RickyPageUi::Icon::Folder : RickyPageUi::Icon::Upload);
    auto text = theme.bodyText;
    text.maxLines = 1;
    text.bold = showSelection && selected == i;
    target.text(fui::Rect{static_cast<int16_t>(rect.x + iconSize + gap),
                          static_cast<int16_t>(rect.y + (rect.height - bodyHeight) / 2),
                          static_cast<int16_t>(rect.width - iconSize - gap * 3), static_cast<int16_t>(bodyHeight)},
                i == 4 ? tr(STR_RICKY_BROWSE_FILES) : tr(STR_RICKY_UPLOAD_FILES), text);
    RickyPageUi::chevron(target, fui::Rect{static_cast<int16_t>(rect.right() - 24), rect.y, 24, rect.height});
    if (i == 4) target.fill(fui::Rect{rect.x, static_cast<int16_t>(rect.bottom() - 1), rect.width, 1}, black);
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
