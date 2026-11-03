// hodrecomp - settings the app applies itself (the SDK's own cvars apply
// themselves)

#pragma once

namespace hod::settings {

// Applies hod_volume and hod_mute_in_background to the SDK's audio output.
// Cheap; call once per UI frame.
void UpdateAudio(bool window_focused);

}  // namespace hod::settings
