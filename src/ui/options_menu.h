// harmony-recomp - in-game options menu
//
// An Unleashed Recompiled-style options screen over the running game:
// categories on the left, settings on the right, a description of the
// highlighted setting at the bottom. Works with mouse, keyboard (arrows,
// Enter, Esc) and controllers (D-pad / stick, A, B). Settings are SDK and
// project cvars; changes apply immediately where the runtime allows and are
// saved to the config file when the menu closes. A few rows are actions
// (e.g. opening the achievements screen) instead of values.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "menu_common.h"

namespace hod::ui {

class OptionsMenu : public rex::ui::ImGuiDialog {
 public:
  struct Actions {
    std::function<void()> show_achievements;
  };

  OptionsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path, Actions actions,
              std::function<void()> on_close);
  ~OptionsMenu() override;

  // Close requested from outside (hotkey pressed again).
  void RequestClose() { close_requested_ = true; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  struct Choice {
    std::string label;
    std::string value;  // cvar value
  };
  struct Option {
    std::string category;
    std::string label;
    std::string description;
    std::string cvar;
    std::vector<Choice> choices;
    bool restart = false;
    std::function<void(const std::string& value)> on_change;
    // Set for action rows, which have no cvar or choices.
    std::function<void()> action;
    std::string action_label = "Open  >";
    // Action rows that change the save (cheats) ask to be pressed twice.
    bool confirm = false;
  };

  void BuildOptions();
  std::vector<Option*> VisibleOptions();
  int CurrentChoice(const Option& option) const;
  void Change(Option& option, int delta);
  void Close();

  std::filesystem::path config_path_;
  Actions actions_;
  std::function<void()> on_close_;
  std::vector<std::string> categories_;
  std::vector<Option> options_;
  MenuNavigator navigator_;
  int category_ = 0;
  int selected_ = 0;
  bool changed_ = false;
  const Option* armed_ = nullptr;  // confirm row pressed once, waiting for the second
  bool close_requested_ = false;
  bool closed_ = false;
};

// Lives for the whole session as an invisible dialog so it ticks every frame:
//  - opens the options menu on the Guide / PS button, which the SDK doesn't
//    pass to the game;
//  - toggles fullscreen on Alt+Enter (the SDK's key binds take single keys);
//  - feeds controllers into ImGui navigation, which the SDK's backend doesn't,
//    so system dialogs (save prompts, message boxes) work without a mouse;
//  - applies the volume settings;
//  - decides each frame whether the game gets input (see GameInputBlocked).
class OptionsHotkey : public rex::ui::ImGuiDialog {
 public:
  // `menus_open` says whether one of the app's menus is showing.
  OptionsHotkey(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_options,
                std::function<void()> on_fullscreen, std::function<bool()> menus_open);
  ~OptionsHotkey() override;

  // Shows how to open the options menu for a few seconds (once the game is
  // starting). Hidden early when the menu opens.
  void ShowHint();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  std::function<void()> on_options_;
  std::function<void()> on_fullscreen_;
  std::function<bool()> menus_open_;
  bool was_down_ = false;
  bool hint_pending_ = false;
  double hint_until_ = 0.0;
};

// Whether the game should ignore input: a menu is open, a text field has
// focus, or Alt is held (so the Enter of Alt+Enter doesn't also press Start).
// Worked out on the UI thread each frame; safe to read from any thread.
bool GameInputBlocked();

}  // namespace hod::ui
