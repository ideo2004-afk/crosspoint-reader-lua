#pragma once

#include <I18n.h>

#include <vector>

#include "CrossPointSettings.h"
#include "activities/settings/SettingsActivity.h"

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
inline std::vector<SettingInfo> getSettingsList() {
  return {
      // --- Display ---
      SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                        {StrId::STR_DARK, StrId::STR_LIGHT, StrId::STR_CUSTOM, StrId::STR_COVER, StrId::STR_PAGE},
                        "sleepScreen", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                        {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                        "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_STATUS_BAR, &CrossPointSettings::statusBar, "statusBar", StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_STATUS_BAR_CLOCK, &CrossPointSettings::statusBarClock, "statusBarClock", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                        {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                        StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(
          StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
          "refreshFrequency", StrId::STR_CAT_DISPLAY),
      SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                        {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_FLOW},
                        "uiTheme", StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                          StrId::STR_CAT_DISPLAY),
      SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode",
                          StrId::STR_CAT_DISPLAY),

      // --- Reader ---
      SettingInfo::Action(StrId::STR_EXT_READER_FONT, SettingAction::FontSelectReader, StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                        {StrId::STR_BOOKERLY, StrId::STR_NOTO_SANS},
                        "fontFamily", StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_FONT_SIZE, &CrossPointSettings::fontSize,
                        {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE}, "fontSize",
                        StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                        {StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing", StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                         StrId::STR_BOOK_S_STYLE},
                        "paragraphAlignment", StrId::STR_CAT_READER),

      SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                          StrId::STR_CAT_READER),
      SettingInfo::Enum(StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
                        {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CCW},
                        "orientation", StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing, "extraParagraphSpacing",
                          StrId::STR_CAT_READER),
      SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                          StrId::STR_CAT_READER),

      // --- Controls ---
      SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                        {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN}, "shortPwrBtn",
                        StrId::STR_CAT_CONTROLS),
      SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::ButtonRemap, StrId::STR_CAT_CONTROLS),
                        
      // Reader
      SettingInfo::Action(StrId::STR_CTRL_R_TITLE, SettingAction::None, StrId::STR_CAT_CONTROLS),
      SettingInfo::Action(StrId::STR_CTRL_R_R_LONG, SettingAction::None, StrId::STR_CAT_CONTROLS),
      SettingInfo::Action(StrId::STR_CTRL_R_L_LONG, SettingAction::None, StrId::STR_CAT_CONTROLS),
      SettingInfo::Action(StrId::STR_CTRL_R_UP_LONG, SettingAction::None, StrId::STR_CAT_CONTROLS),
      SettingInfo::Action(StrId::STR_CTRL_R_DN_LONG, SettingAction::None, StrId::STR_CAT_CONTROLS),
      SettingInfo::Action(StrId::STR_CTRL_R_L_UP_DN, SettingAction::None, StrId::STR_CAT_CONTROLS),
      SettingInfo::Action(StrId::STR_CTRL_R_R_UP_DN, SettingAction::None, StrId::STR_CAT_CONTROLS),

      // --- System ---
      SettingInfo::Enum(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeout,
                        {StrId::STR_MIN_1, StrId::STR_MIN_5, StrId::STR_MIN_10, StrId::STR_MIN_15, StrId::STR_MIN_30},
                        "sleepTimeout", StrId::STR_CAT_SYSTEM),
      SettingInfo::Enum(StrId::STR_LANGUAGE, &CrossPointSettings::language,
                        {StrId::STR_LANG_ENGLISH, StrId::STR_LANG_T_CHINESE}, "language", StrId::STR_CAT_SYSTEM),
      SettingInfo::Action(StrId::STR_TIME_SYNC, SettingAction::TimeSync, StrId::STR_CAT_SYSTEM),
  };
}