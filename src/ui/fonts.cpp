// harmony-recomp - UI fonts (see fonts.h)

#include "fonts.h"

#include <imgui.h>

#include <rex/filesystem.h>
#include <rex/logging.h>

namespace hod::ui {
namespace {

Fonts g_fonts;

ImFont* Load(ImFontAtlas* atlas, const std::filesystem::path& path, float size) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    REXLOG_WARN("UI font missing: {}", rex::path_to_utf8(path));
    return nullptr;
  }
  ImFontConfig config;
  config.OversampleH = 2;
  config.OversampleV = 2;
  return atlas->AddFontFromFileTTF(rex::path_to_utf8(path).c_str(), size, &config);
}

}  // namespace

const Fonts& GetFonts() { return g_fonts; }

void LoadFonts(ImFontAtlas* atlas, const std::filesystem::path& font_dir) {
  g_fonts.body = Load(atlas, font_dir / "Roboto.ttf", 19.0f);
  g_fonts.caption = Load(atlas, font_dir / "Roboto.ttf", 15.0f);
  g_fonts.heading = Load(atlas, font_dir / "Cinzel.ttf", 28.0f);
  g_fonts.title = Load(atlas, font_dir / "Cinzel.ttf", 54.0f);
  // System dialogs and overlays use the default font; make it readable.
  if (g_fonts.body) ImGui::GetIO().FontDefault = g_fonts.body;
}

void ApplyTheme(ImGuiStyle& style) {
  const ImVec4 accent(0.55f, 0.10f, 0.14f, 1.0f);
  const ImVec4 accent_hover(0.70f, 0.16f, 0.20f, 1.0f);
  const ImVec4 accent_active(0.80f, 0.22f, 0.24f, 1.0f);
  const ImVec4 gold(0.93f, 0.78f, 0.45f, 1.0f);
  auto* c = style.Colors;
  c[ImGuiCol_Text] = ImVec4(0.92f, 0.89f, 0.85f, 1.0f);
  c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.04f, 0.05f, 0.96f);
  c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.04f, 0.05f, 0.98f);
  c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  c[ImGuiCol_Border] = accent;
  c[ImGuiCol_TitleBg] = accent;
  c[ImGuiCol_TitleBgActive] = accent;
  c[ImGuiCol_TitleBgCollapsed] = accent;
  c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.10f, 0.11f, 1.0f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.13f, 0.15f, 1.0f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.15f, 0.17f, 1.0f);
  c[ImGuiCol_Button] = accent;
  c[ImGuiCol_ButtonHovered] = accent_hover;
  c[ImGuiCol_ButtonActive] = accent_active;
  c[ImGuiCol_Header] = ImVec4(0.55f, 0.10f, 0.14f, 0.45f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.10f, 0.14f, 0.65f);
  c[ImGuiCol_HeaderActive] = accent;
  c[ImGuiCol_CheckMark] = gold;
  c[ImGuiCol_SliderGrab] = gold;
  c[ImGuiCol_SliderGrabActive] = gold;
  c[ImGuiCol_Separator] = accent;
  c[ImGuiCol_ScrollbarGrab] = accent;
  c[ImGuiCol_ScrollbarGrabHovered] = accent_hover;
  c[ImGuiCol_ScrollbarGrabActive] = gold;
  c[ImGuiCol_PlotHistogram] = accent_hover;
  c[ImGuiCol_NavCursor] = gold;
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
  style.WindowRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.WindowPadding = ImVec2(18, 14);
  style.FramePadding = ImVec2(12, 6);
  style.ItemSpacing = ImVec2(10, 8);
}

ScopedFont::ScopedFont(ImFont* font) {
  if (font) {
    ImGui::PushFont(font, font->LegacySize);
    pushed_ = true;
  }
}

ScopedFont::~ScopedFont() {
  if (pushed_) ImGui::PopFont();
}

}  // namespace hod::ui
