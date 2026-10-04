#include "RickyProfileActivity.h"
#ifdef RICKYOS_PRODUCT
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/RickyProfile.h"
#include "components/UITheme.h"

const char* RickyProfileActivity::headerTitle() const { return tr(STR_RICKY_PROFILE); }

void RickyProfileActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto bounds = pageContentRect();
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(bounds.y), 0,
                  static_cast<int16_t>(renderer.getScreenHeight() - bounds.y - bounds.height), 0});
  const auto& theme = screen.theme();
  screen.insetContent(fui::Insets{theme.spaceSm, theme.spaceLg, theme.spaceSm, theme.spaceLg});
  const int height = screen.target().lineHeight(theme.bodyText.font) +
                     screen.target().lineHeight(theme.smallText.font) + theme.spaceSm;
  RickyProfile::drawCard(screen, renderer, screen.takeTop(height, theme.spaceLg), fui::NO_ACTION);
  rows[0].label = tr(STR_RICKY_NICKNAME);
  rows[0].value = RickyProfile::nickname();
  rows[1].label = tr(STR_RICKY_CHANGE_AVATAR);
  rows[1].subtitle = tr(STR_RICKY_AVATAR_TYPES);
  rows[2].label = tr(STR_RICKY_RESET_AVATAR);
  for (int i = 0; i < listCount(); ++i) rows[i].actionValue = i;
  if (failed)
    screen.target().text(screen.takeTop(screen.target().lineHeight(theme.smallText.font), theme.spaceSm),
                         tr(STR_RICKY_PROFILE_FAILED), theme.smallText);
  fui::ListProps props;
  props.items = rows.data();
  props.count = rows.size();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.rowGap = std::max<int16_t>(6, theme.listRowGap);
  syncListViewport(screen, props);
  screen.list(props);
}

bool RickyProfileActivity::handleCustomInput() {
  if (!waitForConfirmRelease) return false;
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitForConfirmRelease = false;
  return true;
}

void RickyProfileActivity::activateIndex(int index) {
  app.clearTapFlash();
  failed = false;
  if (index == 0) {
    startActivityForResultWith<KeyboardEntryActivity>(
        [this](const ActivityResult& result) {
          waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
          if (result.isCancelled) return;
          const auto* entry = std::get_if<KeyboardResult>(&result.data);
          if (!entry) return;
          RenderLock lock(*this);
          char previous[sizeof(SETTINGS.rickyNickname)];
          memcpy(previous, SETTINGS.rickyNickname, sizeof(previous));
          const int length = utf8SafeTruncateBuffer(
              entry->text.c_str(), std::min<int>(entry->text.size(), sizeof(SETTINGS.rickyNickname) - 1));
          memcpy(SETTINGS.rickyNickname, entry->text.data(), length);
          SETTINGS.rickyNickname[length] = '\0';
          failed = !SETTINGS.saveToFile();
          if (failed) memcpy(SETTINGS.rickyNickname, previous, sizeof(previous));
        },
        tr(STR_RICKY_NICKNAME), std::string(SETTINGS.rickyNickname), sizeof(SETTINGS.rickyNickname) - 1);
  } else if (index == 1) {
    startActivityForResultWith<FileBrowserActivity>(
        [this](const ActivityResult& result) {
          waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
          if (result.isCancelled) return;
          const auto* entry = std::get_if<FilePathResult>(&result.data);
          if (!entry) return;
          RenderLock lock(*this);
          closeRouting();
          GUI.drawPopup(renderer, tr(STR_RICKY_AVATAR_IMPORTING));
          failed = !RickyProfile::importAvatar(entry->path);
        },
        "/", FileBrowserActivity::Mode::PickAvatar);
  } else if (index == 2) {
    RenderLock lock(*this);
    char previous[sizeof(SETTINGS.rickyAvatarPath)];
    memcpy(previous, SETTINGS.rickyAvatarPath, sizeof(previous));
    SETTINGS.rickyAvatarPath[0] = '\0';
    failed = !SETTINGS.saveToFile();
    if (failed) memcpy(SETTINGS.rickyAvatarPath, previous, sizeof(previous));
    requestUpdate();
  }
}
#endif
