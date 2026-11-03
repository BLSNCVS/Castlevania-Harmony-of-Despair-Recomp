// hodrecomp - game language and region
//
// Harmony of Despair ships text for six languages (data/text/textres_*.txrc)
// and picks one from XGetLanguage. It also reads XGetGameRegion, comparing it
// against exact region codes to choose things like the title logo. The SDK
// answers English for every title and 0xFFFF ("region free") for the region,
// which matches none of the game's cases and selects the Japanese title logo.
// Both are overridden here from the hod_language setting.

#include <array>
#include <cctype>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "language.h"

REXCVAR_DEFINE_STRING(hod_language, "english", "HoD",
                      "Game language: english, japanese, german, french, spanish, italian "
                      "(takes effect on restart)")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace hod {
namespace {

struct Language {
  std::string_view name;
  uint32_t xlanguage;    // XC_LANGUAGE_* as returned by XGetLanguage.
  uint32_t game_region;  // XC_GAME_REGION_* the game expects to go with it.
};

// Only the languages the game has text for. The region keeps the game's own
// language/logo logic consistent: Japan for Japanese, North America otherwise.
constexpr std::array<Language, 6> kLanguages = {{
    {"english", 1, 0x00FF},
    {"japanese", 2, 0x0101},
    {"german", 3, 0x00FF},
    {"french", 4, 0x00FF},
    {"spanish", 5, 0x00FF},
    {"italian", 6, 0x00FF},
}};

const Language& CurrentLanguage() {
  static const Language& language = [] () -> const Language& {
    std::string wanted = REXCVAR_GET(hod_language);
    for (auto& c : wanted) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& l : kLanguages) {
      if (l.name == wanted) return l;
    }
    REXLOG_WARN("Unknown hod_language '{}', using english", wanted);
    return kLanguages[0];
  }();
  return language;
}

uint32_t HodXGetLanguage() { return CurrentLanguage().xlanguage; }
uint32_t HodXGetGameRegion() { return CurrentLanguage().game_region; }

}  // namespace

uint32_t LanguageId() { return CurrentLanguage().xlanguage; }

}  // namespace hod

// Strong definitions in the executable take precedence over the SDK's.
REX_HOOK(__imp__XGetLanguage, hod::HodXGetLanguage)
REX_HOOK(__imp__XGetGameRegion, hod::HodXGetGameRegion)
