// harmony-recomp - shared look and controls of the in-game menus

#include "menu_common.h"

#include <SDL3/SDL_gamepad.h>

namespace hod::ui {
namespace {

constexpr uint32_t kDirections = kButtonUp | kButtonDown | kButtonLeft | kButtonRight;

}  // namespace

uint32_t ReadMenuButtons() {
  uint32_t buttons = 0;
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int i = 0; ids && i < count; ++i) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (!pad) continue;
    auto pressed = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    int16_t x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    int16_t y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    constexpr int16_t kDead = 20000;
    if (pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -kDead) buttons |= kButtonUp;
    if (pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > kDead) buttons |= kButtonDown;
    if (pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -kDead) buttons |= kButtonLeft;
    if (pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > kDead) buttons |= kButtonRight;
    // South/East are A/B on Xbox pads and Cross/Circle on PlayStation pads.
    if (pressed(SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= kButtonAccept;
    if (pressed(SDL_GAMEPAD_BUTTON_EAST)) buttons |= kButtonBack;
    if (pressed(SDL_GAMEPAD_BUTTON_GUIDE)) buttons |= kButtonGuide;
    if (pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) buttons |= kButtonPrevTab;
    if (pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) buttons |= kButtonNextTab;
  }
  SDL_free(ids);
  return buttons;
}

MenuNavigator::MenuNavigator() : previous_buttons_(ReadMenuButtons()) {}

Nav MenuNavigator::Read() {
  // Keyboard (ImGui handles key repeat).
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) return Nav::kUp;
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) return Nav::kDown;
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) return Nav::kLeft;
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) return Nav::kRight;
  if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false))
    return Nav::kAccept;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
    return Nav::kBack;
  if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
    return ImGui::GetIO().KeyShift ? Nav::kPrevTab : Nav::kNextTab;

  // Controllers: act on presses, repeat directions while held.
  uint32_t buttons = ReadMenuButtons();
  uint32_t pressed = buttons & ~previous_buttons_;
  previous_buttons_ = buttons;
  double now = ImGui::GetTime();
  uint32_t held_direction = buttons & kDirections;
  if (pressed & kDirections) {
    repeat_at_ = now + 0.35;
  } else if (held_direction && now >= repeat_at_) {
    pressed |= held_direction;
    repeat_at_ = now + 0.09;
  }
  if (pressed & kButtonUp) return Nav::kUp;
  if (pressed & kButtonDown) return Nav::kDown;
  if (pressed & kButtonLeft) return Nav::kLeft;
  if (pressed & kButtonRight) return Nav::kRight;
  if (pressed & kButtonAccept) return Nav::kAccept;
  if (pressed & (kButtonBack | kButtonGuide)) return Nav::kBack;
  if (pressed & kButtonPrevTab) return Nav::kPrevTab;
  if (pressed & kButtonNextTab) return Nav::kNextTab;
  return Nav::kNone;
}

Panel::~Panel() {
  font_.reset();  // Popped before the window ends.
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(11);
}

Panel::Panel(const char* id, ImGuiIO& io) {
  // Dim the game behind the menu.
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                                                IM_COL32(0, 0, 0, 150));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(kPanelWidth, kPanelHeight), ImGuiCond_Always);
  ImGui::SetNextWindowFocus();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, palette::kPanel);
  ImGui::PushStyleColor(ImGuiCol_Border, palette::kAccent);
  ImGui::PushStyleColor(ImGuiCol_Header, palette::kAccentSoft);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, palette::kAccentFaint);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, palette::kAccent);
  ImGui::PushStyleColor(ImGuiCol_Separator, palette::kAccent);
  ImGui::PushStyleColor(ImGuiCol_Text, palette::kText);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, palette::kAccentSoft);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, palette::kAccent);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, palette::kAccent);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 22));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs);
  font_.emplace(GetFonts().body);
}

void PanelHeading(const char* title) {
  {
    ScopedFont heading(GetFonts().heading);
    ImGui::TextColored(palette::kGold, "%s", title);
  }
  ImGui::Separator();
  ImGui::Spacing();
}

void PanelFooter(const char* text) {
  ScopedFont caption_font(GetFonts().caption);
  ImGui::SetCursorPosY(kPanelHeight - ImGui::GetTextLineHeightWithSpacing() - 18.0f);
  ImGui::TextColored(palette::kDim, "%s", text);
}

}  // namespace hod::ui
