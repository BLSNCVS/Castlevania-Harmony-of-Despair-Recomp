// harmony-recomp - installer core
//
// Turns the user's own console packages into an installed game:
//
//   game package (000D0000)  -> <game_dir>/            default.xex, DLLs, data
//   title update (000B0000)  -> <game_dir>/            overlaid: default.xexp, *.dllp, data
//                            -> <game_dir>/../update/ mounted as UPDATE:, which the game reads
//   DLC packages (00000002)  -> <user_data>/0000000000000000/58410A7A/00000002/<package>/
//
// Shared by the first-run wizard in the app and by 'hodtool install'. Nothing
// here needs a running guest: packages are read with the SDK's STFS reader.

#pragma once

#include <atomic>
#include <string_view>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace hod::install {

// Castlevania: Harmony of Despair (XBLA).
constexpr uint32_t kTitleId = 0x58410A7A;

// SHA-256 of default.xex versions to accept. Empty: the recompiled code is
// generated from the player's own dump, so any version is accepted.
struct KnownExecutable {
  const char* sha256;
  const char* label;
};
const std::vector<KnownExecutable>& KnownExecutables();

enum class PackageKind {
  kGame,         // Arcade title (the base game)
  kTitleUpdate,  // Installer package carrying default.xexp
  kDlc,          // Marketplace content
  kOther,        // Another title, or a content type we don't install
};

const char* PackageKindName(PackageKind kind);

struct PackageInfo {
  std::filesystem::path path;
  PackageKind kind = PackageKind::kOther;
  uint32_t title_id = 0;
  uint32_t content_type = 0;
  uint32_t media_id = 0;
  uint32_t version = 0;
  uint32_t license_mask = 0;
  std::string display_name;
  std::string title_name;
};

// Reads one file's STFS header. Returns nullopt for anything that isn't a
// readable CON/LIVE/PIRS package.
std::optional<PackageInfo> ProbePackage(const std::filesystem::path& path);

// Everything found in the places the user pointed the installer at.
struct Sources {
  std::optional<PackageInfo> game;                 // Base game package...
  std::optional<std::filesystem::path> game_dir;   // ...or an already extracted game folder.
  std::vector<PackageInfo> title_updates;
  std::vector<PackageInfo> dlc;
  std::vector<PackageInfo> ignored;
  std::vector<std::string> warnings;

  bool has_game() const { return game.has_value() || game_dir.has_value(); }
  // The newest title update, if any.
  const PackageInfo* best_title_update() const;
};

// Adds whatever `path` holds to `sources`: a package file, an extracted game
// folder (one containing default.xex), or any folder that contains packages
// or extracted game folders, searched recursively (e.g. a copy of the
// console's Content folder).
// A package entry's name as a path under the extraction folder, or nullopt
// when it isn't safe to create on every OS (absolute, "..", control or
// reserved characters, Windows device names, trailing dot or space).
std::optional<std::filesystem::path> SafeEntryPath(std::string_view entry_path);

// Stops early (with what it found so far) once `cancel` is set.
void AddSource(Sources& sources, const std::filesystem::path& path,
               const std::atomic<bool>* cancel = nullptr);

// Thread-safe progress shared between the installer thread and the UI.
class Progress {
 public:
  void Reset(uint64_t total_bytes);
  void Clear();  // Back to the initial state, including cancellation.
  void Advance(uint64_t bytes);
  void SetStep(std::string step);
  void Cancel() { cancelled_ = true; }
  // Also print each step as it starts (command-line installs).
  void EchoSteps(bool echo) { echo_ = echo; }

  bool cancelled() const { return cancelled_; }
  uint64_t done() const { return done_; }
  uint64_t total() const { return total_; }
  float fraction() const;
  std::string step() const;

 private:
  std::atomic<uint64_t> done_{0};
  std::atomic<uint64_t> total_{0};
  std::atomic<bool> cancelled_{false};
  std::atomic<bool> echo_{false};
  mutable std::mutex step_mutex_;
  std::string step_;
};

struct Request {
  Sources sources;
  std::filesystem::path game_dir;       // Receives the game files.
  std::filesystem::path user_data_dir;  // Receives DLC (the runtime's user data root).
  bool install_title_update = true;
  bool install_dlc = true;
};

struct Result {
  bool ok = false;
  std::filesystem::path update_dir;  // Set when a title update was installed.
  std::string error;
  std::string xex_sha256;
  bool xex_known = false;  // Hash is in KnownExecutables().
  std::optional<std::string> title_update;
  std::vector<std::string> dlc;  // Display names of installed DLC.
};

// Runs the whole installation. Blocking; call from a worker thread when a UI
// is involved. Honors progress->Cancel() between files.
Result Install(const Request& request, Progress* progress = nullptr);

// Single steps, also used by hodtool.
bool ExtractPackage(const std::filesystem::path& package, const std::filesystem::path& out_dir,
                    Progress* progress, std::string* error);
bool InstallDlc(const PackageInfo& dlc, const std::filesystem::path& user_data_dir,
                Progress* progress, std::string* error);

// Where InstallDlc puts a package's files.
std::filesystem::path DlcInstallPath(const std::filesystem::path& user_data_dir,
                                     const PackageInfo& dlc);

// True when `game_dir` holds an installed game (default.xex present).
bool IsInstalled(const std::filesystem::path& game_dir);

// Where the title update's files go so the runtime can mount them as UPDATE:
// (the game opens paths like UPDATE:\data\opd\common\p_com00.opd).
std::filesystem::path UpdateDirFor(const std::filesystem::path& game_dir);

}  // namespace hod::install
