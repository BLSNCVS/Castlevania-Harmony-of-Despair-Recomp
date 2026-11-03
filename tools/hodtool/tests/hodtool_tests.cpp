// harmony-recomp - unit tests for the SDK-independent parts of hodtool.
//
// Usage: hodtool_tests <path to jmp_scan_fixture.bin>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "../func_scan.h"
#include "../jmp_scan.h"
#include "install/installer.h"
#include "install/sha256.h"

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
  std::printf("%s %s\n", condition ? "[pass]" : "[FAIL]", what.c_str());
  if (!condition) {
    g_failures++;
  }
}

std::string HashOf(std::string_view text) {
  hod::Sha256 sha;
  sha.Update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
  auto digest = sha.Finish();
  return hod::ToHex(digest);
}

void TestSha256() {
  Check(HashOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "sha256 of empty input");
  Check(HashOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "sha256 of \"abc\"");
  Check(HashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        "sha256 of the two-block NIST vector");

  // One million 'a's fed in uneven chunks exercises the buffering path.
  hod::Sha256 sha;
  std::string chunk(997, 'a');
  size_t remaining = 1000000;
  while (remaining) {
    size_t n = std::min(remaining, chunk.size());
    sha.Update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(chunk.data()), n));
    remaining -= n;
  }
  auto digest = sha.Finish();
  Check(hod::ToHex(digest) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "sha256 of one million 'a' in 997-byte chunks");
}

bool HasCandidateAt(const std::vector<hod::JmpCandidate>& list, uint32_t address) {
  for (const auto& c : list) {
    if (c.address == address) return true;
  }
  return false;
}

void TestJmpScan(const char* fixture_path) {
  std::ifstream in(fixture_path, std::ios::binary);
  Check(bool(in), std::string("open fixture ") + fixture_path);
  if (!in) return;
  std::vector<uint8_t> code((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  // Offsets come from the symbol table of jmp_scan_fixture.s.
  constexpr uint32_t kBase = 0x82000000;
  constexpr uint32_t kSaveGprLr = kBase + 0x00C;
  constexpr uint32_t kSetjmp = kBase + 0x05C;
  constexpr uint32_t kCtxSave = kBase + 0x104;
  constexpr uint32_t kLongjmp = kBase + 0x150;
  constexpr uint32_t kCtxRestore = kBase + 0x204;

  // Split the code into two "sections" to exercise MergeScanResults.
  size_t split = 0x104;
  auto first = hod::ScanForJmpFunctions(kBase, std::span<const uint8_t>(code.data(), split));
  auto second = hod::ScanForJmpFunctions(
      kBase + uint32_t(split), std::span<const uint8_t>(code.data() + split, code.size() - split));
  auto result = hod::MergeScanResults({first, second});

  Check(!HasCandidateAt(result.setjmp_candidates, kSaveGprLr),
        "r1-based __savegprlr helper is not a setjmp candidate");
  Check(HasCandidateAt(result.setjmp_candidates, kSetjmp), "setjmp is a candidate");
  Check(HasCandidateAt(result.setjmp_candidates, kCtxSave), "context-save decoy is a candidate");
  Check(HasCandidateAt(result.longjmp_candidates, kLongjmp), "longjmp is a candidate");
  Check(HasCandidateAt(result.longjmp_candidates, kCtxRestore),
        "context-restore decoy is a candidate");
  Check(!result.setjmp_candidates.empty() && result.setjmp_candidates[0].address == kSetjmp,
        "setjmp ranks first");
  Check(!result.longjmp_candidates.empty() && result.longjmp_candidates[0].address == kLongjmp,
        "longjmp ranks first");
  Check(result.best_pair.has_value(), "a setjmp/longjmp pair was found");
  if (result.best_pair) {
    Check(result.best_pair->setjmp.address == kSetjmp, "pair setjmp address");
    Check(result.best_pair->longjmp.address == kLongjmp, "pair longjmp address");
    Check(result.best_pair->setjmp.base_reg == 3, "setjmp jmp_buf register is r3");
    Check(result.best_pair->matching_offsets == 19 + 18,
          "pair agrees on r13-r31 and f14-f31 offsets");
  }
}


// A synthetic image: a .pdata function, a this-adjustor thunk reached from a
// data table, a function pointer built with lis+addi, a switch on a base
// address, a switch through a table of case addresses, and the entry point.
void TestFuncScan() {
  constexpr uint32_t kBase = 0x82000000;
  std::vector<uint8_t> image(0x5000, 0);
  auto put = [&](uint32_t address, uint32_t value) {
    uint8_t* p = image.data() + (address - kBase);
    p[0] = uint8_t(value >> 24), p[1] = uint8_t(value >> 16), p[2] = uint8_t(value >> 8),
    p[3] = uint8_t(value);
  };
  constexpr uint32_t kBlr = 0x4E800020, kBctr = 0x4E800420;
  // .pdata function, 4 instructions.
  put(0x82001000, 0x7C0802A6), put(0x82001004, 0x60000000), put(0x82001008, 0x60000000);
  put(0x8200100C, kBlr);
  put(0x82004000, 0x82001000), put(0x82004004, 4 << 8);
  // Thunk: addi r3,r3,8; b. Only a data table points at it (with the .pdata function).
  put(0x82001010, 0x38630008), put(0x82001014, 0x4BFFFFEC);
  put(0x82003000, 0x82001010), put(0x82003004, 0x82001000);
  // Switch: lis/addi r12 = 0x82001040; mtctr; bctr; case at 0x82001040.
  put(0x82001020, 0x3D808200), put(0x82001024, 0x398C1040), put(0x82001028, 0x7D8903A6);
  put(0x8200102C, kBctr);
  put(0x82001040, 0x38600001), put(0x82001044, kBlr);
  // A function pointer: lis/addi r3 = 0x82001060; blr. The leaf: li r3,7; blr.
  put(0x82001050, 0x3C608200), put(0x82001054, 0x38631060), put(0x82001058, kBlr);
  put(0x82001060, 0x38600007), put(0x82001064, kBlr);
  // Cases 0x82001070 / 0x82001078, listed in a table at 0x82003010 that
  // lis/addi r11; lwzx r0,r11,r4; mtctr r0; bctr dispatches through.
  put(0x82001070, 0x38600002), put(0x82001074, kBlr);
  put(0x82001078, 0x38600003), put(0x8200107C, kBlr);
  put(0x82001080, 0x3D608200), put(0x82001084, 0x396B3010), put(0x82001088, 0x7C0B202E);
  put(0x8200108C, 0x7C0903A6), put(0x82001090, kBctr);
  put(0x82003010, 0x82001070), put(0x82003014, 0x82001078);
  // An import stub (two import records, mtctr r11, bctr) after a bctr,
  // listed in a data table: bound by the loader, not code to recompile.
  put(0x820010B0, 0x010000E7), put(0x820010B4, 0x020000E7), put(0x820010B8, 0x7D6903A6);
  put(0x820010BC, kBctr);
  put(0x82003020, 0x820010B0);
  // Entry point, referenced by nothing.
  put(0x820010A0, 0x38600000), put(0x820010A4, kBlr), put(0x820010AC, kBctr);

  hodtool::ImageLayout layout;
  layout.base = kBase;
  layout.code = {0x82001000, 0x82002000};
  layout.data = {{0x82003000, 0x82003100}};
  layout.pdata = hodtool::AddressRange{0x82004000, 0x82004008};
  layout.entry = 0x820010A0;
  auto found = hodtool::FindPointerOnlyFunctions(image, layout);
  std::vector<uint32_t> expected = {0x82001010, 0x82001060, 0x820010A0};
  Check(found == expected, "func scan: thunk, pointer-built leaf and entry; no switch cases or import stubs");
  auto known = hodtool::FindPointerOnlyFunctions(image, layout, {0x82001010, 0x820010A0});
  Check(known == std::vector<uint32_t>{0x82001060}, "func scan: known functions left out");
}

// Package entry names are untrusted: only plain relative names get through.
void TestSafeEntryPath() {
  using hod::install::SafeEntryPath;
  Check(SafeEntryPath("default.xex").has_value(), "safe name: plain file");
  Check(SafeEntryPath("data/dll/shot/dllshot.dll").has_value(), "safe name: nested path");
  Check(SafeEntryPath("data\\text\\textres_en.txrc").has_value(), "safe name: backslashes");
  const char* rejected[] = {
      "", "../evil.dll", "data/../../evil", "/etc/passwd", "C:/Windows/evil.dll", "data//x",
      "CON", "nul.txt", "data/COM1/x", "lpt9.bin", "trailing.", "trailing ", "a:b", "a<b",
      "a|b", "a?b", "a*b", "a\"b", "tab\there", "new\nline",
  };
  for (const char* name : rejected) {
    Check(!SafeEntryPath(name).has_value(), std::string("unsafe name rejected: ") + name);
  }
  Check(!SafeEntryPath(std::string_view("default.xex\0x", 13)).has_value(),
        "unsafe name rejected: embedded NUL");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <jmp_scan_fixture.bin>\n", argv[0]);
    return 2;
  }
  TestSha256();
  TestJmpScan(argv[1]);
  TestFuncScan();
  TestSafeEntryPath();
  std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures,
              g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}
