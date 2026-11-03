// hodrecomp - cheats (see cheats.h)
//
// Everything here comes from reading the recompiled game:
//  - sub_82223348 returns the game's global state; the save data (the
//    15104-byte block written to savedata.dat) starts 4120 bytes into it.
//    sub_82224608 resets it, which gives the layout used below.
//  - Gold: u32 at save+9332, capped at 99,999,999 by the game
//    (sub_82459050, which also awards "Wicked Deep Pockets" at 1,000,000).
//  - Chapters: one byte per chapter and difficulty at save+14012 (Normal) and
//    +14013 (Hard), chapters 1-32. A chapter opens once the one before is
//    cleared, Hard mode once chapter 6 is cleared on Normal; DLC chapters are
//    open on Normal from the start (sub_8240B550).
//  - Items: one byte per item id at save+1000 (8331 ids): count in the high
//    nibble, "obtained" and "new" flags in bits 0-1. sub_82224B48(save, id,
//    delta) adds items the way the game does, capped at the item's stack size.
//    Ids are grouped in ranges, each backed by one item table (weapons,
//    armor, ...); sub_8246D8E8 finds an item's record in its table (null if
//    there is none), base game and DLC alike (sub_823C98B0 shows the ranges).

#include "cheats.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/logging.h>

REX_EXTERN(__imp__sub_82223348);  // GetGame
REX_EXTERN(sub_8246D8E8);         // find item record
REX_EXTERN(sub_82224B48);         // add items to the save

namespace hod::cheats {
namespace {

constexpr uint32_t kSaveOffset = 4120;
constexpr uint32_t kGold = 9332;
constexpr uint32_t kMaxGoldValue = 99'999'999;
constexpr uint32_t kChapterCleared = 14012;

struct ItemRange {
  uint32_t first_id;
  uint32_t count;
  uint32_t table;        // guest address of the pointer to the item table
  uint32_t record_size;
};

constexpr ItemRange kItemRanges[] = {
    {51, 500, 0x8291D970, 150},     // weapons
    {551, 1000, 0x8291D984, 78},    // armor
    {1551, 100, 0x8291D96C, 70},    // sub-weapons
    {1651, 200, 0x8291D994, 60},    // souls
    {1861, 200, 0x8291D988, 58},    // consumables
    {2061, 100, 0x8291D980, 62},    // glyphs
};

std::atomic<uint32_t> g_pending{0};
std::mutex g_result_mutex;
std::string g_result;
thread_local bool t_running = false;

uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, 4);
  return __builtin_bswap32(value);
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(base + address, &value, 4);
}

void SetResult(std::string text) {
  REXLOG_INFO("Cheats: {}", text);
  std::lock_guard lock(g_result_mutex);
  g_result = std::move(text);
}

// Adds as many of every existing item as its stack holds. Returns how many
// different items exist.
int GiveAllItems(PPCContext& ctx, uint8_t* base, uint32_t save) {
  int found = 0;
  for (const auto& range : kItemRanges) {
    uint32_t table = Load32(base, range.table);
    if (!table || !Load32(base, table + 4)) continue;  // Not loaded (yet).
    for (uint32_t local = 0; local < range.count; ++local) {
      uint32_t id = range.first_id + local;
      ctx.r3.u64 = Load32(base, table + 4);
      ctx.r4.u64 = table + 12;
      ctx.r5.u64 = local;
      ctx.r6.u64 = range.record_size;
      sub_8246D8E8(ctx, base);
      if (!ctx.r3.u32) continue;
      ctx.r3.u64 = save;
      ctx.r4.u64 = id;
      ctx.r5.u64 = 9;
      sub_82224B48(ctx, base);
      ++found;
    }
  }
  return found;
}

void Run(uint32_t cheats, PPCContext& ctx, uint8_t* base) {
  __imp__sub_82223348(ctx, base);
  if (!ctx.r3.u32) {
    SetResult("the game isn't ready yet; try again from the main menu");
    return;
  }
  const uint32_t save = ctx.r3.u32 + kSaveOffset;
  std::string done;
  auto note = [&done](const std::string& text) {
    if (!done.empty()) done += "; ";
    done += text;
  };

  if (cheats & (kAddGold | kMaxGold)) {
    uint32_t gold = Load32(base, save + kGold);
    gold = (cheats & kMaxGold) ? kMaxGoldValue
                               : std::min<uint64_t>(uint64_t(gold) + 100'000, kMaxGoldValue);
    Store32(base, save + kGold, gold);
    note(fmt::format("gold is now {}", gold));
  }
  if (cheats & kUnlockChapters) {
    for (uint32_t chapter = 0; chapter < 6; ++chapter) {
      base[save + kChapterCleared + chapter * 2] = 1;      // Normal
      base[save + kChapterCleared + chapter * 2 + 1] = 1;  // Hard
    }
    note("chapters 1-6 cleared on Normal and Hard");
  }
  if (cheats & kAllItems) {
    int found = GiveAllItems(ctx, base, save);
    note(found ? fmt::format("{} kinds of items added", found)
               : std::string("items not available yet (try from the main menu)"));
  }
  SetResult(done);
}

}  // namespace

void Request(uint32_t cheats) { g_pending.fetch_or(cheats); }

std::string LastResult() {
  std::lock_guard lock(g_result_mutex);
  return g_result;
}

}  // namespace hod::cheats

// The game asks for its global state constantly, from its own threads, so this
// is a safe moment to run game code. The registers are restored afterwards,
// so the caller sees nothing but the state pointer it asked for.
extern "C" REX_FUNC(sub_82223348) {
  using namespace hod::cheats;
  if (!t_running && g_pending.load(std::memory_order_relaxed)) {
    uint32_t cheats = g_pending.exchange(0);
    if (cheats) {
      t_running = true;
      PPCContext saved = ctx;
      Run(cheats, ctx, base);
      ctx = saved;
      t_running = false;
    }
  }
  __imp__sub_82223348(ctx, base);
}
