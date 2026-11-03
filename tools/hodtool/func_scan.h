// hodtool - finds code reached only through pointers
//
// MSVC emits no .pdata entry for tiny leaf functions, this-adjustor thunks
// ('addi r3,r3,N; b target') or vcall thunks, and nothing calls them
// directly: their addresses only appear in vtables, other data, or lis/addi
// pairs. The recompiler doesn't find them, and when the game calls one
// through a pointer the runtime stops with "Call to invalid or unregistered
// function". This lists them so they can be declared to the recompiler.
// (C++ port of tools/analysis/find_missing_functions.py.)

#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace hodtool {

struct AddressRange {
  uint32_t start = 0;
  uint32_t end = 0;  // exclusive
};

struct ImageLayout {
  uint32_t base = 0;                 // guest address of image byte 0
  AddressRange code;                 // the executable section
  std::vector<AddressRange> data;    // .rdata, .data, .sdata, .tls
  std::optional<AddressRange> pdata;
  uint32_t entry = 0;                // module entry point, 0 if none
};

// Candidates: code addresses referenced from data or built with lis+addi/ori
// that look like a function entry (previous word blr/bctr/b or padding) and
// are neither a .pdata function nor inside one, nor part of a switch (an
// address built right before a bctr, or the entries of such a table), nor an
// import stub, nor in
// `known` (functions the
// recompiler registered and labels inside them, when its output is at hand),
// nor obviously data. The module entry point is always included unless known.
std::vector<uint32_t> FindPointerOnlyFunctions(std::span<const uint8_t> image,
                                               const ImageLayout& layout,
                                               const std::set<uint32_t>& known = {});

}  // namespace hodtool
