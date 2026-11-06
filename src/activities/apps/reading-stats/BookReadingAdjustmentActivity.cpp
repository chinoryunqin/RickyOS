#include "BookReadingAdjustmentActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

#include "AchievementsStore.h"
#include "ReadingDateSelectionActivity.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "util/ReadingStatsAnalytics.h"
#include "util/TimeUtils.h"

namespace {
constexpr int FIELD_COUNT = 3;
constexpr int OPERATION_COUNT = 2;
constexpr int DURATION_COUNT = 4;
constexpr int64_t MINUTES_TO_MS = 60LL * 1000LL;
constexpr int DURATION_MINUTES[DURATION_COUNT] = {15, 30, 45, 60};

int wrapIndex(const int value, const int delta, const int count) {
  int next = value + delta;
  next %= count;
  if (next < 0) {
    next += count;
  }
  return next;
}
}  // namespace

void BookReadingAdjustmentActivity::onEnter() {
  RickyStatsEditor::onEnter();
  selectedField = 0;
  selectedOperation = 0;
  selectedDuration = 1;
  lastApplyFailed = false;
  initializeSelectedDate();
  requestUpdate();
}

void BookReadingAdjustmentActivity::adjustSelectedValue(const int delta) {
  lastApplyFailed = false;
  if (selectedField == 0) {
    selectedOperation = wrapIndex(selectedOperation, delta, OPERATION_COUNT);
  } else if (selectedField == 1) {
    if (selectedDayOrdinal != 0 || delta > 0) {
      selectedDayOrdinal = static_cast<uint32_t>(static_cast<int32_t>(selectedDayOrdinal) + delta);
    }
  } else {
    selectedDuration = wrapIndex(selectedDuration, delta, DURATION_COUNT);
  }
  requestUpdate();
}

void BookReadingAdjustmentActivity::initializeSelectedDate() {
  bool usedFallback = false;
  const uint32_t referenceTimestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
  if (!TimeUtils::isClockValid(referenceTimestamp)) {
    selectedDayOrdinal = 0;
    return;
  }

  selectedDayOrdinal = TimeUtils::getLocalDayOrdinal(referenceTimestamp);
}

void BookReadingAdjustmentActivity::openDateSelection() {
  startActivityForResultWith<ReadingDateSelectionActivity>(
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          if (const auto* page = std::get_if<PageResult>(&result.data)) {
            selectedDayOrdinal = page->page;
            lastApplyFailed = false;
          }
        }
        guardEntryRelease();
        requestUpdate();
      },
      selectedDayOrdinal);
}

int32_t BookReadingAdjustmentActivity::getSelectedDeltaMs() const {
  int32_t deltaMs = static_cast<int32_t>(DURATION_MINUTES[selectedDuration] * MINUTES_TO_MS);
  if (selectedOperation == 1) {
    deltaMs = -deltaMs;
  }
  return deltaMs;
}

uint64_t BookReadingAdjustmentActivity::getSelectedDayReadingMs() const {
  if (selectedDayOrdinal == 0) {
    return 0;
  }

  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return 0;
  }

  for (const auto& day : book->readingDays) {
    if (day.dayOrdinal == selectedDayOrdinal) {
      return day.readingMs;
    }
    if (day.dayOrdinal > selectedDayOrdinal) {
      break;
    }
  }
  return 0;
}

bool BookReadingAdjustmentActivity::canApplySelectedAdjustment() const {
  if (selectedDayOrdinal == 0 || READING_STATS.findBook(bookPath) == nullptr) {
    return false;
  }

  const int32_t deltaMs = getSelectedDeltaMs();
  if (deltaMs >= 0) {
    return true;
  }

  return getSelectedDayReadingMs() >= static_cast<uint64_t>(-deltaMs);
}

std::string BookReadingAdjustmentActivity::getAdjustmentPreviewInfo() const {
  const auto* book = READING_STATS.findBook(bookPath);
  if (book == nullptr) {
    return tr(STR_BOOK_NOT_FOUND);
  }
  if (selectedDayOrdinal == 0) {
    return tr(STR_SET_DATE_BEFORE_APPLYING);
  }

  const uint64_t currentMs = getSelectedDayReadingMs();
  const int32_t deltaMs = getSelectedDeltaMs();
  const std::string dayTotal = std::string(tr(STR_DAY_TOTAL)) + ": ";
  if (deltaMs < 0) {
    const uint64_t removeMs = static_cast<uint64_t>(-deltaMs);
    if (currentMs < removeMs) {
      return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " (" + tr(STR_NOT_ENOUGH) + ")";
    }
    return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
           ReadingStatsAnalytics::formatDurationHm(currentMs - removeMs);
  }

  return dayTotal + ReadingStatsAnalytics::formatDurationHm(currentMs) + " -> " +
         ReadingStatsAnalytics::formatDurationHm(currentMs + static_cast<uint64_t>(deltaMs));
}

const char* BookReadingAdjustmentActivity::getOperationLabel() const {
  return selectedOperation == 0 ? tr(STR_ADD) : tr(STR_SUBTRACT);
}

std::string BookReadingAdjustmentActivity::getDateLabel() const {
  int selectedYear = 0;
  unsigned selectedMonth = 0;
  unsigned selectedDay = 0;
  if (selectedDayOrdinal == 0 ||
      !TimeUtils::getDateFromDayOrdinal(selectedDayOrdinal, selectedYear, selectedMonth, selectedDay)) {
    return tr(STR_NOT_SET);
  }
  return TimeUtils::formatDateParts(selectedYear, selectedMonth, selectedDay);
}

const char* BookReadingAdjustmentActivity::getDurationLabel() const {
  static char label[48];
  snprintf(label, sizeof(label), tr(STR_RICKY_STATS_MINUTES),
           static_cast<unsigned>(DURATION_MINUTES[selectedDuration]));
  return label;
}

bool BookReadingAdjustmentActivity::applyAdjustment() {
  const uint32_t dayOrdinal = selectedDayOrdinal;
  const int32_t deltaMs = getSelectedDeltaMs();

  if (!READING_STATS.adjustBookReadingTime(bookPath, dayOrdinal, deltaMs)) {
    lastApplyFailed = true;
    requestUpdate();
    return false;
  }

  ACHIEVEMENTS.rebuildProgressFromCurrentStats();
  finish();
  return true;
}

const char* BookReadingAdjustmentActivity::headerTitle() const { return tr(STR_ADJUST_READING_TIME); }

const char* BookReadingAdjustmentActivity::fieldLabel(int index) const {
  static constexpr StrId labels[] = {StrId::STR_ACTION, StrId::STR_DATE, StrId::STR_AMOUNT};
  return I18N.get(labels[index]);
}

std::string BookReadingAdjustmentActivity::fieldValue(int index) const {
  if (index == 0) return getOperationLabel();
  if (index == 1) return getDateLabel();
  return getDurationLabel();
}

const char* BookReadingAdjustmentActivity::confirmLabel() const {
  return selectedField == 1 ? tr(STR_SELECT) : tr(STR_CONFIRM);
}

void BookReadingAdjustmentActivity::activateField() {
  if (selectedField == 1)
    openDateSelection();
  else
    applyAdjustment();
}

void BookReadingAdjustmentActivity::buildDetails(RickyStatsPage& page) {
  const std::string info = getAdjustmentPreviewInfo();
  const char* hint = selectedField == 1 ? tr(STR_SELECT_OPENS_DATE_PICKER) : tr(STR_SELECT_APPLIES_CORRECTION);
  if (lastApplyFailed)
    hint = tr(STR_COULD_NOT_APPLY_CORRECTION);
  else if (!canApplySelectedAdjustment())
    hint = tr(STR_CHOOSE_ADD_OR_REDUCE_AMOUNT);
  const auto infoArea = page.take(page.measure(info.c_str(), RickyDesignUi::BODY, page.viewport.width));
  RickyDesignUi::text(renderer, infoArea, info.c_str(), RickyDesignUi::BODY, false, freeink::ui::TextAlign::Left, 16);
  const auto hintArea = page.take(page.measure(hint, RickyDesignUi::CAPTION, page.viewport.width));
  RickyDesignUi::secondaryText(renderer, hintArea, hint, RickyDesignUi::CAPTION, false, freeink::ui::TextAlign::Left,
                               16);
}
