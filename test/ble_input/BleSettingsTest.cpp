#include <gtest/gtest.h>

#include "BleButtonMapActivity.h"
#include "BleInput.h"
#include "BluetoothSettingsActivity.h"
#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

class BleSettingsTest : public testing::Test {
 protected:
  HalGPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input{gpio, renderer};
  void SetUp() override {
    BleHid = {};
    SETTINGS = {};
    sdFontSystem = {};
    freeInternal = bleinput::kExplicitMinFreeInternal - 1;
    largestInternal = bleinput::kExplicitMinLargestInternal;
    renderer.cache.recoveredInternal = 1;
  }
};

TEST_F(BleSettingsTest, SettingsAndMappingRecoverUnderOneRenderLock) {
  BluetoothSettingsActivity settings(renderer, input);
  settings.onEnter();
  EXPECT_TRUE(BleHid.running);
  EXPECT_TRUE(BleHid.startUnderLock);
  EXPECT_TRUE(renderer.cache.recoveredUnderLock);
  EXPECT_EQ(renderLockDepth, 0);
  EXPECT_EQ(sdFontSystem.unloads, 1);
  BleButtonMapActivity mapping(renderer, input);
  mapping.onEnter();
  EXPECT_EQ(sdFontSystem.unloads, 1);
  bleinput::stop();
  freeInternal = bleinput::kExplicitMinFreeInternal - 1;
  BleButtonMapActivity retryMapping(renderer, input);
  retryMapping.onEnter();
  EXPECT_TRUE(BleHid.running);
  EXPECT_TRUE(renderer.cache.recoveredUnderLock);
  EXPECT_EQ(sdFontSystem.unloads, 2);
  EXPECT_EQ(renderLockDepth, 0);
}

TEST_F(BleSettingsTest, PairingDoesNotOverwriteStartGateFailure) {
  renderer.cache.recoveredInternal = 0;
  BluetoothSettingsActivity settings(renderer, input);
  settings.onEnter();
  UiScreen screen;
  settings.testBuild(screen);
  settings.testActivate(2);  // Paired devices; available even while host is stopped.
  settings.testBuild(screen);
  settings.testActivate(0);
  settings.testDraw();
  EXPECT_STREQ(UITheme::getInstance().lastSubHeader, "low memory");
  EXPECT_EQ(BleHid.connects, 0);
  EXPECT_FALSE(BleHid.running);
  EXPECT_EQ(renderLockDepth, 0);
}

TEST_F(BleSettingsTest, ConnectionFailureIsStillReportedAfterSuccessfulStart) {
  BleHid.connectResult = false;
  BluetoothSettingsActivity settings(renderer, input);
  settings.onEnter();
  UiScreen screen;
  settings.testBuild(screen);
  settings.testActivate(3);  // Running test host reports connected, adding Disconnect.
  settings.testBuild(screen);
  settings.testActivate(0);
  settings.testDraw();
  EXPECT_STREQ(UITheme::getInstance().lastSubHeader, "connection failed");
  EXPECT_EQ(BleHid.connects, 1);
}

TEST_F(BleSettingsTest, ResetRequiresConfirmationAndPreservesPairingAndOtherSettings) {
  ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 42, bleinput::Action::PageForward));
  ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 43, bleinput::Action::Back));
  BleButtonMapActivity mapping(renderer, input);
  mapping.onEnter();
  const auto paired = BleHid.pairedCount();
  mapping.testActivate(static_cast<int>(bleinput::Action::Count));
  ASSERT_NE(OptionPopup::shown, nullptr);
  EXPECT_EQ(OptionPopup::shown->selected, 0);  // Cancel is selected initially.
  EXPECT_TRUE(mapping.testInput());
  EXPECT_EQ(SETTINGS.saves, 0);
  EXPECT_NE(bleinput::findByAction(SETTINGS.bleKeyMap, bleinput::Action::PageForward), nullptr);
  OptionPopup::choose(1);
  EXPECT_EQ(SETTINGS.saves, 1);
  for (const auto& entry : SETTINGS.bleKeyMap) EXPECT_TRUE(bleinput::isEmpty(entry));
  EXPECT_EQ(BleHid.pairedCount(), paired);
  EXPECT_TRUE(BleHid.running);
  EXPECT_EQ(BleHid.ends, 0);
  EXPECT_EQ(BleHid.forgets, 0);
  EXPECT_TRUE(SETTINGS.bluetoothEnabled);
  BleHid.events.push_back({freeink::SpecialKey::None, 42});
  input.update();
  EXPECT_FALSE(input.wasPressed(MappedInputManager::Button::PageForward));
  mapping.onExit();
}

TEST_F(BleSettingsTest, CancellingOrLeavingResetKeepsBindingsAndDoesNotSave) {
  ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 42, bleinput::Action::PageForward));
  BleButtonMapActivity mapping(renderer, input);
  mapping.onEnter();
  mapping.testActivate(static_cast<int>(bleinput::Action::Count));
  OptionPopup::choose(0);
  EXPECT_NE(bleinput::findByAction(SETTINGS.bleKeyMap, bleinput::Action::PageForward), nullptr);
  EXPECT_EQ(SETTINGS.saves, 0);
  mapping.testActivate(static_cast<int>(bleinput::Action::Count));
  mapping.onExit();
  EXPECT_EQ(OptionPopup::shown, nullptr);
  EXPECT_EQ(SETTINGS.saves, 0);
  EXPECT_NE(bleinput::findByAction(SETTINGS.bleKeyMap, bleinput::Action::PageForward), nullptr);
}

TEST_F(BleSettingsTest, ResetWorksWhenBluetoothCannotStartAndLearningStillWorksAfterReset) {
  ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 42, bleinput::Action::PageForward));
  renderer.cache.recoveredInternal = 0;
  BleButtonMapActivity offline(renderer, input);
  offline.onEnter();
  ASSERT_FALSE(BleHid.running);
  offline.testActivate(static_cast<int>(bleinput::Action::Count));
  ASSERT_NE(OptionPopup::shown, nullptr);
  OptionPopup::choose(1);
  EXPECT_EQ(SETTINGS.saves, 1);
  EXPECT_EQ(bleinput::findByAction(SETTINGS.bleKeyMap, bleinput::Action::PageForward), nullptr);
  offline.onExit();
  renderer.cache.recoveredInternal = 1;
  BleButtonMapActivity online(renderer, input);
  online.onEnter();
  ASSERT_TRUE(BleHid.running);
  online.testActivate(0);
  BleHid.events.push_back({freeink::SpecialKey::None, 99});
  input.update();
  EXPECT_TRUE(online.testInput());
  const auto* entry = bleinput::findByAction(SETTINGS.bleKeyMap, bleinput::Action::PageForward);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->keyValue, 99);
  EXPECT_EQ(SETTINGS.saves, 2);
  online.onExit();
}
