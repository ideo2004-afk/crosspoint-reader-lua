#include <Arduino.h>
#include <Epub.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <cstring>

#include <FontManager.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "ReadingStatsStore.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/home/MyLibraryActivity.h"
#include "activities/home/RecentBooksActivity.h"
#include "LibraryStore.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "activities/util/LuaActivity.h"
#include "activities/util/PluginListActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"
#include "util/LuaManager.h"

#include "util/TimeService.h"

HalDisplay display;
HalGPIO gpio;
MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
Activity* currentActivity;
unsigned long lastActivityMillis = 0;

// Fonts
EpdFont bookerly14RegularFont(&bookerly_14_regular);
EpdFont bookerly14BoldFont(&bookerly_14_bold);
EpdFont bookerly14ItalicFont(&bookerly_14_italic);
EpdFont bookerly14BoldItalicFont(&bookerly_14_bolditalic);
EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, &bookerly14BoldFont, &bookerly14ItalicFont,
                                   &bookerly14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont bookerly12RegularFont(&bookerly_12_regular);
EpdFont bookerly12BoldFont(&bookerly_12_bold);
EpdFont bookerly12ItalicFont(&bookerly_12_italic);
EpdFont bookerly12BoldItalicFont(&bookerly_12_bolditalic);
EpdFontFamily bookerly12FontFamily(&bookerly12RegularFont, &bookerly12BoldFont, &bookerly12ItalicFont,
                                   &bookerly12BoldItalicFont);
EpdFont bookerly16RegularFont(&bookerly_16_regular);
EpdFont bookerly16BoldFont(&bookerly_16_bold);
EpdFont bookerly16ItalicFont(&bookerly_16_italic);
EpdFont bookerly16BoldItalicFont(&bookerly_16_bolditalic);
EpdFontFamily bookerly16FontFamily(&bookerly16RegularFont, &bookerly16BoldFont, &bookerly16ItalicFont,
                                   &bookerly16BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&notosans_12_regular);
EpdFont ui12BoldFont(&notosans_12_bold);
EpdFont ui12ItalicFont(&notosans_12_italic);
EpdFont ui12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont, &ui12ItalicFont, &ui12BoldItalicFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

Activity* nextActivity = nullptr;

void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  nextActivity = activity;
}

void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) return;
  const auto start = millis();
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration = (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;
  gpio.update();
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) { delay(10); gpio.update(); }
  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do { delay(10); gpio.update(); } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    if (gpio.getHeldTime() < calibratedPressDuration) powerManager.startDeepSleep(gpio);
  } else powerManager.startDeepSleep(gpio);
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) { delay(50); gpio.update(); }
}

void enterDeepSleep() {
  static bool isEnteringSleep = false; if (isEnteringSleep) return; isEnteringSleep = true;
  HalPowerManager::Lock powerLock;
  APP_STATE.lastSleepFromReader = currentActivity && currentActivity->isReaderActivity();
  APP_STATE.saveToFile();
  exitActivity();
  currentActivity = new SleepActivity(renderer, mappedInputManager);
  currentActivity->onEnter();
  display.deepSleep();
  powerManager.startDeepSleep(gpio);
}

void enterLightSleep() {
  HalPowerManager::Lock powerLock;
  APP_STATE.lastSleepFromReader = currentActivity && currentActivity->isReaderActivity();
  APP_STATE.saveToFile();
  exitActivity();
  currentActivity = new SleepActivity(renderer, mappedInputManager);
  currentActivity->onEnter();
  display.deepSleep();
  powerManager.startLightSleep(gpio);
}

void onGoHome();
void onGoToMyLibraryWithPath(const std::string& path);
void onGoToRecentBooks();

void onGoToReader(const std::string& initialEpubPath) {
  enterNewActivity(new ReaderActivity(renderer, mappedInputManager, initialEpubPath, onGoHome, onGoToMyLibraryWithPath));
}

void onGoToMyLibrary() {
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader, "/books"));
}

void onGoToRecentBooks() {
  enterNewActivity(new RecentBooksActivity(renderer, mappedInputManager, onGoHome, onGoToReader));
}

void onGoToSettings() {
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome));
}

void onGoToMyLibraryWithPath(const std::string& path) {
  enterNewActivity(new MyLibraryActivity(renderer, mappedInputManager, onGoHome, onGoToReader, path));
}

void onGoToLuaPlugins() {
  enterNewActivity(new PluginListActivity(renderer, mappedInputManager, 
    [](const std::string& name) {
        enterNewActivity(new LuaActivity(renderer, mappedInputManager, name, onGoToLuaPlugins));
    }, onGoHome));
}

void onGoHome() {
  enterNewActivity(new HomeActivity(renderer, mappedInputManager, onGoToReader, onGoToMyLibrary, onGoToRecentBooks,
                                     onGoToSettings, onGoToLuaPlugins));
}

void setupDisplayAndFonts() {
  display.begin(); renderer.begin();
  if (!fontDecompressor.init()) LOG_ERR("MAIN", "Font decompressor init failed");
  renderer.setFontDecompressor(&fontDecompressor);
  renderer.insertFont(BOOKERLY_14_FONT_ID, bookerly14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(BOOKERLY_12_FONT_ID, bookerly12FontFamily);
  renderer.insertFont(BOOKERLY_16_FONT_ID, bookerly16FontFamily);
  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
#endif
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
}

void setup() {
  gpio.begin(); powerManager.begin();
  if (gpio.isUsbConnected()) { Serial.begin(115200); unsigned long s = millis(); while (!Serial && (millis()-s) < 3000) delay(10); }
  if (!Storage.begin()) {
    setupDisplayAndFonts();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInputManager, "SD card error", EpdFontFamily::BOLD));
    return;
  }
  SETTINGS.loadFromFile(); I18N.loadSettings(); 
  lastActivityMillis = millis();
  // Sync I18n with global settings if different (I18N.loadSettings loads from its own file, 
  // but SETTINGS.language is the source of truth for the UI enum)
  if (I18N.getLanguage() != static_cast<Language>(SETTINGS.language)) {
    I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  }
  UITheme::getInstance().reload();
  TIME_SERVICE.begin();
  FontMgr.scanFonts(); FontMgr.loadSettings();
  renderer.setFadingFix(SETTINGS.fadingFix);
  renderer.setDarkMode(SETTINGS.darkMode);
  ButtonNavigator::setMappedInputManager(mappedInputManager);
  if (gpio.getWakeupReason() == HalGPIO::WakeupReason::PowerButton) verifyPowerButtonDuration();
  else if (gpio.getWakeupReason() == HalGPIO::WakeupReason::AfterUSBPower) powerManager.startDeepSleep(gpio);
  setupDisplayAndFonts();
  
  exitActivity();
  enterNewActivity(new BootActivity(renderer, mappedInputManager));
  APP_STATE.loadFromFile(); RECENT_BOOKS.loadFromFile(); READING_STATS.loadFromFile();
  LIBRARY_STORE.loadFromFile();
  if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader || mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    onGoHome();
  } else {
    std::string p = APP_STATE.openEpubPath; APP_STATE.openEpubPath = ""; APP_STATE.readerActivityLoadCount++; APP_STATE.saveToFile(); onGoToReader(p);
  }
  waitForPowerRelease();
}

void loop() {
  if (nextActivity) { Activity* a = nextActivity; nextActivity = nullptr; exitActivity(); currentActivity = a; currentActivity->onEnter(); lastActivityMillis = millis(); }
  mappedInputManager.update();

  if (mappedInputManager.wasAnyPressed() || mappedInputManager.wasAnyReleased()) {
    lastActivityMillis = millis();
  }

  if (TIME_SERVICE.syncIfDue() && currentActivity) {
    currentActivity->requestUpdate();
  }
  renderer.setFadingFix(SETTINGS.fadingFix);
  if (currentActivity && currentActivity->preventAutoSleep()) {
    powerManager.setPowerSaving(false);
    lastActivityMillis = millis();
  }

  if (millis() > 3000 && gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) enterDeepSleep();

  if (currentActivity && !currentActivity->preventAutoSleep() && (millis() - lastActivityMillis > SETTINGS.getSleepTimeoutMs())) {
    enterDeepSleep();
  }

  if (currentActivity) currentActivity->loop();
  if (currentActivity && currentActivity->skipLoopDelay()) { powerManager.setPowerSaving(false); yield(); }
  else { delay(10); }
}
