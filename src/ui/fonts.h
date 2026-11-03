// harmony-recomp - UI fonts
//
// The SDK's ImGui atlas only bakes its tiny debug font, at a fixed size, so
// the app's own screens bake theirs here (from fonts/ next to the
// executable, staged from assets/fonts at build time).

#pragma once

#include <filesystem>

struct ImFont;
struct ImFontAtlas;
struct ImGuiStyle;

namespace hod::ui {

struct Fonts {
  ImFont* body = nullptr;     // Roboto, running text
  ImFont* caption = nullptr;  // Roboto, captions and hints (not "small": rpcndr.h #defines that)
  ImFont* heading = nullptr;  // Cinzel, page headings
  ImFont* title = nullptr;    // Cinzel, the big title
};

// Null members mean the font files were missing; callers fall back to the
// default font.
const Fonts& GetFonts();
void LoadFonts(ImFontAtlas* atlas, const std::filesystem::path& font_dir);

// Theme for every ImGui window, including the SDK's overlays and the
// system dialogs it draws for the game (message boxes, save prompts).
void ApplyTheme(ImGuiStyle& style);

// Pushes a font at its baked size for the lifetime of the object.
class ScopedFont {
 public:
  explicit ScopedFont(ImFont* font);
  ~ScopedFont();
  ScopedFont(const ScopedFont&) = delete;
  ScopedFont& operator=(const ScopedFont&) = delete;

 private:
  bool pushed_ = false;
};

}  // namespace hod::ui
