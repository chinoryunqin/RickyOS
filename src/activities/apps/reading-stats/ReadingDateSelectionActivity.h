#pragma once

#include "RickyStatsEditor.h"

class ReadingDateSelectionActivity final : public RickyStatsEditor {
  uint32_t initialDayOrdinal = 0;
  int year = 2026;
  unsigned month = 6;
  unsigned day = 15;

  void adjustSelectedValue(int delta) override;
  uint32_t getSelectedDayOrdinal() const;
  std::string getSelectedDateLabel() const;
  void finishWithDate();

 public:
  explicit ReadingDateSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        uint32_t initialDayOrdinal)
      : RickyStatsEditor("ReadingDateSelection", renderer, mappedInput), initialDayOrdinal(initialDayOrdinal) {}

  void onEnter() override;

 protected:
  const char* headerTitle() const override;
  const char* fieldLabel(int index) const override;
  std::string fieldValue(int index) const override;
  void activateField() override { finishWithDate(); }
  void buildDetails(RickyStatsPage& page) override;
};
