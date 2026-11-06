#include "ReadingDateSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "util/TimeUtils.h"

namespace {
constexpr int FIELD_COUNT = 3;
constexpr int MIN_DAY = 1;
constexpr int MIN_MONTH = 1;
constexpr int MAX_MONTH = 12;
constexpr int MIN_YEAR = 2024;
constexpr int MAX_YEAR = 2099;

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

unsigned getDaysInMonth(const int year, const unsigned month) {
  static constexpr unsigned DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    return isLeapYear(year) ? 29U : 28U;
  }
  if (month < 1 || month > 12) {
    return 31;
  }
  return DAYS_PER_MONTH[month - 1];
}

std::string formatTwoDigits(const unsigned value) {
  char buffer[4];
  snprintf(buffer, sizeof(buffer), "%02u", value);
  return buffer;
}

unsigned wrapValue(const unsigned value, const int delta, const unsigned minValue, const unsigned maxValue) {
  const int range = static_cast<int>(maxValue - minValue + 1);
  int offset = static_cast<int>(value - minValue) + delta;
  offset %= range;
  if (offset < 0) {
    offset += range;
  }
  return minValue + static_cast<unsigned>(offset);
}
}  // namespace

void ReadingDateSelectionActivity::onEnter() {
  RickyStatsEditor::onEnter();

  bool initialized = false;
  if (initialDayOrdinal != 0) {
    initialized = TimeUtils::getDateFromDayOrdinal(initialDayOrdinal, year, month, day);
  }

  if (!initialized) {
    bool usedFallback = false;
    const uint32_t referenceTimestamp = READING_STATS.getDisplayTimestamp(&usedFallback);
    if (TimeUtils::isClockValid(referenceTimestamp)) {
      tm localTime = {};
      if (TimeUtils::getLocalDateTime(referenceTimestamp, localTime)) {
        year = std::clamp(localTime.tm_year + 1900, MIN_YEAR, MAX_YEAR);
        month = static_cast<unsigned>(std::clamp(localTime.tm_mon + 1, MIN_MONTH, MAX_MONTH));
        day = static_cast<unsigned>(
            std::clamp(localTime.tm_mday, MIN_DAY, static_cast<int>(getDaysInMonth(year, month))));
      }
    }
  }

  selectedField = 0;
  requestUpdate();
}

void ReadingDateSelectionActivity::adjustSelectedValue(const int delta) {
  if (selectedField == 0) {
    day = wrapValue(day, delta, MIN_DAY, getDaysInMonth(year, month));
  } else if (selectedField == 1) {
    month = wrapValue(month, delta, MIN_MONTH, MAX_MONTH);
    day = std::min(day, getDaysInMonth(year, month));
  } else {
    year = std::clamp(year + delta, MIN_YEAR, MAX_YEAR);
    day = std::min(day, getDaysInMonth(year, month));
  }
  requestUpdate();
}

uint32_t ReadingDateSelectionActivity::getSelectedDayOrdinal() const {
  return TimeUtils::getDayOrdinalForDate(year, month, day);
}

std::string ReadingDateSelectionActivity::getSelectedDateLabel() const {
  return TimeUtils::formatDateParts(year, month, day);
}

void ReadingDateSelectionActivity::finishWithDate() {
  setResult(ActivityResult{PageResult{getSelectedDayOrdinal()}});
  finish();
}

const char* ReadingDateSelectionActivity::headerTitle() const { return tr(STR_SET_DATE); }

const char* ReadingDateSelectionActivity::fieldLabel(int index) const {
  static constexpr StrId labels[] = {StrId::STR_DAY, StrId::STR_MONTH, StrId::STR_YEAR};
  return I18N.get(labels[index]);
}

std::string ReadingDateSelectionActivity::fieldValue(int index) const {
  if (index == 0) return formatTwoDigits(day);
  if (index == 1) return formatTwoDigits(month);
  return std::to_string(year);
}

void ReadingDateSelectionActivity::buildDetails(RickyStatsPage& page) {
  const auto date = getSelectedDateLabel();
  const auto area = page.take(page.measure(date.c_str(), RickyDesignUi::BODY, page.viewport.width));
  RickyDesignUi::text(renderer, area, date.c_str(), RickyDesignUi::BODY, false, freeink::ui::TextAlign::Left, 16);
  const char* hint = I18N.get(mappedInput.hasTouch() ? StrId::STR_SET_DATE_TOUCH_HINT : StrId::STR_SET_DATE_HINT);
  const auto hintArea = page.take(page.measure(hint, RickyDesignUi::CAPTION, page.viewport.width));
  RickyDesignUi::secondaryText(renderer, hintArea, hint, RickyDesignUi::CAPTION, false, freeink::ui::TextAlign::Left,
                               16);
}
