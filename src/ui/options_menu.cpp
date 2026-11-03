// harmony-recomp - in-game options menu (see options_menu.h)

#include "options_menu.h"

#include <algorithm>
#include <atomic>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>
#include <fmt/format.h>
#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "cheats.h"
#include "fonts.h"
#include "settings.h"

namespace hod::ui {
namespace {

constexpr float kTabWidth = 190.0f;

std::string Get(const std::string& cvar) { return rex::cvar::GetFlagByName(cvar); }

// Set from SDL's event watch, which sees every key press as events are
// pumped; polling key state once per frame misses quick taps on slow frames.
std::atomic<bool> g_alt_enter_pressed{false};
std::atomic<bool> g_game_input_blocked{false};

bool SDLCALL WatchAltEnter(void*, SDL_Event* event) {
  if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
      (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) &&
      (event->key.mod & SDL_KMOD_ALT)) {
    g_alt_enter_pressed = true;
  }
  return true;
}

}  // namespace

// --- Hotkey watcher ------------------------------------------------------------

OptionsHotkey::OptionsHotkey(rex::ui::ImGuiDrawer* drawer, std::function<void()> on_options,
                             std::function<void()> on_fullscreen,
                             std::function<bool()> menus_open)
    : ImGuiDialog(drawer),
      on_options_(std::move(on_options)),
      on_fullscreen_(std::move(on_fullscreen)),
      menus_open_(std::move(menus_open)) {
  SDL_AddEventWatch(WatchAltEnter, nullptr);
}

OptionsHotkey::~OptionsHotkey() { SDL_RemoveEventWatch(WatchAltEnter, nullptr); }

bool GameInputBlocked() { return g_game_input_blocked.load(std::memory_order_relaxed); }

void OptionsHotkey::ShowHint() { hint_pending_ = true; }

void OptionsHotkey::OnDraw(ImGuiIO& io) {
  uint32_t buttons = ReadMenuButtons();
  bool down = (buttons & kButtonGuide) != 0;
  if (down && !was_down_) {
    hint_until_ = 0.0;
    on_options_();
  }
  was_down_ = down;

  double now = ImGui::GetTime();
  if (hint_pending_) {
    hint_pending_ = false;
    hint_until_ = now + 15.0;
  }
  if (now < hint_until_) {
    // Top-right corner, fading out over the last second.
    float alpha = float(std::min(1.0, hint_until_ - now));
    ScopedFont font(GetFonts().body);
    const char* text = "Options:  F1  or  Guide / PS button";
    ImVec2 size = ImGui::CalcTextSize(text);
    ImVec2 pad(18, 10);
    ImVec2 max(io.DisplaySize.x - 24, 24 + size.y + pad.y * 2);
    ImVec2 min(max.x - size.x - pad.x * 2, 24);
    auto faded = [alpha](ImVec4 color) {
      color.w *= alpha;
      return ImGui::GetColorU32(color);
    };
    auto* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(min, max, faded(palette::kPanel), 6.0f);
    draw->AddRect(min, max, faded(palette::kAccent), 6.0f);
    draw->AddText(ImVec2(min.x + pad.x, min.y + pad.y), faded(palette::kText),
                  text);
  }

  if (g_alt_enter_pressed.exchange(false)) on_fullscreen_();
  bool alt_held = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
  g_game_input_blocked = alt_held || io.WantTextInput || (menus_open_ && menus_open_());

  settings::UpdateAudio(SDL_GetKeyboardFocus() != nullptr);

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  // ImGui ignores A / Enter while its nav cursor is hidden, and only shows it
  // after a direction press: the first A on a Yes/No prompt did nothing.
  io.ConfigNavCursorVisibleAlways = true;
  io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (buttons & kButtonUp) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (buttons & kButtonDown) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (buttons & kButtonLeft) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (buttons & kButtonRight) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (buttons & kButtonAccept) != 0);
  io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (buttons & kButtonBack) != 0);
}

// --- Options menu ------------------------------------------------------------------

OptionsMenu::OptionsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
                         Actions actions, std::function<void()> on_close)
    : ImGuiDialog(drawer),
      config_path_(std::move(config_path)),
      actions_(std::move(actions)),
      on_close_(std::move(on_close)) {
  BuildOptions();
}

OptionsMenu::~OptionsMenu() = default;

void OptionsMenu::BuildOptions() {
  categories_ = {"System", "Input", "Audio", "Video", "Cheats"};

  // --- System
  options_.push_back({"System", "Language",
                      "Language of the game's text. The Japanese setting also uses the Japanese "
                      "title logo.",
                      "hod_language",
                      {{"English", "english"},
                       {"Japanese", "japanese"},
                       {"Deutsch", "german"},
                       {"Fran\xC3\xA7" "ais", "french"},
                       {"Espa\xC3\xB1ol", "spanish"},
                       {"Italiano", "italian"}},
                      true});
  if (actions_.show_achievements) {
    Option achievements{"System", "Achievements",
                        "Your unlocked achievements. Unlocks are saved on this PC; the game's own "
                        "Achievements menu opens the same list.",
                        "", {}};
    achievements.action = actions_.show_achievements;
    options_.push_back(std::move(achievements));
  }

  // --- Input
  options_.push_back({"Input", "Keyboard Controls",
                      "Play with the keyboard: arrows move, Z / X / C / V are A / X / B / Y, "
                      "Enter is Start. Rebind keys in the settings overlay (F4). Xbox and "
                      "PlayStation controllers always work.",
                      "mnk_mode",
                      {{"On", "true"}, {"Off", "false"}}});
  options_.push_back({"Input", "Vibration", "Controller rumble.", "input_vibration",
                      {{"On", "true"}, {"Off", "false"}}});

  // --- Audio
  Option volume{"Audio", "Master Volume", "Volume of all game audio.", "hod_volume", {}};
  for (int level = 0; level <= 100; level += 10) {
    volume.choices.push_back({level == 0 ? "Muted" : fmt::format("{}%", level),
                              std::to_string(level)});
  }
  options_.push_back(std::move(volume));
  options_.push_back({"Audio", "Mute in Background",
                      "Silence the game while you are in another window.",
                      "hod_mute_in_background",
                      {{"Off", "false"}, {"On", "true"}}});

  // --- Video
  options_.push_back({"Video", "Window Mode",
                      "Borderless fullscreen covers the whole monitor at its current resolution "
                      "(Alt+Enter also switches).",
                      "fullscreen",
                      {{"Windowed", "false"}, {"Borderless Fullscreen", "true"}}});

  Option window_size{"Video", "Window Size",
                     "Size of the window when not in fullscreen.", "window_width", {}};
  for (auto [w, h] : {std::pair{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}}) {
    window_size.choices.push_back({fmt::format("{} x {}", w, h), std::to_string(w)});
  }
  window_size.on_change = [](const std::string& value) {
    int width = std::stoi(value);
    int height = width * 9 / 16;
    rex::cvar::SetFlagByName("window_height", std::to_string(height));
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    if (windows && count > 0 && !(SDL_GetWindowFlags(windows[0]) & SDL_WINDOW_FULLSCREEN)) {
      SDL_SetWindowSize(windows[0], width, height);
      SDL_SetWindowPosition(windows[0], SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_free(windows);
  };
  options_.push_back(std::move(window_size));

  Option monitor{"Video", "Monitor", "The display the game opens on.", "monitor",
                 {{"Default", "0"}}, true};
  int display_count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
  for (int i = 0; displays && i < display_count; ++i) {
    const char* name = SDL_GetDisplayName(displays[i]);
    monitor.choices.push_back(
        {fmt::format("{}: {}", i + 1, name ? name : "Display"), std::to_string(i + 1)});
  }
  SDL_free(displays);
  if (monitor.choices.size() > 2) options_.push_back(std::move(monitor));

  options_.push_back({"Video", "Internal Resolution",
                      "Renders the game at a multiple of the Xbox 360's 1280 x 720. Higher is "
                      "sharper and needs a stronger GPU.",
                      "resolution_scale",
                      {{"1x  (1280 x 720)", "1"}, {"2x  (2560 x 1440)", "2"}, {"3x  (3840 x 2160)", "3"}},
                      true});

  options_.push_back({"Video", "V-Sync",
                      "Waits for the display's refresh to avoid tearing. The game runs at 60 "
                      "frames per second either way, like on the console.",
                      "vsync",
                      {{"On", "true"}, {"Off", "false"}}});

  options_.push_back({"Video", "Aspect Ratio",
                      "Keep the game's 16:9 picture with black bars on wider or taller screens, "
                      "or stretch it to fill the window.",
                      "present_letterbox",
                      {{"Keep 16:9", "true"}, {"Stretch", "false"}}});

  Option filter{"Video", "Upscaling Filter",
                "How the image is scaled to your window or screen.", "present_effect", {}, true};
  const std::pair<const char*, const char*> kFilters[] = {
      {"bilinear", "Bilinear"},
      {"cas", "AMD FidelityFX CAS (sharpen)"},
      {"fsr", "AMD FidelityFX FSR 1"},
  };
  if (const auto* info = rex::cvar::GetFlagInfo("present_effect")) {
    for (const auto& [value, label] : kFilters) {
      const auto& allowed = info->constraints.allowed_values;
      if (std::find(allowed.begin(), allowed.end(), value) != allowed.end()) {
        filter.choices.push_back({label, value});
      }
    }
  }
  if (filter.choices.size() > 1) options_.push_back(std::move(filter));

  options_.push_back({"Video", "Texture Filtering",
                      "Anisotropic filtering keeps textures sharp at angles.",
                      "anisotropic_override",
                      {{"Game Default", "-1"},
                       {"Off", "0"},
                       {"2x", "2"},
                       {"4x", "3"},
                       {"8x", "4"},
                       {"16x", "5"}}});

  // --- Cheats
  const char* kCheatNote = " Use from the main menu or the castle lobby, not in a chapter.";
  auto cheat = [&](const char* label, const char* description, uint32_t which) {
    Option option{"Cheats", label, std::string(description) + kCheatNote, "", {}};
    option.action = [which] { cheats::Request(which); };
    option.action_label = "Apply";
    option.confirm = true;
    options_.push_back(std::move(option));
  };
  cheat("Unlock All Chapters", "Marks chapters 1-6 cleared on Normal and Hard, which opens every "
        "chapter and Hard mode.", cheats::kUnlockChapters);
  cheat("Give All Items", "Adds every weapon, armor, sub-weapon, soul, glyph and consumable, "
        "DLC included, as many as each stacks.", cheats::kAllItems);
  cheat("+100,000 Gold", "Adds 100,000 gold.", cheats::kAddGold);
  cheat("Max Gold", "Sets gold to 99,999,999.", cheats::kMaxGold);

  // Drop settings whose cvar this build doesn't have (e.g. an SDK without
  // the project's patches), so the menu never shows a dead row.
  std::erase_if(options_, [](const Option& option) {
    return !option.action && !rex::cvar::GetFlagInfo(option.cvar);
  });
}

std::vector<OptionsMenu::Option*> OptionsMenu::VisibleOptions() {
  std::vector<Option*> visible;
  for (auto& option : options_) {
    if (option.category == categories_[category_]) visible.push_back(&option);
  }
  return visible;
}

int OptionsMenu::CurrentChoice(const Option& option) const {
  std::string value = Get(option.cvar);
  for (size_t i = 0; i < option.choices.size(); ++i) {
    if (option.choices[i].value == value) return int(i);
  }
  return 0;
}

void OptionsMenu::Change(Option& option, int delta) {
  if (option.action) {
    if (delta <= 0) return;
    if (option.confirm && armed_ != &option) {
      armed_ = &option;  // Applied on the next press.
      return;
    }
    armed_ = nullptr;
    option.action();
    return;
  }
  int count = int(option.choices.size());
  int next = (CurrentChoice(option) + delta + count) % count;
  const auto& value = option.choices[next].value;
  if (!rex::cvar::SetFlagByName(option.cvar, value)) {
    REXLOG_WARN("Options: could not set {} to {}", option.cvar, value);
    return;
  }
  if (option.on_change) option.on_change(value);
  changed_ = true;
}

void OptionsMenu::Close() {
  if (closed_) return;
  closed_ = true;
  if (changed_ && !config_path_.empty()) {
    rex::cvar::SaveConfig(config_path_);
    REXLOG_INFO("Options saved to {}", rex::path_to_utf8(config_path_));
  }
  if (on_close_) on_close_();
}

void OptionsMenu::OnDraw(ImGuiIO& io) {
  if (closed_) return;
  auto visible = VisibleOptions();
  selected_ = std::clamp(selected_, 0, std::max(0, int(visible.size()) - 1));

  const int previous_selected = selected_, previous_category = category_;
  switch (navigator_.Read()) {
    case Nav::kUp:
      selected_ = (selected_ + int(visible.size()) - 1) % std::max<int>(1, int(visible.size()));
      break;
    case Nav::kDown:
      selected_ = (selected_ + 1) % std::max<int>(1, int(visible.size()));
      break;
    case Nav::kLeft:
      if (!visible.empty()) Change(*visible[selected_], -1);
      break;
    case Nav::kRight:
    case Nav::kAccept:
      if (!visible.empty()) Change(*visible[selected_], +1);
      break;
    case Nav::kNextTab:
      category_ = (category_ + 1) % int(categories_.size());
      selected_ = 0;
      break;
    case Nav::kPrevTab:
      category_ = (category_ + int(categories_.size()) - 1) % int(categories_.size());
      selected_ = 0;
      break;
    case Nav::kBack:
      close_requested_ = true;
      break;
    case Nav::kNone:
      break;
  }
  if (selected_ != previous_selected || category_ != previous_category) armed_ = nullptr;
  if (close_requested_) {
    Close();
    return;
  }

  Panel panel("##options", io);
  PanelHeading("Options");

  // Category tabs.
  ImGui::BeginChild("tabs", ImVec2(kTabWidth, kPanelHeight - 210.0f));
  for (int i = 0; i < int(categories_.size()); ++i) {
    bool active = i == category_;
    ImGui::PushStyleColor(ImGuiCol_Text, active ? palette::kGold : palette::kDim);
    if (ImGui::Selectable(categories_[i].c_str(), active, 0, ImVec2(0, 34))) {
      category_ = i;
      selected_ = 0;
    }
    ImGui::PopStyleColor();
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // Settings of the current category.
  visible = VisibleOptions();
  selected_ = std::clamp(selected_, 0, std::max(0, int(visible.size()) - 1));
  ImGui::BeginChild("settings", ImVec2(0, kPanelHeight - 210.0f));
  float value_x = ImGui::GetContentRegionAvail().x - 330.0f;
  constexpr float kSlot = 260.0f;
  for (int i = 0; i < int(visible.size()); ++i) {
    auto& option = *visible[i];
    ImGui::PushID(i);
    ImVec2 row = ImGui::GetCursorPos();
    if (ImGui::Selectable("##row", i == selected_, ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(0, 38))) {
      selected_ = i;
      if (option.action) Change(option, +1);
    }
    ImGui::SetCursorPos(ImVec2(row.x + 12, row.y + 8));
    ImGui::TextUnformatted(option.label.c_str());
    if (option.action) {
      const char* open = armed_ == &option ? "Press again to apply" : option.action_label.c_str();
      float text_w = ImGui::CalcTextSize(open).x;
      ImGui::SetCursorPos(ImVec2(row.x + value_x + 30.0f + (kSlot - text_w) * 0.5f, row.y + 8));
      ImGui::TextColored(i == selected_ ? palette::kGold : palette::kText, "%s", open);
    } else {
      ImGui::SetCursorPos(ImVec2(row.x + value_x, row.y + 6));
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette::kAccentSoft);
      if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
        selected_ = i;
        Change(option, -1);
      }
      ImGui::SameLine();
      const std::string& label = option.choices[CurrentChoice(option)].label;
      float text_w = ImGui::CalcTextSize(label.c_str()).x;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kSlot - text_w) * 0.5f);
      ImGui::TextColored(i == selected_ ? palette::kGold : palette::kText, "%s", label.c_str());
      ImGui::SameLine();
      ImGui::SetCursorPosX(row.x + value_x + 30.0f + kSlot);
      if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
        selected_ = i;
        Change(option, +1);
      }
      ImGui::PopStyleColor(2);
    }
    // Move below the row and submit an item there, as ImGui requires when
    // the cursor is used to lay out overlapping widgets.
    ImGui::SetCursorPos(ImVec2(row.x, row.y + 42));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
  }
  ImGui::EndChild();

  // Description of the highlighted setting.
  ImGui::Separator();
  ImGui::Spacing();
  if (!visible.empty()) {
    const auto& option = *visible[selected_];
    ImGui::PushTextWrapPos(kPanelWidth - 40.0f);
    ImGui::TextColored(palette::kDim, "%s", option.description.c_str());
    ImGui::PopTextWrapPos();
    if (option.restart) {
      ImGui::TextColored(palette::kWarn, "Takes effect after restarting the game.");
    }
    if (option.category == "Cheats") {
      std::string result = cheats::LastResult();
      if (!result.empty()) ImGui::TextColored(palette::kGold, "Done: %s", result.c_str());
    }
  }

  PanelFooter(
      "Up/Down  select     Left/Right  change     LB/RB or Tab  category     B / Esc  close");
}

}  // namespace hod::ui
