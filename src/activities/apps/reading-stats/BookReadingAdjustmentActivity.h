#pragma once

#include <cstdint>
#include <string>

#include "RickyStatsEditor.h"

class BookReadingAdjustmentActivity final : public RickyStatsEditor {
  std::string bookPath;
  std::string bookTitle;
  int selectedOperation = 0;
  int selectedDuration = 1;
  uint32_t selectedDayOrdinal = 0;
  bool lastApplyFailed = false;

  void adjustSelectedValue(int delta) override;
  void openDateSelection();
  void initializeSelectedDate();
  int32_t getSelectedDeltaMs() const;
  uint64_t getSelectedDayReadingMs() const;
  bool canApplySelectedAdjustment() const;
  std::string getAdjustmentPreviewInfo() const;
  const char* getOperationLabel() const;
  std::string getDateLabel() const;
  const char* getDurationLabel() const;
  bool applyAdjustment();

 public:
  explicit BookReadingAdjustmentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                         std::string bookTitle)
      : RickyStatsEditor("BookReadingAdjustment", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        bookTitle(std::move(bookTitle)) {}

  void onEnter() override;

 protected:
  const char* headerTitle() const override;
  const char* fieldLabel(int index) const override;
  std::string fieldValue(int index) const override;
  const char* contextText() const override { return bookTitle.c_str(); }
  void buildDetails(RickyStatsPage& page) override;
  void activateField() override;
  const char* confirmLabel() const override;
};
