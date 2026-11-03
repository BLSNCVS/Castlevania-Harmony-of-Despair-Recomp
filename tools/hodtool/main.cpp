// harmony-recomp - hodtool
//
// Command-line companion to the in-app installer, for developers and
// headless setups. Works only with the user's own console packages.
//
//   hodtool info        <package>
//   hodtool extract     <package> <out_dir>
//   hodtool install     <source>... [--game-dir <dir>] [--user-data <dir>] [--no-tu] [--no-dlc]
//   hodtool install-dlc <package or folder>... [--user-data <dir>]
//   hodtool scan        <game_dir> [--write <toml>]
//   hodtool dump-image  <game_dir> <out.bin> [--module <path>] [--layout <json>]
//   hodtool find-functions <game_dir> --module <path> [--generated <dir>] [--out <toml>]
//   hodtool stage-modules --game <dir> --user-data <dir> --out <dir> --manifest <toml>
//     (the manifest is built from config/manifest_base.toml when that exists;
//     DLLs that share an address range go to extra codegen passes under passes/)
//
// <package> is an STFS container (LIVE/PIRS/CON) copied off the console, e.g.
// Content/0000000000000000/58410A7A/000D0000/<name>. A <source> is a package,
// an extracted game folder, or any folder to search for packages.

#include <algorithm>
#include <charconv>
#include <csignal>
#include <exception>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <toml++/toml.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <rex/system/xtypes.h>

#include "install/installer.h"
#include "install/sha256.h"
#include "func_scan.h"
#include "jmp_scan.h"

namespace fs = std::filesystem;
namespace install = hod::install;
using rex::X_STATUS;

namespace {

// Must match the name ReXApp derives the default user data folder from
// (the first argument to the app constructor in src/hodrecomp_app.h).
constexpr const char* kAppName = "hodrecomp";

int Usage() {
  std::fputs(
      "hodtool - prepare your own Castlevania: Harmony of Despair files\n"
      "\n"
      "  hodtool info        <package>\n"
      "      Show what a package is (game, title update, DLC) and its header fields.\n"
      "  hodtool extract     <package> <out_dir>\n"
      "      Extract every file in a package and fingerprint default.xex.\n"
      "  hodtool install     <source>... [--game-dir <dir>] [--user-data <dir>]\n"
      "                      [--no-tu] [--no-dlc]\n"
      "      Install the game, newest title update and all DLC found in the sources\n"
      "      (packages, an extracted game folder, or folders to search).\n"
      "  hodtool install-dlc <package or folder>... [--user-data <dir>]\n"
      "      Install DLC packages where the runtime's content manager looks for them.\n"
      "  hodtool scan        <game_dir> [--write <toml>]\n"
      "      Load <game_dir>/default.xex and locate setjmp/longjmp for the codegen.\n"
      "  hodtool dump-image  <game_dir> <out.bin> [--module <path>] [--layout <json>]\n"
      "      Write the decrypted, decompressed (and title-update patched) image of\n"
      "      default.xex, or of the DLL at <path> under <game_dir>, for disassemblers\n"
      "      and tools/analysis. Load it at the base address printed.\n"
      "  hodtool find-functions <game_dir> --module <path> [--generated <dir>] [--out <toml>]\n"
      "      List code in the DLL at <path> that is reached only through pointers\n"
      "      (functions the recompiler can't find). With --generated, functions the\n"
      "      recompiler already registered there are left out.\n"
      "  hodtool stage-modules --game <dir> --user-data <dir> --out <dir> --manifest <toml>\n"
      "      Collect default.xex and every DLL (base game and installed DLC) into\n"
      "      <out> for codegen, and write the manifest's [[modules]] list. DLLs\n"
      "      without a reviewed config/modules/<name>.toml get one generated in\n"
      "      generated/autoconfig/ by the same scan as find-functions.\n",
      stderr);
  return 2;
}

// Same rule as the game: portable.txt beside the executable keeps user data
// in user/ next to it.
fs::path DefaultUserData() {
  auto exe_dir = rex::filesystem::GetExecutableFolder();
  std::error_code ec;
  if (fs::exists(exe_dir / "portable.txt", ec)) return exe_dir / "user";
  return rex::filesystem::GetUserFolder() / kAppName;
}

std::string VersionString(uint32_t value) {
  return fmt::format("{}.{}.{}.{}", value >> 28, (value >> 24) & 0xF, (value >> 8) & 0xFFFF,
                     value & 0xFF);
}

void PrintPackage(const install::PackageInfo& info) {
  fmt::print("  kind         : {}\n", install::PackageKindName(info.kind));
  fmt::print("  content type : {:08X}\n", info.content_type);
  fmt::print("  title ID     : {:08X}{}\n", info.title_id,
             info.title_id == install::kTitleId ? " (Harmony of Despair)" : "");
  fmt::print("  media ID     : {:08X}\n", info.media_id);
  fmt::print("  version      : {}\n", VersionString(info.version));
  fmt::print("  title name   : {}\n", info.title_name);
  fmt::print("  display name : {}\n", info.display_name);
}

std::optional<install::PackageInfo> ProbeOrComplain(const fs::path& path) {
  auto info = install::ProbePackage(path);
  if (!info) {
    fmt::print(stderr,
               "error: {} is not a readable Xbox 360 package (expected a CON/LIVE/PIRS file "
               "copied from Content/0000000000000000/<TitleID>/...)\n",
               rex::path_to_utf8(path));
  }
  return info;
}

int CmdInfo(const std::vector<std::string>& args) {
  if (args.size() != 1) return Usage();
  auto info = ProbeOrComplain(rex::to_path(args[0]));
  if (!info) return 1;
  fmt::print("{}\n", args[0]);
  PrintPackage(*info);

  rex::filesystem::StfsContainerDevice device("", info->path);
  if (device.Initialize()) {
    size_t files = 0, bytes = 0;
    std::vector<rex::filesystem::Entry*> stack{device.ResolvePath("")};
    while (!stack.empty()) {
      auto* entry = stack.back();
      stack.pop_back();
      for (auto& child : entry->children()) stack.push_back(child.get());
      if (!(entry->attributes() & rex::filesystem::kFileAttributeDirectory)) {
        files++;
        bytes += entry->size();
      }
    }
    fmt::print("  contents     : {} files, {:.1f} MiB\n", files, double(bytes) / (1024.0 * 1024.0));
  }
  return 0;
}

int CmdExtract(const std::vector<std::string>& args) {
  if (args.size() != 2) return Usage();
  auto info = ProbeOrComplain(rex::to_path(args[0]));
  if (!info) return 1;
  fs::path out_dir = rex::to_path(args[1]);
  fmt::print("Package {}\n", args[0]);
  PrintPackage(*info);

  std::string error;
  if (!install::ExtractPackage(info->path, out_dir, nullptr, &error)) {
    fmt::print(stderr, "error: {}\n", error);
    return 1;
  }
  size_t files = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(out_dir, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec)) files++;
  }
  fmt::print("Extracted {} files to {}\n", files, rex::path_to_utf8(out_dir));

  if (auto digest = hod::Sha256File(out_dir / "default.xex")) {
    fmt::print("default.xex sha256: {}\n", *digest);
  }
  if (auto digest = hod::Sha256File(out_dir / "default.xexp")) {
    fmt::print("default.xexp sha256: {}\n", *digest);
  }
  return 0;
}

void PrintSources(const install::Sources& sources) {
  if (sources.game) {
    fmt::print("Game:          {} ({})\n", sources.game->display_name,
               rex::path_to_utf8(sources.game->path.filename()));
  } else if (sources.game_dir) {
    fmt::print("Game:          extracted folder {}\n", rex::path_to_utf8(*sources.game_dir));
  } else {
    fmt::print("Game:          not found\n");
  }
  for (const auto& tu : sources.title_updates) {
    fmt::print("Title update:  {} ({})\n", tu.display_name, rex::path_to_utf8(tu.path.filename()));
  }
  for (const auto& dlc : sources.dlc) {
    fmt::print("DLC:           {}\n", dlc.display_name);
  }
  for (const auto& other : sources.ignored) {
    fmt::print("Ignored:       {} (title {:08X}, type {:08X})\n", rex::path_to_utf8(other.path.filename()),
               other.title_id, other.content_type);
  }
  for (const auto& warning : sources.warnings) {
    fmt::print("Warning:       {}\n", warning);
  }
}

int CmdInstall(const std::vector<std::string>& args) {
  install::Request request;
  request.game_dir = "game";
  request.user_data_dir = DefaultUserData();
  std::vector<fs::path> inputs;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--game-dir" && i + 1 < args.size()) {
      request.game_dir = rex::to_path(args[++i]);
    } else if (args[i] == "--user-data" && i + 1 < args.size()) {
      request.user_data_dir = rex::to_path(args[++i]);
    } else if (args[i] == "--no-tu") {
      request.install_title_update = false;
    } else if (args[i] == "--no-dlc") {
      request.install_dlc = false;
    } else if (args[i].starts_with("--")) {
      return Usage();
    } else {
      inputs.emplace_back(rex::to_path(args[i]));
    }
  }
  if (inputs.empty()) return Usage();
  for (const auto& input : inputs) {
    fmt::print("Scanning {}\n", rex::path_to_utf8(input));
    install::AddSource(request.sources, input);
  }
  PrintSources(request.sources);

  install::Progress progress;
  progress.EchoSteps(true);
  auto result = install::Install(request, &progress);
  if (!result.ok) {
    fmt::print(stderr, "error: {}\n", result.error);
    return 1;
  }
  fmt::print("\nInstalled to  {}\n", rex::path_to_utf8(request.game_dir));
  fmt::print("default.xex   {}{}\n", result.xex_sha256,
             result.xex_known ? " (supported)" : " (not in the supported list)");
  if (result.title_update) {
    fmt::print("Title update  {} (UPDATE: files in {})\n", *result.title_update,
               rex::path_to_utf8(result.update_dir));
  }
  if (!result.dlc.empty()) {
    fmt::print("DLC           {} pack(s) under {}\n", result.dlc.size(),
               rex::path_to_utf8(request.user_data_dir));
  }
  return 0;
}

int CmdInstallDlc(const std::vector<std::string>& args) {
  fs::path user_data = DefaultUserData();
  std::vector<fs::path> packages;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--user-data" && i + 1 < args.size()) {
      user_data = rex::to_path(args[++i]);
    } else {
      packages.emplace_back(rex::to_path(args[i]));
    }
  }
  if (packages.empty()) return Usage();

  // Folders are searched for DLC packages; anything else in them is skipped.
  std::vector<install::PackageInfo> infos;
  int failures = 0;
  for (const auto& package : packages) {
    std::error_code ec;
    if (fs::is_directory(package, ec)) {
      install::Sources found;
      install::AddSource(found, package);
      if (found.dlc.empty()) fmt::print("No DLC packages in {}\n", rex::path_to_utf8(package));
      infos.insert(infos.end(), found.dlc.begin(), found.dlc.end());
      continue;
    }
    auto info = ProbeOrComplain(package);
    if (!info) {
      failures++;
      continue;
    }
    infos.push_back(std::move(*info));
  }
  for (const auto& info : infos) {
    fmt::print("Installing {}\n", info.display_name);
    std::string error;
    if (!install::InstallDlc(info, user_data, nullptr, &error)) {
      fmt::print(stderr, "error: {}\n", error);
      failures++;
    }
  }
  fmt::print("DLC installed under {}\n", rex::path_to_utf8(user_data));
  return failures ? 1 : 0;
}

// Brings up a headless runtime with <game_dir>/default.xex loaded, the same
// way the rexglue codegen does.
std::unique_ptr<rex::Runtime> LoadGame(const fs::path& game_dir) {
  if (!fs::exists(game_dir / "default.xex")) {
    fmt::print(stderr, "error: {} has no default.xex (run 'hodtool install' first)\n",
               rex::path_to_utf8(game_dir));
    return nullptr;
  }
  auto runtime = std::make_unique<rex::Runtime>(fs::canonical(game_dir));
  auto status = runtime->Setup(rex::RuntimeConfig{
      .kernel_init = rex::kernel::InitializeKernel,
      .tool_mode = true,
  });
  if (status != X_STATUS_SUCCESS) {
    fmt::print(stderr, "error: runtime setup failed ({:#x})\n", status);
    return nullptr;
  }
  status = runtime->LoadXexImage("game:\\default.xex");
  if (status != X_STATUS_SUCCESS) {
    fmt::print(stderr, "error: could not load default.xex ({:#x})\n", status);
    return nullptr;
  }
  return runtime;
}

std::string Hex32(uint32_t value) { return fmt::format("0x{:08X}", value); }

int CmdScan(const std::vector<std::string>& args) {
  if (args.empty()) return Usage();
  fs::path game_dir = rex::to_path(args[0]);
  std::optional<fs::path> write_path;
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--write" && i + 1 < args.size()) {
      write_path = rex::to_path(args[++i]);
    } else {
      return Usage();
    }
  }

  auto runtime = LoadGame(game_dir);
  if (!runtime) return 1;
  auto module = runtime->kernel_state()->GetExecutableModule();
  auto* xex = module->xex_module();
  fmt::print("default.xex: title {:08X}, base {}, entry {}\n", module->title_id(),
             Hex32(xex->base_address()), Hex32(xex->entry_point()));

  std::vector<hod::JmpScanResult> per_section;
  for (const auto& section : xex->binary_sections()) {
    if (!section.executable || !section.host_data) continue;
    fmt::print("  scanning {} [{} - {}]\n", section.name, Hex32(section.virtual_address),
               Hex32(section.virtual_address + section.virtual_size));
    per_section.push_back(hod::ScanForJmpFunctions(
        section.virtual_address, std::span<const uint8_t>(section.host_data, section.virtual_size)));
  }
  auto result = hod::MergeScanResults(per_section);

  auto print_list = [](const char* label, const std::vector<hod::JmpCandidate>& list) {
    fmt::print("{} candidates: {}\n", label, list.size());
    for (size_t i = 0; i < list.size() && i < 5; ++i) {
      const auto& c = list[i];
      fmt::print("  {} score {:3}  r{} buffer, {} GPRs, {} FPRs, {}{}\n", Hex32(c.address),
                 c.score, c.base_reg, c.gprs, c.fprs, c.touches_lr ? "LR " : "",
                 c.return_value_hint ? "return-value pattern" : "");
    }
  };
  print_list("setjmp", result.setjmp_candidates);
  print_list("longjmp", result.longjmp_candidates);

  if (!result.best_pair) {
    fmt::print(stderr,
               "No consistent setjmp/longjmp pair found. Inspect the candidates above in a\n"
               "disassembler and set setjmp_address/longjmp_address by hand.\n");
    return 1;
  }
  const auto& pair = *result.best_pair;
  std::string toml = fmt::format(
      "# Generated by 'hodtool scan' for default.xex (title {:08X}).\n"
      "# Both functions agree on {} jmp_buf register slots. Verify in a\n"
      "# disassembler before trusting a new dump.\n"
      "setjmp_address = {}\n"
      "longjmp_address = {}\n",
      module->title_id(), pair.matching_offsets, Hex32(pair.setjmp.address),
      Hex32(pair.longjmp.address));
  fmt::print("\n{}", toml);
  if (write_path) {
    std::ofstream out(*write_path, std::ios::trunc);
    out << toml;
    if (!out) {
      fmt::print(stderr, "error: cannot write {}\n", rex::path_to_utf8(*write_path));
      return 1;
    }
    fmt::print("wrote {}\n", rex::path_to_utf8(*write_path));
  }
  return 0;
}

// The few XEX2 header fields module staging needs: where the image loads.
struct XexLayout {
  uint32_t base = 0;
  uint32_t size = 0;
};

std::optional<XexLayout> ReadXexLayout(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> header(0x4000);
  in.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
  header.resize(size_t(in.gcount()));
  auto be32 = [&](size_t off) -> std::optional<uint32_t> {
    if (off + 4 > header.size()) return std::nullopt;
    return (uint32_t(header[off]) << 24) | (uint32_t(header[off + 1]) << 16) |
           (uint32_t(header[off + 2]) << 8) | uint32_t(header[off + 3]);
  };
  if (be32(0) != 0x58455832) return std::nullopt;  // 'XEX2'
  auto security = be32(0x10);
  auto count = be32(0x14);
  if (!security || !count) return std::nullopt;
  XexLayout layout;
  auto size = be32(*security + 0x4);
  auto load = be32(*security + 0x110);
  if (!size || !load) return std::nullopt;
  layout.size = *size;
  layout.base = *load;
  // The optional-header count comes from the file: no more than fit in it.
  const uint32_t max_count = header.size() >= 0x18 ? uint32_t((header.size() - 0x18) / 8) : 0;
  for (uint32_t i = 0; i < std::min(*count, max_count); ++i) {
    auto key = be32(0x18 + i * 8);
    auto value = be32(0x18 + i * 8 + 4);
    if (key == 0x00010201u && value) layout.base = *value;  // XEX_HEADER_IMAGE_BASE_ADDRESS
  }
  return layout;
}

bool IEndsWith(const std::string& s, std::string_view suffix) {
  if (s.size() < suffix.size()) return false;
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

std::string Lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Leaves the file (and its timestamp) alone when the content is the same, so
// re-staging doesn't make CMake reconfigure or codegen re-run for nothing.
// Leaves the file alone when it already holds `text` (keeps build stamps).
// Throws on failure: a half-written manifest must not go unnoticed.
void WriteIfChanged(const fs::path& path, const std::string& text) {
  std::ifstream in(path, std::ios::binary);
  if (in) {
    std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (old == text) return;
  }
  in.close();
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  out.close();
  if (!out) throw std::runtime_error("cannot write " + rex::path_to_utf8(path));
}

// A TOML basic string: names from packages can hold any character.
std::string TomlString(std::string_view text) {
  std::string out = "\"";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += char(c);
    } else if (c < 0x20 || c == 0x7F) {
      out += fmt::format("\\u{:04X}", c);
    } else {
      out += char(c);
    }
  }
  return out + "\"";
}

// A path as UTF-8 with forward slashes, for the manifest.
std::string GenericUtf8(const fs::path& path) {
  std::string text = rex::path_to_utf8(path.lexically_normal());
  std::replace(text.begin(), text.end(), '\\', '/');
  return text;
}

// Text for a TOML comment: one line, printable.
std::string CommentText(std::string_view text) {
  std::string out;
  for (unsigned char c : text) out += (c < 0x20 || c == 0x7F) ? '?' : char(c);
  return out;
}

struct ManifestBasics {
  std::string name;
  std::string sdk_version;
  std::string game_root;
  std::string entry_file;
  std::vector<std::string> includes;
};

// The [project] and [entrypoint] settings of a codegen manifest.
std::optional<ManifestBasics> ReadManifestBasics(const fs::path& manifest) {
  toml::table table;
#if TOML_EXCEPTIONS
  try {
    table = toml::parse_file(rex::path_to_utf8(manifest));
  } catch (const toml::parse_error& error) {
    fmt::print(stderr, "error: {}: {}\n", rex::path_to_utf8(manifest), error.description());
    return std::nullopt;
  }
#else
  auto parsed = toml::parse_file(rex::path_to_utf8(manifest));
  if (!parsed) {
    fmt::print(stderr, "error: {}: {}\n", rex::path_to_utf8(manifest), parsed.error().description());
    return std::nullopt;
  }
  table = std::move(parsed).table();
#endif
  ManifestBasics basics;
  basics.name = table["project"]["name"].value_or(std::string(kAppName));
  basics.sdk_version = table["project"]["sdk_version"].value_or(std::string());
  basics.game_root = table["project"]["game_root"].value_or(std::string());
  basics.entry_file = table["entrypoint"]["file_path"].value_or(std::string());
  if (auto* includes = table["entrypoint"]["includes"].as_array()) {
    for (const auto& include : *includes) {
      if (auto value = include.value<std::string>()) basics.includes.push_back(*value);
    }
  }
  if (basics.game_root.empty() || basics.entry_file.empty()) {
    fmt::print(stderr, "error: {} has no [project] game_root or [entrypoint] file_path\n",
               rex::path_to_utf8(manifest));
    return std::nullopt;
  }
  return basics;
}

// The functions a DLL needs declared (see func_scan.h). Loads the module into
// `runtime` the way codegen does and unloads it again, so DLLs sharing an
// address range can be scanned one after another.
std::optional<std::vector<uint32_t>> ScanModuleFunctions(rex::Runtime& runtime,
                                                         const std::string& module_path,
                                                         const std::set<uint32_t>& known) {
  std::string vfs = module_path;
  std::replace(vfs.begin(), vfs.end(), '/', '\\');
  auto* kernel = runtime.kernel_state();
  auto module = kernel->LoadUserModule("game:\\" + vfs, false);
  if (!module) {
    fmt::print(stderr, "warning: could not load {} to scan it\n", module_path);
    return std::nullopt;
  }
  auto* xex = module->xex_module();
  hodtool::ImageLayout layout;
  layout.base = xex->base_address();
  layout.entry = xex->entry_point();
  int code_sections = 0;
  for (const auto& section : xex->binary_sections()) {
    hodtool::AddressRange range{section.virtual_address,
                                section.virtual_address + section.virtual_size};
    if (section.executable) {
      layout.code = range;
      ++code_sections;
    } else if (section.name == ".rdata" || section.name == ".data" || section.name == ".sdata" ||
               section.name == ".tls") {
      layout.data.push_back(range);
    } else if (section.name == ".pdata") {
      layout.pdata = range;
    }
  }
  std::optional<std::vector<uint32_t>> found;
  if (code_sections == 1) {
    const uint8_t* image = runtime.memory()->TranslateVirtual(layout.base);
    found = hodtool::FindPointerOnlyFunctions({image, xex->image_size()}, layout, known);
  } else {
    fmt::print(stderr, "warning: {} has {} code sections; not scanned\n", module_path,
               code_sections);
  }
  kernel->UnloadUserModule(module, false);
  return found;
}

// Functions and labels the recompiler wrote to `generated` (*_init.cpp
// registers "{ 0x82000000, ...", *_recomp.*.cpp has "loc_82000000:" labels).
std::set<uint32_t> RegisteredInGenerated(const fs::path& generated) {
  std::set<uint32_t> known;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(generated, ec)) {
    std::string name = rex::path_to_utf8(entry.path().filename());
    bool init = name.ends_with("_init.cpp");
    bool recomp = name.find("_recomp.") != std::string::npos && name.ends_with(".cpp");
    if (!init && !recomp) continue;
    std::ifstream in(entry.path());
    std::string line;
    while (std::getline(in, line)) {
      std::string_view view(line);
      size_t at = std::string_view::npos;
      if (init) {
        auto brace = view.find('{');
        if (brace != std::string_view::npos) {
          auto hex = view.find("0x", brace);
          if (hex != std::string_view::npos && view.substr(brace + 1, hex - brace - 1)
                                                       .find_first_not_of(" \t") == std::string_view::npos) {
            at = hex + 2;
          }
        }
      } else if (view.starts_with("loc_")) {
        at = 4;
      }
      if (at == std::string_view::npos || at + 8 > view.size()) continue;
      uint32_t value = 0;
      auto [end, err] = std::from_chars(view.data() + at, view.data() + at + 8, value, 16);
      if (err == std::errc() && end == view.data() + at + 8) known.insert(value);
    }
  }
  return known;
}

std::string FunctionsToml(const std::vector<uint32_t>& functions, std::string_view origin) {
  std::string text = fmt::format(
      "# {}: code reached only through pointers (no .pdata entry, no direct\n"
      "# calls), declared so the recompiler includes it.\n\n[functions]\n",
      origin);
  for (uint32_t address : functions) text += fmt::format("0x{:08X} = {{}}\n", address);
  return text;
}

int CmdFindFunctions(const std::vector<std::string>& args) {
  if (args.empty()) return Usage();
  std::string module_path;
  std::optional<fs::path> generated, out;
  for (size_t i = 1; i + 1 < args.size(); i += 2) {
    if (args[i] == "--module") module_path = args[i + 1];
    else if (args[i] == "--generated") generated = rex::to_path(args[i + 1]);
    else if (args[i] == "--out") out = rex::to_path(args[i + 1]);
    else return Usage();
  }
  if (module_path.empty()) return Usage();
  auto runtime = LoadGame(rex::to_path(args[0]));
  if (!runtime) return 1;
  auto known = generated ? RegisteredInGenerated(*generated) : std::set<uint32_t>{};
  auto found = ScanModuleFunctions(*runtime, module_path, known);
  if (!found) return 1;
  for (uint32_t address : *found) fmt::print("{}\n", Hex32(address));
  if (out) WriteIfChanged(*out, FunctionsToml(*found, "Written by 'hodtool find-functions'"));
  fmt::print("{} function(s) reached only through pointers\n", found->size());
  return 0;
}

int CmdStageModules(const std::vector<std::string>& args) {
  fs::path game_dir, user_data = DefaultUserData(), out_dir, manifest;
  for (size_t i = 0; i + 1 < args.size(); i += 2) {
    if (args[i] == "--game") game_dir = rex::to_path(args[i + 1]);
    else if (args[i] == "--user-data") user_data = rex::to_path(args[i + 1]);
    else if (args[i] == "--out") out_dir = rex::to_path(args[i + 1]);
    else if (args[i] == "--manifest") manifest = rex::to_path(args[i + 1]);
    else return Usage();
  }
  if (game_dir.empty() || out_dir.empty() || manifest.empty()) return Usage();
  if (!fs::exists(game_dir / "default.xex")) {
    fmt::print(stderr, "error: {} has no default.xex\n", rex::path_to_utf8(game_dir));
    return 1;
  }

  struct Module {
    fs::path source;
    std::string guest_path;  // Path the game loads it by, without device prefix.
    fs::path staged;         // Relative to out_dir.
    XexLayout layout;
  };
  std::vector<Module> modules;
  auto collect = [&](const fs::path& root, const fs::path& stage_prefix) {
    std::vector<fs::path> found;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file(ec) && IEndsWith(rex::path_to_utf8(it->path().filename()), ".dll")) {
        found.push_back(it->path());
      }
    }
    std::sort(found.begin(), found.end());
    for (const auto& path : found) {
      auto layout = ReadXexLayout(path);
      if (!layout) {
        fmt::print("warning: {} is not an XEX module, skipped\n", rex::path_to_utf8(path));
        continue;
      }
      auto relative = fs::relative(path, root);
      modules.push_back({path, relative.generic_string(), stage_prefix / relative, *layout});
    }
  };
  collect(game_dir, {});
  auto dlc_root = user_data / "0000000000000000" / fmt::format("{:08X}", install::kTitleId) / "00000002";
  std::error_code ec;
  if (fs::is_directory(dlc_root, ec)) {
    std::vector<fs::path> packages;
    for (const auto& entry : fs::directory_iterator(dlc_root, ec)) {
      if (entry.is_directory()) packages.push_back(entry.path());
    }
    std::sort(packages.begin(), packages.end());
    for (const auto& package : packages) {
      collect(package, fs::path("dlc") / package.filename());
    }
  }

  // Modules that share an address range can't be loaded into one codegen
  // runtime together (the bosses take turns in one slot). Spread them over
  // codegen passes: pass 0 is the main manifest, every further pass gets a
  // manifest of its own under passes/pass<N>/, which CMakeLists.txt picks up.
  std::vector<std::vector<Module>> passes(1);
  for (auto& module : modules) {
    size_t pass = 0;
    for (; pass < passes.size(); ++pass) {
      const Module* clash = nullptr;
      for (const auto& other : passes[pass]) {
        if (module.layout.base < other.layout.base + other.layout.size &&
            other.layout.base < module.layout.base + module.layout.size) {
          clash = &other;
          break;
        }
      }
      if (!clash) break;
      if (pass == 0) {
        fmt::print("note: {} shares {}'s address range at {}; recompiled in a separate pass\n",
                   module.guest_path, clash->guest_path, Hex32(module.layout.base));
      }
    }
    if (pass == passes.size()) passes.emplace_back();
    passes[pass].push_back(std::move(module));
  }

  // Stage files: the executable with its patch, every DLL with its patch.
  bool copy_failed = false;
  auto copy = [&](const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::update_existing, ec);
    if (ec) {
      fmt::print(stderr, "error: cannot copy {}: {}\n", rex::path_to_utf8(from), ec.message());
      copy_failed = true;
    }
  };
  copy(game_dir / "default.xex", out_dir / "default.xex");
  if (fs::exists(game_dir / "default.xexp")) copy(game_dir / "default.xexp", out_dir / "default.xexp");
  for (const auto& pass : passes) {
    for (const auto& module : pass) {
      copy(module.source, out_dir / module.staged);
      auto patch = module.source;
      patch += "p";
      if (fs::exists(patch)) {
        auto staged_patch = out_dir / module.staged;
        staged_patch += "p";
        copy(patch, staged_patch);
      }
    }
  }

  // Generated function lists, from one runtime loaded on first use.
  std::unique_ptr<rex::Runtime> scan_runtime;
  bool scan_failed = false;
  auto AutoConfig = [&](const Module& module, const fs::path& path) {
    if (!scan_runtime && !scan_failed) {
      scan_runtime = LoadGame(out_dir);
      scan_failed = !scan_runtime;
    }
    std::optional<std::vector<uint32_t>> found;
    if (scan_runtime) found = ScanModuleFunctions(*scan_runtime, module.staged.generic_string(), {});
    if (!found || found->empty()) {
      fs::remove(path, ec);
      return false;
    }
    fs::create_directories(path.parent_path(), ec);
    WriteIfChanged(path, FunctionsToml(*found, fmt::format("Generated by 'hodtool stage-modules' for {}",
                                                           module.guest_path)));
    fmt::print("{}: {} function(s) reached only through pointers\n", module.guest_path,
               found->size());
    return true;
  };

  // [[modules]] entries for a manifest in `manifest_dir`. Paths in a manifest
  // are relative to it; output folder names are unique across all passes.
  const fs::path root = fs::absolute(manifest).parent_path();
  const fs::path abs_out = fs::absolute(out_dir);
  std::set<std::string> used_names;
  auto modules_block = [&](const std::vector<Module>& list, const fs::path& manifest_dir) {
    fs::path to_root = fs::relative(root, manifest_dir);
    fs::path out_rel = fs::relative(abs_out, manifest_dir);
    std::string block;
    for (const auto& module : list) {
      std::string name = Lower(rex::path_to_utf8(module.staged.stem()));
      while (!used_names.insert(name).second) name += "_";
      auto config = fs::path("config") / "modules" / (name + ".toml");
      bool has_config = fs::exists(root / config);
      if (!has_config) {
        // No reviewed list for this DLL (e.g. DLC this project wasn't
        // checked against): scan it now.
        config = fs::path("generated") / "autoconfig" / (name + ".toml");
        has_config = AutoConfig(module, root / config);
      }
      block += fmt::format(
          "\n[[modules]]\n# {} at {}\nguest_path = {}\nfile_path = {}\n"
          "out_directory_path = {}\nincludes = [{}]\n",
          CommentText(rex::path_to_utf8(module.source.filename())), Hex32(module.layout.base),
          TomlString(module.guest_path),
          TomlString(GenericUtf8(out_rel / module.staged)),
          TomlString(GenericUtf8(to_root / "generated" / name)),
          has_config
              ? TomlString(GenericUtf8(to_root / config))
              : "");
    }
    return block;
  };

  // Rewrite the [[modules]] block of the main manifest. With a tracked
  // template (config/manifest_base.toml beside it) the manifest is built from
  // that, so the git-ignored manifest never carries local edits.
  auto base = root / "config" / "manifest_base.toml";
  std::ifstream manifest_in(fs::exists(base, ec) ? base : fs::path(manifest));
  std::string text((std::istreambuf_iterator<char>(manifest_in)), std::istreambuf_iterator<char>());
  manifest_in.close();
  const std::string begin_marker = "# BEGIN MODULES";
  const std::string end_marker = "# END MODULES";
  std::string block = begin_marker + " (written by 'hodtool stage-modules'; do not edit by hand)\n";
  block += modules_block(passes[0], root);
  block += "\n" + end_marker + "\n";
  auto begin = text.find(begin_marker);
  auto end = text.find(end_marker);
  if (begin != std::string::npos && end != std::string::npos && end > begin) {
    auto end_line = text.find('\n', end);
    text.replace(begin, (end_line == std::string::npos ? text.size() : end_line + 1) - begin, block);
  } else {
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += "\n" + block;
  }
  WriteIfChanged(manifest, text);

  // Extra passes: the main manifest's project and entrypoint settings with
  // paths rebased, plus that pass's modules. The entrypoint is recompiled
  // again as a by-product (codegen needs it loaded); only the modules' output
  // is built.
  if (passes.size() > 1) {
    auto project = ReadManifestBasics(manifest);
    if (!project) return 1;
    for (size_t pass = 1; pass < passes.size(); ++pass) {
      fs::path pass_dir = root / "passes" / fmt::format("pass{}", pass);
      fs::create_directories(pass_dir, ec);
      auto rebase = [&](const std::string& path) {
        return (fs::relative(root, pass_dir) / path).lexically_normal().generic_string();
      };
      std::string includes;
      for (const auto& include : project->includes) {
        includes += fmt::format("    \"{}\",\n", rebase(include));
      }
      std::string pass_text = fmt::format(
          "# Codegen pass {} for {}, written by 'hodtool stage-modules'; do not edit.\n"
          "# These modules share an address range with modules in the main manifest,\n"
          "# so they are recompiled in a run of their own.\n\n"
          "[project]\nname = \"{}\"\nsdk_version = \"{}\"\ngame_root = \"{}\"\n\n"
          "[entrypoint]\nfile_path = \"{}\"\nout_directory_path = \"{}\"\nincludes = [\n{}]\n",
          pass, rex::path_to_utf8(manifest.filename()), project->name, project->sdk_version,
          rebase(project->game_root), rebase(project->entry_file),
          rebase(fmt::format("generated/pass{}", pass)), includes);
      pass_text += modules_block(passes[pass], pass_dir);
      WriteIfChanged(pass_dir / manifest.filename(), pass_text);
      WriteIfChanged(pass_dir / "CMakeLists.txt",
                     fmt::format("# Written by 'hodtool stage-modules'; do not edit.\n"
                                 "hodrecomp_add_codegen_pass({})\n",
                                 pass));
    }
  }
  // Passes left over from an earlier run with more overlapping DLC.
  for (size_t pass = passes.size();; ++pass) {
    fs::path stale = root / "passes" / fmt::format("pass{}", pass);
    if (!fs::exists(stale, ec)) break;
    fs::remove_all(stale, ec);
  }

  size_t total = 0;
  for (const auto& pass : passes) total += pass.size();
  fmt::print("Staged default.xex and {} DLL module(s) into {} ({} codegen pass(es))\n", total,
             rex::path_to_utf8(out_dir), passes.size());
  return copy_failed ? 1 : 0;
}

int CmdDumpImage(const std::vector<std::string>& args) {
  if (args.size() < 2) return Usage();
  std::optional<std::string> module_path;
  std::optional<fs::path> layout_path;
  for (size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--module" && i + 1 < args.size()) {
      module_path = args[++i];
    } else if (args[i] == "--layout" && i + 1 < args.size()) {
      layout_path = rex::to_path(args[++i]);
    } else {
      return Usage();
    }
  }
  auto runtime = LoadGame(rex::to_path(args[0]));
  if (!runtime) return 1;

  // A DLL is loaded the way codegen loads it: through game:\, with its
  // .dllp patch beside it applied.
  rex::system::object_ref<rex::system::UserModule> user_module;
  rex::runtime::XexModule* xex = nullptr;
  if (module_path) {
    std::string vfs = *module_path;
    std::replace(vfs.begin(), vfs.end(), '/', '\\');
    user_module = runtime->kernel_state()->LoadUserModule("game:\\" + vfs, false);
    if (!user_module) {
      fmt::print(stderr, "error: could not load {}\n", *module_path);
      return 1;
    }
    xex = user_module->xex_module();
  } else {
    xex = runtime->kernel_state()->GetExecutableModule()->xex_module();
  }

  const uint8_t* image = runtime->memory()->TranslateVirtual(xex->base_address());
  std::ofstream out(rex::to_path(args[1]), std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(image), std::streamsize(xex->image_size()));
  if (!out) {
    fmt::print(stderr, "error: cannot write {}\n", args[1]);
    return 1;
  }
  fmt::print("Wrote {} bytes; base {}\n", xex->image_size(), Hex32(xex->base_address()));
  std::string sections;
  for (const auto& section : xex->binary_sections()) {
    fmt::print("  {:8} {} - {} {}{}\n", section.name, Hex32(section.virtual_address),
               Hex32(section.virtual_address + section.virtual_size),
               section.executable ? "x" : "-", section.writable ? "w" : "-");
    sections += fmt::format(
        "{}    {{\"name\": \"{}\", \"start\": {}, \"end\": {}, \"executable\": {}, "
        "\"writable\": {}}}",
        sections.empty() ? "" : ",\n", section.name, section.virtual_address,
        section.virtual_address + section.virtual_size, section.executable ? "true" : "false",
        section.writable ? "true" : "false");
  }
  if (layout_path) {
    std::ofstream layout(*layout_path, std::ios::trunc);
    layout << fmt::format(
        "{{\n  \"base\": {},\n  \"size\": {},\n  \"entry\": {},\n  \"sections\": [\n{}\n  ]\n}}\n",
        xex->base_address(), xex->image_size(), xex->entry_point(), sections);
  }
  return 0;
}

}  // namespace

// hodtool runs under build scripts that only see its output, so a crash has
// to say what happened before the process goes away.
[[noreturn]] void OnTerminate() {
  if (auto exception = std::current_exception()) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "fatal: uncaught exception: %s\n", e.what());
    } catch (...) {
      std::fprintf(stderr, "fatal: uncaught exception of unknown type\n");
    }
  } else {
    std::fprintf(stderr, "fatal: std::terminate called\n");
  }
  std::fflush(stderr);
  std::abort();
}

#ifdef _WIN32
LONG WINAPI ReportCrash(EXCEPTION_POINTERS* info) {
  const auto* record = info->ExceptionRecord;
  HMODULE module = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(record->ExceptionAddress), &module)) {
    GetModuleFileNameA(module, name, MAX_PATH);
  }
  std::fprintf(stderr, "fatal: exception 0x%08lX at %s+0x%llX\n", record->ExceptionCode, name,
               static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(record->ExceptionAddress) -
                                               reinterpret_cast<uintptr_t>(module)));
  std::fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}

void ReportInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {
  std::fprintf(stderr, "fatal: invalid parameter passed to a C runtime function\n");
  std::fflush(stderr);
  std::abort();
}
#endif

void InstallCrashReporting() {
  // Unbuffered, so everything printed before a crash reaches the log.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::set_terminate(OnTerminate);
  std::signal(SIGABRT, [](int) {
    std::fprintf(stderr, "fatal: aborted\n");
    std::fflush(stderr);
  });
#ifdef _WIN32
  SetUnhandledExceptionFilter(ReportCrash);
  _set_invalid_parameter_handler(ReportInvalidParameter);
#endif
}

// Everything after the program name, as UTF-8. On Windows the entry point is
// wmain: argv in the ANSI code page can't hold every file name, and the
// SDK's GetExecutablePath needs a wide entry point (_get_wpgmptr fails the
// CRT's invalid-parameter check under plain main).
int Main(std::vector<std::string> args) {
  InstallCrashReporting();
  if (args.size() < 2) return Usage();
  // SDK diagnostics go to the console; HODTOOL_LOG=debug|trace for more.
  const char* log_level = std::getenv("HODTOOL_LOG");
  std::map<std::string, std::string> category_levels;
  auto log_config =
      rex::BuildLogConfig(nullptr, log_level ? log_level : "warn", category_levels);
  log_config.log_to_console = true;
  rex::InitLogging(log_config);

  std::string command = args[1];
  args.erase(args.begin(), args.begin() + 2);
  try {
    if (command == "info") return CmdInfo(args);
    if (command == "extract") return CmdExtract(args);
    if (command == "install") return CmdInstall(args);
    if (command == "install-dlc") return CmdInstallDlc(args);
    if (command == "scan") return CmdScan(args);
    if (command == "dump-image") return CmdDumpImage(args);
    if (command == "stage-modules") return CmdStageModules(args);
    if (command == "find-functions") return CmdFindFunctions(args);
  } catch (const std::exception& e) {
    fmt::print(stderr, "error: {}\n", e.what());
    return 1;
  }
  return Usage();
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    args.push_back(rex::string::to_utf8(std::u16string_view(reinterpret_cast<const char16_t*>(argv[i]))));
  }
  return Main(std::move(args));
}
#else
int main(int argc, char** argv) { return Main(std::vector<std::string>(argv, argv + argc)); }
#endif
