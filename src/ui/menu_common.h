// harmony-recomp - shared look and controls of the in-game menus
//
// The options menu and the achievements screen share one panel style and
// read controllers the same way: straight from SDL, so they work whatever
// the game is doing with its own input.

#pragma once

#include <cstdint>
#include <optional>

#include <imgui.h>

#include "fonts.h"

namespace hod::ui {

namespace palette {
inline const ImVec4 kGold(0.93f, 0.78f, 0.45f, 1.0f);
inline const ImVec4 kText(0.92f, 0.89f, 0.85f, 1.0f);
inline const ImVec4 kDim(0.66f, 0.61f, 0.58f, 1.0f);
inline const ImVec4 kWarn(0.95f, 0.65f, 0.35f, 1.0f);
inline const ImVec4 kPanel(0.07f, 0.04f, 0.05f, 0.96f);
inline const ImVec4 kAccent(0.55f, 0.10f, 0.14f, 1.0f);
inline const ImVec4 kAccentSoft(0.55f, 0.10f, 0.14f, 0.45f);
inline const ImVec4 kAccentFaint(0.55f, 0.10f, 0.14f, 0.20f);
inline const ImVec4 kAccentHover(0.70f, 0.16f, 0.20f, 1.0f);
inline const ImVec4 kGood(0.55f, 0.85f, 0.55f, 1.0f);
// The installer's full-screen gradient.
inline constexpr ImU32 kBackgroundTop = IM_COL32(46, 6, 12, 255);
inline constexpr ImU32 kBackgroundBottom = IM_COL32(6, 2, 4, 255);
}  // namespace palette

constexpr float kPanelWidth = 980.0f;
constexpr float kPanelHeight = 600.0f;

enum MenuButton : uint32_t {
  kButtonUp = 1u << 0,
  kButtonDown = 1u << 1,
  kButtonLeft = 1u << 2,
  kButtonRight = 1u << 3,
  kButtonAccept = 1u << 4,
  kButtonBack = 1u << 5,
  kButtonGuide = 1u << 6,
  kButtonPrevTab = 1u << 7,
  kButtonNextTab = 1u << 8,
};

// Bit mask of MenuButton for every connected SDL gamepad.
uint32_t ReadMenuButtons();

enum class Nav { kNone, kUp, kDown, kLeft, kRight, kAccept, kBack, kNextTab, kPrevTab };

// Keyboard and controller navigation with press detection and key repeat on
// held directions. Call Read() once per frame.
class MenuNavigator {
 public:
  // Ignores whatever is held right now (e.g. the button that opened the menu).
  MenuNavigator();
  Nav Read();

 private:
  uint32_t previous_buttons_ = 0;
  double repeat_at_ = 0.0;
};

// Centered panel with the menus' colors, in the body font, for the lifetime
// of the object.
class Panel {
 public:
  Panel(const char* id, ImGuiIO& io);
  ~Panel();
  Panel(const Panel&) = delete;
  Panel& operator=(const Panel&) = delete;

 private:
  std::optional<ScopedFont> font_;
};

// The panel's title line.
void PanelHeading(const char* title);

// Hint line along the bottom of the panel.
void PanelFooter(const char* text);

}  // namespace hod::ui
