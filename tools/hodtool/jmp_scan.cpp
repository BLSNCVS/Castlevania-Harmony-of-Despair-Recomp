// harmony-recomp - setjmp/longjmp locator (see jmp_scan.h)

#include "jmp_scan.h"

#include <algorithm>
#include <array>

namespace hod {
namespace {

constexpr uint32_t kBlr = 0x4E800020;
constexpr uint32_t kBctr = 0x4E800420;
constexpr uint32_t kLiR3Zero = 0x38600000;   // li r3, 0
constexpr uint32_t kMrR3R4 = 0x7C832378;     // mr r3, r4
constexpr size_t kMaxFunctionWords = 160;    // setjmp/longjmp are short.
constexpr int kMinGprs = 15;                 // r14..r31 is 18; leave slack.
constexpr int kMaxOppositeAccesses = 3;      // setjmp barely loads, longjmp barely stores.

uint32_t ReadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t Opcode(uint32_t insn) { return insn >> 26; }
uint32_t RegS(uint32_t insn) { return (insn >> 21) & 31; }
uint32_t RegA(uint32_t insn) { return (insn >> 16) & 31; }
int32_t DispD(uint32_t insn) { return int16_t(insn & 0xFFFF); }
int32_t DispDS(uint32_t insn) { return int16_t(insn & 0xFFFC); }

bool IsUnconditionalBranch(uint32_t insn) { return Opcode(insn) == 18 && (insn & 3) == 0; }
bool IsMflr(uint32_t insn) { return (insn & 0xFC1FFFFF) == 0x7C0802A6; }
bool IsMtlr(uint32_t insn) { return (insn & 0xFC1FFFFF) == 0x7C0803A6; }
bool IsMtctr(uint32_t insn) { return (insn & 0xFC1FFFFF) == 0x7C0903A6; }

// A word that typically ends the previous function, making the next word a
// plausible function entry.
bool EndsFunction(uint32_t insn) {
  return insn == kBlr || insn == kBctr || insn == 0 || IsUnconditionalBranch(insn);
}

enum class Access { kNone, kStoreGpr, kLoadGpr, kStoreFpr, kLoadFpr };

struct MemOp {
  Access access = Access::kNone;
  uint32_t reg = 0;
  uint32_t base = 0;
  int32_t offset = 0;
};

MemOp DecodeMemOp(uint32_t insn) {
  MemOp op;
  op.reg = RegS(insn);
  op.base = RegA(insn);
  switch (Opcode(insn)) {
    case 62:  // std (xo 0)
      if ((insn & 3) == 0) {
        op.access = Access::kStoreGpr;
        op.offset = DispDS(insn);
      }
      break;
    case 58:  // ld (xo 0)
      if ((insn & 3) == 0) {
        op.access = Access::kLoadGpr;
        op.offset = DispDS(insn);
      }
      break;
    case 36:  // stw
      op.access = Access::kStoreGpr;
      op.offset = DispD(insn);
      break;
    case 32:  // lwz
      op.access = Access::kLoadGpr;
      op.offset = DispD(insn);
      break;
    case 54:  // stfd
      op.access = Access::kStoreFpr;
      op.offset = DispD(insn);
      break;
    case 50:  // lfd
      op.access = Access::kLoadFpr;
      op.offset = DispD(insn);
      break;
    default:
      break;
  }
  // r0 as a base means literal zero and r1 is the stack pointer: the
  // compiler's __savegprlr_*/__restgprlr_* helpers save via r1 and must not
  // be mistaken for setjmp.
  if (op.base == 0 || op.base == 1) {
    op.access = Access::kNone;
  }
  return op;
}

bool IsNonVolatileGpr(uint32_t reg) { return reg >= 13; }
bool IsNonVolatileFpr(uint32_t reg) { return reg >= 14; }

std::optional<JmpCandidate> Classify(uint32_t address, const uint8_t* words, size_t count) {
  // First pass: find the extent and the dominant base register.
  std::array<int, 32> base_votes{};
  size_t length = 0;
  for (size_t i = 0; i < count && i < kMaxFunctionWords; ++i) {
    uint32_t insn = ReadBE32(words + i * 4);
    length = i + 1;
    MemOp op = DecodeMemOp(insn);
    if ((op.access == Access::kStoreGpr || op.access == Access::kLoadGpr) &&
        IsNonVolatileGpr(op.reg) && op.reg != op.base) {
      base_votes[op.base]++;
    }
    if (insn == kBlr || insn == kBctr) {
      break;
    }
  }
  uint32_t base = uint32_t(std::max_element(base_votes.begin(), base_votes.end()) -
                           base_votes.begin());
  if (base_votes[base] < kMinGprs) {
    return std::nullopt;
  }

  JmpCandidate c{};
  c.address = address;
  c.length = uint32_t(length * 4);
  c.base_reg = base;
  // Offsets may be negative, so "seen" is tracked separately from the offset.
  struct Slots {
    int offset[32] = {};
    bool seen[32] = {};
    int count = 0;
    void Add(uint32_t reg, int32_t off) {
      if (!seen[reg]) {
        seen[reg] = true;
        offset[reg] = off;
        count++;
      }
    }
  } stored_gprs, loaded_gprs, stored_fprs, loaded_fprs;
  bool saw_mflr = false, saw_restore_branch = false, saw_li_zero = false, saw_mr_r3_r4 = false;

  for (size_t i = 0; i < length; ++i) {
    uint32_t insn = ReadBE32(words + i * 4);
    saw_mflr |= IsMflr(insn);
    saw_restore_branch |= IsMtlr(insn) || IsMtctr(insn);
    saw_li_zero |= insn == kLiR3Zero;
    saw_mr_r3_r4 |= insn == kMrR3R4;
    MemOp op = DecodeMemOp(insn);
    if (op.base != base) {
      continue;
    }
    switch (op.access) {
      case Access::kStoreGpr:
        if (IsNonVolatileGpr(op.reg)) stored_gprs.Add(op.reg, op.offset);
        break;
      case Access::kLoadGpr:
        if (IsNonVolatileGpr(op.reg)) loaded_gprs.Add(op.reg, op.offset);
        break;
      case Access::kStoreFpr:
        if (IsNonVolatileFpr(op.reg)) stored_fprs.Add(op.reg, op.offset);
        break;
      case Access::kLoadFpr:
        if (IsNonVolatileFpr(op.reg)) loaded_fprs.Add(op.reg, op.offset);
        break;
      case Access::kNone:
        break;
    }
  }

  auto export_slots = [](const Slots& slots, int* out) {
    for (int r = 0; r < 32; ++r) out[r] = slots.seen[r] ? slots.offset[r] : kNoSlot;
  };
  int gpr_stores = stored_gprs.count, gpr_loads = loaded_gprs.count;
  if (gpr_stores >= kMinGprs && gpr_loads <= kMaxOppositeAccesses) {
    c.kind = JmpKind::kSetjmp;
    c.gprs = gpr_stores;
    c.fprs = stored_fprs.count;
    c.touches_lr = saw_mflr;
    c.return_value_hint = saw_li_zero;
    export_slots(stored_gprs, c.gpr_offset);
    export_slots(stored_fprs, c.fpr_offset);
  } else if (gpr_loads >= kMinGprs && gpr_stores <= kMaxOppositeAccesses) {
    c.kind = JmpKind::kLongjmp;
    c.gprs = gpr_loads;
    c.fprs = loaded_fprs.count;
    c.touches_lr = saw_restore_branch;
    c.return_value_hint = saw_mr_r3_r4;
    export_slots(loaded_gprs, c.gpr_offset);
    export_slots(loaded_fprs, c.fpr_offset);
  } else {
    return std::nullopt;
  }
  c.score = c.gprs * 2 + c.fprs + (c.touches_lr ? 4 : 0) + (c.return_value_hint ? 3 : 0);
  return c;
}

int MatchingOffsets(const JmpCandidate& s, const JmpCandidate& l) {
  int matches = 0;
  for (int r = 0; r < 32; ++r) {
    if (s.gpr_offset[r] != kNoSlot && s.gpr_offset[r] == l.gpr_offset[r]) matches++;
    if (s.fpr_offset[r] != kNoSlot && s.fpr_offset[r] == l.fpr_offset[r]) matches++;
  }
  return matches;
}

void RankAndPair(JmpScanResult& result) {
  auto by_score = [](const JmpCandidate& a, const JmpCandidate& b) {
    return a.score != b.score ? a.score > b.score : a.address < b.address;
  };
  std::sort(result.setjmp_candidates.begin(), result.setjmp_candidates.end(), by_score);
  std::sort(result.longjmp_candidates.begin(), result.longjmp_candidates.end(), by_score);

  result.best_pair.reset();
  for (const auto& s : result.setjmp_candidates) {
    for (const auto& l : result.longjmp_candidates) {
      int matches = MatchingOffsets(s, l);
      // A real pair agrees on the jmp_buf layout for nearly every register.
      if (matches < kMinGprs) {
        continue;
      }
      if (!result.best_pair || matches > result.best_pair->matching_offsets ||
          (matches == result.best_pair->matching_offsets &&
           s.score + l.score > result.best_pair->setjmp.score + result.best_pair->longjmp.score)) {
        result.best_pair = JmpPair{s, l, matches};
      }
    }
  }
}

}  // namespace

JmpScanResult ScanForJmpFunctions(uint32_t base_address, std::span<const uint8_t> code) {
  JmpScanResult result;
  const size_t word_count = code.size() / 4;
  for (size_t i = 0; i < word_count; ++i) {
    if (i != 0 && !EndsFunction(ReadBE32(code.data() + (i - 1) * 4))) {
      continue;
    }
    uint32_t insn = ReadBE32(code.data() + i * 4);
    if (insn == 0) {
      continue;  // Padding, not an entry.
    }
    auto candidate = Classify(base_address + uint32_t(i * 4), code.data() + i * 4, word_count - i);
    if (!candidate) {
      continue;
    }
    if (candidate->kind == JmpKind::kSetjmp) {
      result.setjmp_candidates.push_back(*candidate);
    } else {
      result.longjmp_candidates.push_back(*candidate);
    }
  }
  RankAndPair(result);
  return result;
}

JmpScanResult MergeScanResults(const std::vector<JmpScanResult>& results) {
  JmpScanResult merged;
  for (const auto& r : results) {
    merged.setjmp_candidates.insert(merged.setjmp_candidates.end(), r.setjmp_candidates.begin(),
                                    r.setjmp_candidates.end());
    merged.longjmp_candidates.insert(merged.longjmp_candidates.end(), r.longjmp_candidates.begin(),
                                     r.longjmp_candidates.end());
  }
  RankAndPair(merged);
  return merged;
}

}  // namespace hod
