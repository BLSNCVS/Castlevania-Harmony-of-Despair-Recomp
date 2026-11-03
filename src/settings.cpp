// hodrecomp - settings the app applies itself (see settings.h)

#include "settings.h"

#include <algorithm>

#include <rex/audio/downmix.h>
#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(hod_volume, 100, "HoD", "Master volume, 0-100");
REXCVAR_DEFINE_BOOL(hod_mute_in_background, false, "HoD",
                    "Mute the game while its window is not focused");

namespace hod::settings {

void UpdateAudio(bool window_focused) {
  float gain = 0.0f;
  if (window_focused || !REXCVAR_GET(hod_mute_in_background)) {
    // Squared, so the steps sound even: loudness is roughly logarithmic.
    float level = float(std::clamp(REXCVAR_GET(hod_volume), 0, 100)) / 100.0f;
    gain = level * level;
  }
  if (rex::audio::GetOutputGain() != gain) rex::audio::SetOutputGain(gain);
}

}  // namespace hod::settings
