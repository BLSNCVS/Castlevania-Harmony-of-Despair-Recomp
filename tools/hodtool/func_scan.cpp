// hodtool - finds code reached only through pointers (see func_scan.h)

#include "func_scan.h"

#include <algorithm>
#include <map>
#include <utility>

namespace hodtool {

std::vector<uint32_t> FindPointerOnlyFunctions(std::span<const uint8_t> image,
                                               const ImageLayout& unclamped,
                                               const std::set<uint32_t>& known) {
  // The section table comes from the DLL: keep every range inside the image
  // (and away from the top of the address space, so the loops end).
  ImageLayout layout = unclamped;
  const uint64_t image_end = std::min<uint64_t>(uint64_t(layout.base) + image.size(), 0xFFFFFFF0u);
  auto clamp = [&](AddressRange& range) {
    range.start = uint32_t(std::clamp<uint64_t>(range.start, layout.base, image_end));
    range.end = uint32_t(std::clamp<uint64_t>(range.end, range.start, image_end));
  };
  clamp(layout.code);
  for (auto& range : layout.data) clamp(range);
  if (layout.pdata) clamp(*layout.pdata);

  auto readable = [&](uint32_t address) {
    return address >= layout.base && uint64_t(address) - layout.base + 4 <= image.size();
  };
  auto word = [&](uint32_t address) -> uint32_t {
    if (!readable(address)) return 0;
    const uint8_t* p = image.data() + (address - layout.base);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  };
  auto in_code = [&](uint32_t v) {
    return v >= layout.code.start && v < layout.code.end && v % 4 == 0;
  };

  // .pdata: (begin address, packed length) pairs.
  std::vector<std::pair<uint32_t, uint32_t>> functions;
  if (layout.pdata) {
    for (uint32_t at = layout.pdata->start; at + 8 <= layout.pdata->end; at += 8) {
      uint32_t begin = word(at);
      if (begin) functions.emplace_back(begin, begin + ((word(at + 4) >> 8) & 0x3FFFFF) * 4);
    }
    std::sort(functions.begin(), functions.end());
  }
  auto pdata_function = [&](uint32_t a) {  // a starts or lies inside one
    auto it = std::upper_bound(functions.begin(), functions.end(),
                               std::make_pair(a, UINT32_MAX));
    if (it == functions.begin()) return false;
    --it;
    return it->first == a || (it->first < a && a < it->second);
  };

  std::set<uint32_t> refs;
  for (const auto& range : layout.data) {
    for (uint32_t at = range.start; at + 4 <= range.end; at += 4) {
      uint32_t v = word(at);
      if (in_code(v)) refs.insert(v);
    }
  }
  // Addresses built with lis+addi/ori shortly before a bctr belong to a
  // switch: the base its case offsets are added to, or a table of case
  // addresses. Case labels sit inside the switch's function, so neither the
  // base nor a table's entries are functions.
  std::set<uint32_t> switch_parts;
  auto jumps_soon = [&](uint32_t at) {
    for (uint32_t k = 1; k <= 16; ++k) {
      uint32_t i = word(at + 4 * k);
      if (i == 0x4E800420) return true;                       // bctr
      if (i == 0x4E800020 || (i >> 26) == 18) return false;  // blr, b/bl
    }
    return false;
  };
  auto built_address = [&](uint32_t at, uint32_t v) {
    if (jumps_soon(at)) {
      switch_parts.insert(v);
      if (!in_code(v)) {
        for (uint32_t k = 0; k < 1024; ++k) {
          uint32_t entry = word(v + 4 * k);
          if (!in_code(entry)) break;
          switch_parts.insert(entry);
        }
      }
    }
    if (in_code(v)) refs.insert(v);
  };
  std::map<uint32_t, uint32_t> pending;  // register -> lis value
  for (uint32_t at = layout.code.start; at < layout.code.end; at += 4) {
    uint32_t i = word(at);
    uint32_t op = i >> 26;
    if (op == 15 && ((i >> 16) & 31) == 0) {  // lis rD,imm
      pending[(i >> 21) & 31] = (i & 0xFFFF) << 16;
    } else if (op == 14 && pending.count((i >> 16) & 31)) {  // addi rD,rA,simm
      uint32_t lo = i & 0xFFFF;
      built_address(at, pending[(i >> 16) & 31] + (lo & 0x8000 ? lo - 0x10000 : lo));
    } else if (op == 24 && pending.count((i >> 21) & 31)) {  // ori rA,rS,imm
      built_address(at, pending[(i >> 21) & 31] | (i & 0xFFFF));
    } else if (i == 0x4E800020 || op == 18) {  // blr, b
      pending.clear();
    }
  }

  auto looks_like_entry = [&](uint32_t a) {
    uint32_t prev = word(a - 4);
    return prev == 0x4E800020 || prev == 0x4E800420 || prev == 0 || (prev >> 26) == 18;
  };
  auto looks_like_data = [&](uint32_t a) {
    uint32_t w[4] = {word(a), word(a + 4), word(a + 8), word(a + 12)};
    bool repeated = w[0] == w[1] && w[1] == w[2] && w[2] == w[3];
    // A table of pointers into game images (0x82-0x8F).
    auto image_pointer = [](uint32_t x) { return (x >> 24) >= 0x82 && (x >> 24) <= 0x8F; };
    return repeated || (image_pointer(w[0]) && image_pointer(w[1]));
  };

  // XEX import stubs: two import records (0x01nnnnnn, 0x02nnnnnn) that the
  // loader turns into a jump. Imports are bound separately, not recompiled.
  auto import_stub = [&](uint32_t a) {
    uint32_t first = word(a), second = word(a + 4);
    return (first >> 24) == 0x01 && (second >> 24) == 0x02 &&
           (first & 0xFFFFFF) == (second & 0xFFFFFF);
  };

  std::set<uint32_t> found;
  for (uint32_t a : refs) {
    if (!known.count(a) && !switch_parts.count(a) && !import_stub(a) && looks_like_entry(a) &&
        !pdata_function(a) && !looks_like_data(a)) {
      found.insert(a);
    }
  }
  // The loader calls a DLL's entry point when the game loads it.
  if (in_code(layout.entry) && !known.count(layout.entry) && !pdata_function(layout.entry)) {
    found.insert(layout.entry);
  }
  return {found.begin(), found.end()};
}

}  // namespace hodtool
