#pragma once

#include "RickyStatsActivity.h"

// Shared three-field editor for date and time corrections. Fields are measured
// documents; the action strip has its own reserved safe area and hit geometry.
class RickyStatsEditor : public RickyStatsActivity {
 public:
  void onEnter() override;

 protected:
  static constexpr int kFieldCount = 3;
  RickyStatsEditor(const char* name, GfxRenderer& renderer, MappedInputManager& input)
      : RickyStatsActivity(name, renderer, input) {}
  int selectedField = 0;  // Navigation index into the three visible fields.
  virtual const char* fieldLabel(int index) const = 0;
  virtual std::string fieldValue(int index) const = 0;
  virtual const char* contextText() const { return nullptr; }
  virtual void buildDetails(RickyStatsPage&) {}
  virtual void adjustSelectedValue(int delta) = 0;
  virtual void activateField() = 0;
  virtual const char* confirmLabel() const;
  void buildPage(UiScreen& screen, RickyStatsPage& page) final;
  bool handleStatsInput() final;
  bool scrollWithButtons() const final { return false; }
  int actionHeight() const final;
  void buildActions(UiScreen& screen, const Rect& area) final;
  void drawFooter() final;

 private:
  enum class Action : freeink::ui::ActionId { Field = ACTION_USER, Minus, Plus, Confirm };
  static void dispatch(const freeink::ui::ActionEvent& event, void* user);
  void selectField(int index);
  Rect fieldAreas_[kFieldCount] = {Rect{}, Rect{}, Rect{}};
};
