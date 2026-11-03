// hodrecomp - cheats (Options > Cheats)
//
// The options menu queues a cheat; it runs on a game thread the next time the
// game asks for its global state (see cheats.cpp), using the game's own
// functions where it can so its limits and bookkeeping stay intact.
//
// The game copies gold and items into a chapter when it starts and back when
// it ends, so cheats belong in the main menu or the castle lobby.

#pragma once

#include <cstdint>
#include <string>

namespace hod::cheats {

enum Cheat : uint32_t {
  kAddGold = 1u << 0,          // +100,000 gold
  kMaxGold = 1u << 1,          // 99,999,999 gold
  kUnlockChapters = 1u << 2,   // chapters 1-6 cleared on Normal and Hard
  kAllItems = 1u << 3,         // every weapon, armor, sub-weapon, soul,
                               // glyph and consumable, as many as stack
};

// Thread-safe; runs at the next opportunity.
void Request(uint32_t cheats);

// What the last cheats did, for the menu ("" until one ran).
std::string LastResult();

}  // namespace hod::cheats
