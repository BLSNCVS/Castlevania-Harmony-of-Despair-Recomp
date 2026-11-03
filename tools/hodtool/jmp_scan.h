// harmony-recomp - setjmp/longjmp locator
//
// The rexglue codegen needs the guest addresses of the CRT's setjmp and
// longjmp so it can lower them to host equivalents. Neither function is
// exported or named in a retail XEX, so we find them by shape:
//
//   setjmp:  stores the non-volatile GPRs (r13/r14..r31) and FPRs (f14..f31)
//            into a buffer pointed at by an argument register, then returns 0.
//   longjmp: loads the same registers back from the buffer, restores LR and
//            returns the second argument (or 1).
//
// This file has no SDK dependencies so it can be unit tested on raw PPC code.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hod {

enum class JmpKind { kSetjmp, kLongjmp };

// Marks a register the function doesn't save/restore. Real offsets are 16-bit
// displacements, so this can never collide with one.
constexpr int kNoSlot = 0x7FFFFFFF;

struct JmpCandidate {
  JmpKind kind;
  uint32_t address = 0;     // Guest address of the function entry.
  uint32_t length = 0;      // Bytes scanned up to (and including) the return.
  uint32_t base_reg = 0;    // GPR used as the jmp_buf pointer.
  int gprs = 0;             // Distinct non-volatile GPRs saved/restored.
  int fprs = 0;             // Distinct non-volatile FPRs saved/restored.
  bool touches_lr = false;  // mflr (setjmp) or mtlr/mtctr (longjmp) seen.
  bool return_value_hint = false;  // li r3,0 (setjmp) / mr r3,r4 (longjmp).
  int score = 0;
  // Offset into the buffer for each GPR 0..31 / FPR 0..31, or kNoSlot.
  int gpr_offset[32];
  int fpr_offset[32];
};

struct JmpPair {
  JmpCandidate setjmp;
  JmpCandidate longjmp;
  int matching_offsets = 0;  // Registers stored and loaded at the same offset.
};

struct JmpScanResult {
  std::vector<JmpCandidate> setjmp_candidates;   // Best first.
  std::vector<JmpCandidate> longjmp_candidates;  // Best first.
  std::optional<JmpPair> best_pair;
};

// Scans one executable section. `code` holds big-endian PPC instructions and
// begins at guest address `base_address`. Can be called once per section;
// use MergeScanResults to combine them.
JmpScanResult ScanForJmpFunctions(uint32_t base_address, std::span<const uint8_t> code);

// Combines per-section results and re-picks the best pair.
JmpScanResult MergeScanResults(const std::vector<JmpScanResult>& results);

}  // namespace hod
