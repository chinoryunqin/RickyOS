#pragma once
#include <array>
#include <string>

#include "activities/UiListActivity.h"
class UiTypographyActivity final : public UiListActivity {
 public:
  UiTypographyActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UiListActivity("UiTypography", renderer, input) {}
  void onEnter() override;
  void onExit() override;

 private:
  static constexpr unsigned maxFamilies = 32;
  std::array<std::string, maxFamilies> families_;
  freeink::ui::ListItem rows_[maxFamilies + 1]{};
  std::string draftFamily_;
  uint8_t draftScale_ = 100;
  unsigned familyCount_ = 0;
  enum class Page { Preview, Family };
  Page page_ = Page::Preview;
  bool saveFailed_ = false;
  char sizeText_[12]{};
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  void updatePreview();
};
