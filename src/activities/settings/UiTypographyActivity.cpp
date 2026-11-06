#include "SystemUiFonts.h"
#if RICKYOS_SYSTEM_UI_FONTS
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <array>
#include <cstdio>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "UiTypographyActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
namespace fui = freeink::ui;
int UiTypographyActivity::listCount() const {
  if (page_ == Page::Family) return familyCount_ + 1;
  return 5;
}
void UiTypographyActivity::onEnter() {
  UiListActivity::onEnter();
  draftFamily_ = SETTINGS.uiFontFamily;
  draftScale_ = SETTINGS.uiTextScale;
  RenderLock lock(*this);
  sdFontSystem.refreshIfDirty();
  for (const auto& f : sdFontSystem.registry().getFamilies())
    if (f.vector && f.name.size() < sizeof(SETTINGS.uiFontFamily) && familyCount_ < maxFamilies)
      families_[familyCount_++] = f.name;
}
void UiTypographyActivity::onExit() {
  systemUiFonts::clearPreview();
  Activity::onExit();
}
const char* UiTypographyActivity::headerTitle() const { return tr(STR_UI_TYPOGRAPHY); }
void UiTypographyActivity::updatePreview() {
  RenderLock lock(*this);
  systemUiFonts::preview(draftFamily_.c_str(), draftScale_);
  saveFailed_ = false;
  requestUpdate();
}
void UiTypographyActivity::onBackButton() {
  if (page_ != Page::Preview) {
    page_ = Page::Preview;
    nav.reset();
    requestUpdate();
  } else
    finish();
}
void UiTypographyActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (page_ == Page::Family) {
    draftFamily_ = index == 0 ? "" : families_[index - 1];
    page_ = Page::Preview;
    nav.reset();
    updatePreview();
    return;
  }
  if (index == 0) {
    page_ = Page::Family;
    nav.reset();
    requestUpdate();
    return;
  }
  if (index == 1) {
    startActivityForResultWith<IntervalSelectionActivity>(
        [this](const ActivityResult& result) {
          if (result.isCancelled) return;
          if (const auto* selected = std::get_if<IntervalResult>(&result.data)) {
            draftScale_ = selected->value;
            updatePreview();
          }
        },
        "UiTextScale", StrId::STR_UI_FONT_SIZE, draftScale_, systemUiFonts::minScale, systemUiFonts::maxScale, 1, 5,
        StrId::STR_LINE_SPACING_VALUE);
    return;
  }
  if (index == 2) {
    draftFamily_.clear();
    draftScale_ = 100;
    updatePreview();
    return;
  }
  if (index == 3) {
    RenderLock lock(*this);
    const auto previousScale = SETTINGS.uiTextScale;
    char previousFamily[sizeof(SETTINGS.uiFontFamily)];
    snprintf(previousFamily, sizeof(previousFamily), "%s", SETTINGS.uiFontFamily);
    snprintf(SETTINGS.uiFontFamily, sizeof(SETTINGS.uiFontFamily), "%s", draftFamily_.c_str());
    SETTINGS.uiTextScale = draftScale_;
    if (!SETTINGS.saveToFile()) {
      snprintf(SETTINGS.uiFontFamily, sizeof(SETTINGS.uiFontFamily), "%s", previousFamily);
      SETTINGS.uiTextScale = previousScale;
      saveFailed_ = true;
      LOG_ERR("UIFONT", "Failed to save interface typography");
      requestUpdate();
      return;
    }
    systemUiFonts::clearPreview();
    finish();
    return;
  }
}
void UiTypographyActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto padding = static_cast<int16_t>(metrics.contentSidePadding);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), padding, 0, padding});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (auto& row : rows_) row = {};
  if (page_ == Page::Family) {
    rows_[0].label = tr(STR_UI_FONT_DEFAULT);
    rows_[0].actionValue = 0;
    for (unsigned i = 0; i < familyCount_; ++i) {
      rows_[i + 1].label = families_[i].c_str();
      rows_[i + 1].actionValue = i + 1;
    }
  } else {
    rows_[0].label = tr(STR_UI_FONT);
    rows_[0].value = draftFamily_.empty() ? tr(STR_UI_FONT_DEFAULT) : draftFamily_.c_str();
    snprintf(sizeText_, sizeof(sizeText_), "%u%%", draftScale_);
    rows_[1].label = tr(STR_UI_FONT_SIZE);
    rows_[1].value = sizeText_;
    rows_[2].label = tr(STR_UI_FONT_RESET);
    rows_[3].label = tr(STR_UI_FONT_APPLY);
    rows_[4].label = tr(STR_UI_FONT_SAMPLE);
    rows_[4].subtitle = saveFailed_ ? tr(STR_AUTO_FONT_SETTINGS_SAVE_FAILED) : tr(STR_UI_FONT_HINT);
    if (!draftFamily_.empty() && !systemUiFonts::customAvailable()) {
      rows_[0].value = tr(STR_UI_FONT_DEFAULT);
      rows_[0].subtitle = tr(STR_UI_FONT_UNAVAILABLE);
    }
    for (int i = 0; i < 5; ++i) rows_[i].actionValue = i;
  }
  fui::ListProps props;
  props.items = rows_;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
#endif
