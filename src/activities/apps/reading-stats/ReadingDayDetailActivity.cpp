#include "ReadingDayDetailActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>
#include <string>

#include "ReadingStatsDetailActivity.h"
#include "components/RickyDesignUi.h"
#include "components/RickyStatsLayout.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

ReadingDayDetailActivity::ReadingDayDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   const uint32_t dayOrdinal)
    : UiListActivity("ReadingDayDetail", renderer, mappedInput), dayOrdinal(dayOrdinal) {}

void ReadingDayDetailActivity::refreshEntries() {
  entries = ReadingStatsAnalytics::getBooksReadOnDay(dayOrdinal);
  nav.selected = std::min<int>(nav.selected, std::max(0, listCount() - 1));
  nav.scrollBy(0, listCount());
  nav.follow(listCount());

  rowValues.clear();
  rowValues.reserve(entries.size());
  std::transform(entries.begin(), entries.end(), std::back_inserter(rowValues),
                 [](const auto& entry) { return ReadingStatsAnalytics::formatDurationHm(entry.readingMs); });

  rowItems.clear();
  rowItems.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const ReadingBookStats* book = entries[i].book;
    fui::ListItem item;
    item.label = book ? (book->title.empty() ? book->path.c_str() : book->title.c_str()) : tr(STR_NOT_SET);
    item.subtitle = book ? (book->author.empty() ? tr(STR_IN_PROGRESS) : book->author.c_str()) : tr(STR_NOT_SET);
    item.icon = listIconFor(UIIcon::Book, 40);
    item.value = rowValues[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }

  dateLabel = ReadingStatsAnalytics::formatDayOrdinalLabel(dayOrdinal);
  const auto timeline = ReadingStatsAnalytics::buildTimelineDayEntry(dayOrdinal);
  totalReadingText = ReadingStatsAnalytics::formatDurationHm(entries.empty() ? 0 : timeline.totalReadingMs);
  bookCountText = std::to_string(entries.size());
}

void ReadingDayDetailActivity::openSelectedBook() {
  if (nav.selected < 0 || nav.selected >= listCount() || entries[nav.selected].book == nullptr) {
    return;
  }

  app.clearTapFlash();
  startActivityForResultWith<ReadingStatsDetailActivity>(
      [this](const ActivityResult&) {
        refreshEntries();
        requestUpdate();
      },
      entries[nav.selected].book->path);
}

void ReadingDayDetailActivity::onEnter() {
  UiListActivity::onEnter();
  refreshEntries();
}

void ReadingDayDetailActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  openSelectedBook();
}

const char* ReadingDayDetailActivity::headerTitle() const { return tr(STR_READING_DAY); }

void ReadingDayDetailActivity::buildScreen(UiScreen& screen) {
  namespace ds = ricky::ds;
  namespace design = RickyDesignUi;
  const auto bounds = pageContentRect();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int left = std::max<int>(ds::space::kPageMargin, safe.x);
  const int right = std::min<int>(renderer.getScreenWidth() - ds::space::kPageMargin, safe.x + safe.width);
  const int top = bounds.y + ds::space::kBlockGap;
  const int bottom = std::min(bounds.y + bounds.height, safe.y + safe.height) - ds::space::k4;
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(top), static_cast<int16_t>(renderer.getScreenWidth() - right),
                  static_cast<int16_t>(renderer.getScreenHeight() - bottom), static_cast<int16_t>(left)});
  const int gap = ds::space::kGutter;
  const int pad = ds::space::kCardPadding;
  const int captionHeight = std::max(renderer.getLineHeight(design::CAPTION), renderer.getLineHeight(design::BODY));
  const int valueHeight = std::max(renderer.getLineHeight(design::HEADLINE), renderer.getLineHeight(design::BODY));
  const auto date = screen.takeTop(captionHeight, ds::space::kBlockGap);
  design::secondaryText(renderer, date, dateLabel.c_str());
  const int width = (screen.body().width - gap) / 2;
  const StrId labels[] = {StrId::STR_TOTAL_TIME, StrId::STR_BOOKS_READ};
  const char* values[] = {totalReadingText.c_str(), bookCountText.c_str()};
  RickyUiTarget measureTarget(renderer);
  fui::TextStyle labelStyle;
  labelStyle.font = fui::GfxRendererTarget::FONT_BODY;
  labelStyle.maxLines = 2;
  int labelHeight = captionHeight;
  for (const auto label : labels) {
    const char* text = I18N.get(label);
    measureTarget.setFont(labelStyle.font,
                          renderer.textFontId(design::fontFor(renderer, design::CAPTION, text), text));
    labelHeight = std::max<int>(
        labelHeight, fui::measureWrappedText(measureTarget, text, labelStyle, std::max(1, width - pad * 2)).height);
  }
  const int cardHeight = RickyStatsLayout::summaryCardHeight(valueHeight, labelHeight);
  const auto cards = screen.takeTop(cardHeight, ds::space::kBlockGap);
  for (int i = 0; i < 2; ++i) {
    const fui::Rect card{static_cast<int16_t>(cards.x + i * (width + gap)), cards.y, static_cast<int16_t>(width),
                         cards.height};
    design::softCard(renderer, card, false);
    design::text(renderer,
                 {static_cast<int16_t>(card.x + pad), static_cast<int16_t>(card.y + pad),
                  static_cast<int16_t>(width - pad * 2), static_cast<int16_t>(valueHeight)},
                 values[i], design::HEADLINE);
    design::secondaryText(renderer,
                          {static_cast<int16_t>(card.x + pad),
                           static_cast<int16_t>(card.y + pad + valueHeight + ds::space::kTextStackGap),
                           static_cast<int16_t>(width - pad * 2), static_cast<int16_t>(labelHeight)},
                          I18N.get(labels[i]), design::CAPTION, false, fui::TextAlign::Left, 2);
  }
  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_READING_DAY), screen.theme().bodyText);
    return;
  }
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = ds::space::kInlineGap;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void ReadingDayDetailActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
