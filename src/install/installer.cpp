// harmony-recomp - installer core (see installer.h)

#include "installer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <queue>
#include <set>
#include <string_view>

#include <fmt/format.h>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/string/utf8.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>

#include "sha256.h"

namespace fs = std::filesystem;
using rex::filesystem::StfsContainerDevice;
using rex::system::XContentType;

namespace hod::install {

const std::vector<KnownExecutable>& KnownExecutables() {
  // Filled in once a dump has been verified end to end; see
  // docs/SUPPORTED_DUMPS.md.
  static const std::vector<KnownExecutable> kKnown = {};
  return kKnown;
}

const char* PackageKindName(PackageKind kind) {
  switch (kind) {
    case PackageKind::kGame:
      return "game";
    case PackageKind::kTitleUpdate:
      return "title update";
    case PackageKind::kDlc:
      return "DLC";
    case PackageKind::kOther:
      return "other";
  }
  return "other";
}

const PackageInfo* Sources::best_title_update() const {
  const PackageInfo* best = nullptr;
  for (const auto& tu : title_updates) {
    if (!best || tu.version > best->version) best = &tu;
  }
  return best;
}

// --- Progress ---------------------------------------------------------------

void Progress::Reset(uint64_t total_bytes) {
  total_ = total_bytes;
  done_ = 0;
}

void Progress::Clear() {
  Reset(0);
  cancelled_ = false;
  SetStep({});
}

void Progress::Advance(uint64_t bytes) { done_ += bytes; }

void Progress::SetStep(std::string step) {
  if (echo_ && !step.empty() && step != "Done") fmt::print("{}...\n", step);
  std::lock_guard lock(step_mutex_);
  step_ = std::move(step);
}

float Progress::fraction() const {
  uint64_t total = total_;
  return total ? std::min(1.0f, float(double(done_) / double(total))) : 0.0f;
}

std::string Progress::step() const {
  std::lock_guard lock(step_mutex_);
  return step_;
}

// --- Probing ----------------------------------------------------------------

std::optional<PackageInfo> ProbePackage(const fs::path& path) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec) || fs::file_size(path, ec) < sizeof(rex::filesystem::StfsHeader)) {
    return std::nullopt;
  }
  auto header = StfsContainerDevice::ReadPackageHeader(path);
  if (!header) {
    return std::nullopt;
  }
  const auto& meta = header->metadata;
  PackageInfo info;
  info.path = path;
  info.title_id = meta.execution_info.title_id;
  info.content_type = uint32_t(meta.content_type.get());
  info.media_id = meta.execution_info.media_id;
  info.version = meta.execution_info.version_value;
  info.display_name = rex::string::to_utf8(meta.display_name(rex::system::XLanguage::kEnglish));
  info.title_name = rex::string::to_utf8(meta.title_name());
  for (const auto& license : header->header.licenses) {
    if (license.license_flags) info.license_mask |= license.license_bits;
  }
  if (info.title_id == kTitleId) {
    switch (meta.content_type.get()) {
      case XContentType::kArcadeTitle:
        info.kind = PackageKind::kGame;
        break;
      case XContentType::kInstaller:
        info.kind = PackageKind::kTitleUpdate;
        break;
      case XContentType::kMarketplaceContent:
        info.kind = PackageKind::kDlc;
        break;
      default:
        break;
    }
  }
  return info;
}

namespace {

bool SamePath(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  return fs::equivalent(a, b, ec);
}

bool AlreadyListed(const Sources& sources, const fs::path& path) {
  auto listed = [&](const std::vector<PackageInfo>& list) {
    return std::any_of(list.begin(), list.end(),
                       [&](const PackageInfo& p) { return SamePath(p.path, path); });
  };
  return (sources.game && SamePath(sources.game->path, path)) || listed(sources.title_updates) ||
         listed(sources.dlc) || listed(sources.ignored);
}

void AddPackage(Sources& sources, PackageInfo info) {
  if (AlreadyListed(sources, info.path)) return;
  switch (info.kind) {
    case PackageKind::kGame:
      if (sources.game || sources.game_dir) {
        sources.warnings.push_back(
            fmt::format("More than one copy of the game found; ignoring {}", rex::path_to_utf8(info.path)));
        sources.ignored.push_back(std::move(info));
      } else {
        sources.game = std::move(info);
      }
      break;
    case PackageKind::kTitleUpdate:
      sources.title_updates.push_back(std::move(info));
      break;
    case PackageKind::kDlc:
      sources.dlc.push_back(std::move(info));
      break;
    case PackageKind::kOther:
      sources.ignored.push_back(std::move(info));
      break;
  }
}

}  // namespace

void AddSource(Sources& sources, const fs::path& path, const std::atomic<bool>* cancel) {
  std::error_code ec;
  if (fs::is_regular_file(path, ec)) {
    if (auto info = ProbePackage(path)) {
      AddPackage(sources, std::move(*info));
    } else if (path.filename() == "default.xex") {
      AddSource(sources, path.parent_path(), cancel);
    } else {
      sources.warnings.push_back(
          fmt::format("{} is not an Xbox 360 package", rex::path_to_utf8(path.filename())));
    }
    return;
  }
  if (!fs::is_directory(path, ec)) {
    sources.warnings.push_back(fmt::format("{} does not exist", rex::path_to_utf8(path)));
    return;
  }
  if (fs::exists(path / "default.xex", ec)) {
    // An extracted game folder: its files are game data, not packages.
    if (sources.game || sources.game_dir) {
      sources.warnings.push_back(
          fmt::format("More than one copy of the game found; ignoring {}", rex::path_to_utf8(path)));
    } else {
      sources.game_dir = path;
    }
    return;
  }
  size_t found = 0;
  for (auto it = fs::recursive_directory_iterator(
           path, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (cancel && cancel->load()) return;
    std::error_code entry_ec;
    if (it->is_directory(entry_ec)) {
      // An extracted game folder anywhere below: take it as the game and
      // don't probe its thousands of data files as packages.
      if (fs::exists(it->path() / "default.xex", entry_ec)) {
        AddSource(sources, it->path(), cancel);
        it.disable_recursion_pending();
        found++;
      }
      continue;
    }
    if (!it->is_regular_file(entry_ec)) continue;
    if (auto info = ProbePackage(it->path())) {
      AddPackage(sources, std::move(*info));
      found++;
    }
  }
  if (!found) {
    sources.warnings.push_back(fmt::format("No Xbox 360 packages found in {}", rex::path_to_utf8(path)));
  }
}

// Whether one component of a package entry name is safe to create as a
// file or folder on any OS: printable, none of Windows' reserved characters
// or device names (CON, NUL, COM1, ... with any extension), no trailing dot
// or space, no "." or "..".
namespace {
bool SafeComponent(std::string_view part) {
  if (part.empty() || part.size() > 255 || part == "." || part == "..") return false;
  for (unsigned char c : part) {
    if (c < 0x20 || c == 0x7F) return false;
    if (std::string_view("<>:\"/\\|?*").find(char(c)) != std::string_view::npos) return false;
  }
  if (part.back() == '.' || part.back() == ' ') return false;
  std::string stem(part.substr(0, part.find('.')));
  for (auto& c : stem) c = char(std::toupper(static_cast<unsigned char>(c)));
  static constexpr std::string_view kDevices[] = {"CON", "PRN", "AUX", "NUL"};
  for (auto device : kDevices) {
    if (stem == device) return false;
  }
  if (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
      stem[3] >= '0' && stem[3] <= '9') {
    return false;
  }
  return true;
}

}  // namespace

// Package entry names come from the package's file table, which anyone can
// write. Refuse anything that isn't a plain relative path of safe names.
std::optional<fs::path> SafeEntryPath(std::string_view entry_path) {
  if (entry_path.empty() || entry_path.find('\0') != std::string_view::npos) return std::nullopt;
  fs::path relative;
  size_t start = 0;
  while (start <= entry_path.size()) {
    size_t end = entry_path.find_first_of("/\\", start);
    if (end == std::string_view::npos) end = entry_path.size();
    std::string_view part = entry_path.substr(start, end - start);
    if (!SafeComponent(part)) return std::nullopt;
    relative /= rex::to_path(part);
    start = end + 1;
  }
  return relative;
}

// --- Extraction -------------------------------------------------------------

namespace {

template <typename Fn>
void ForEachEntry(StfsContainerDevice& device, Fn&& fn) {
  std::queue<rex::filesystem::Entry*> queue;
  queue.push(device.ResolvePath(""));
  while (!queue.empty()) {
    auto* entry = queue.front();
    queue.pop();
    if (!entry) continue;
    for (auto& child : entry->children()) queue.push(child.get());
    if (!entry->path().empty()) fn(entry);
  }
}

uint64_t PackageContentBytes(const fs::path& package) {
  StfsContainerDevice device("", package);
  if (!device.Initialize()) return 0;
  uint64_t total = 0;
  ForEachEntry(device, [&](rex::filesystem::Entry* entry) {
    if (!(entry->attributes() & rex::filesystem::kFileAttributeDirectory)) total += entry->size();
  });
  return total;
}

uint64_t FolderBytes(const fs::path& dir) {
  uint64_t total = 0;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(dir, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec)) total += it->file_size(ec);
  }
  return total;
}

bool Fail(std::string* error, std::string message) {
  if (error) *error = std::move(message);
  return false;
}

bool CopyFolder(const fs::path& from, const fs::path& to, Progress* progress, std::string* error) {
  std::error_code ec;
  // Copying into a folder inside the source would copy its own output.
  auto source = fs::weakly_canonical(from, ec);
  auto target = fs::weakly_canonical(to, ec);
  auto [source_end, target_it] = std::mismatch(source.begin(), source.end(), target.begin(), target.end());
  if (source_end == source.end()) {
    return Fail(error, "The install folder can't be inside the game folder it copies from");
  }
  fs::create_directories(to, ec);
  for (auto it = fs::recursive_directory_iterator(from, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (progress && progress->cancelled()) return Fail(error, "Installation cancelled");
    // Links could point anywhere on the PC; game files are never links.
    if (it->is_symlink(ec)) continue;
    auto dest = to / fs::relative(it->path(), from);
    if (it->is_directory(ec)) {
      fs::create_directories(dest, ec);
    } else if (it->is_regular_file(ec)) {
      fs::create_directories(dest.parent_path(), ec);
      if (!fs::copy_file(it->path(), dest, fs::copy_options::overwrite_existing, ec)) {
        return Fail(error, fmt::format("Cannot copy {}: {}", rex::path_to_utf8(it->path()), ec.message()));
      }
      if (progress) progress->Advance(it->file_size(ec));
    }
  }
  if (ec) return Fail(error, fmt::format("Cannot read {}: {}", rex::path_to_utf8(from), ec.message()));
  return true;
}

bool WriteDlcHeader(const PackageInfo& dlc, const fs::path& user_data_dir, std::string* error) {
  using rex::system::xam::XCONTENT_AGGREGATE_DATA;
  auto file_name = rex::path_to_utf8(dlc.path.filename());
  // The header holds the package's file name; the runtime finds the DLC by it.
  constexpr size_t kMaxFileName = 42;
  if (file_name.size() > kMaxFileName) {
    return Fail(error, fmt::format("The DLC file name {} is too long; use the package's original "
                                   "name (42 characters at most)",
                                   file_name));
  }
  XCONTENT_AGGREGATE_DATA data{};
  data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
  data.content_type = XContentType::kMarketplaceContent;
  data.set_display_name(rex::string::to_utf16(dlc.display_name));
  data.set_file_name(file_name);
  data.xuid = 0;
  data.title_id = dlc.title_id;

  // Same layout as the runtime's ContentManager::ResolvePackageHeaderPath.
  auto header_path = user_data_dir / "0000000000000000" / fmt::format("{:08X}", dlc.title_id) /
                     "Headers" / "00000002" / (file_name + ".header");
  std::error_code ec;
  fs::create_directories(header_path.parent_path(), ec);
  std::ofstream out(header_path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&data), sizeof(data));
  if (dlc.license_mask) {
    out.write(reinterpret_cast<const char*>(&dlc.license_mask), sizeof(dlc.license_mask));
  }
  if (!out) return Fail(error, fmt::format("Cannot write {}", rex::path_to_utf8(header_path)));
  return true;
}

}  // namespace

bool ExtractPackage(const fs::path& package, const fs::path& out_dir, Progress* progress,
                    std::string* error) {
  StfsContainerDevice device("", package);
  if (!device.Initialize()) {
    return Fail(error, fmt::format("{} is not a readable package", rex::path_to_utf8(package.filename())));
  }
  // STFS packages aren't compressed: their files can't add up to more than
  // the package itself. A damaged or crafted file table could otherwise
  // claim gigabytes and fill the disk.
  std::error_code size_ec;
  const uint64_t package_size = fs::file_size(package, size_ec);
  uint64_t claimed = 0;
  ForEachEntry(device, [&](rex::filesystem::Entry* entry) {
    if (!(entry->attributes() & rex::filesystem::kFileAttributeDirectory)) claimed += entry->size();
  });
  if (size_ec || claimed > package_size) {
    return Fail(error, fmt::format("{} is damaged (its files add up to more than the package)",
                                   rex::path_to_utf8(package.filename())));
  }
  std::vector<uint8_t> buffer(4 << 20);
  bool ok = true;
  ForEachEntry(device, [&](rex::filesystem::Entry* entry) {
    if (!ok) return;
    if (progress && progress->cancelled()) {
      ok = Fail(error, "Installation cancelled");
      return;
    }
    auto relative = SafeEntryPath(entry->path());
    if (!relative) {
      ok = Fail(error, fmt::format("Refusing unsafe path '{}' in {}", entry->path(),
                                   rex::path_to_utf8(package.filename())));
      return;
    }
    auto dest = out_dir / *relative;
    std::error_code ec;
    if (entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
      fs::create_directories(dest, ec);
      if (ec) ok = Fail(error, fmt::format("Cannot create {}: {}", rex::path_to_utf8(dest), ec.message()));
      return;
    }
    fs::create_directories(dest.parent_path(), ec);
    if (ec) {
      ok = Fail(error, fmt::format("Cannot create {}: {}", rex::path_to_utf8(dest.parent_path()),
                                   ec.message()));
      return;
    }
    rex::filesystem::File* in_file = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &in_file) != 0) {
      ok = Fail(error, fmt::format("Cannot read {} from the package", entry->path()));
      return;
    }
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
      in_file->Destroy();
      ok = Fail(error, fmt::format("Cannot write {}", rex::path_to_utf8(dest)));
      return;
    }
    size_t offset = 0;
    size_t remaining = entry->size();
    while (remaining) {
      size_t want = std::min(remaining, buffer.size());
      size_t got = 0;
      in_file->ReadSync(std::span<uint8_t>(buffer.data(), want), offset, &got);
      if (got == 0) {
        ok = Fail(error, fmt::format("{} is truncated or damaged", entry->path()));
        break;
      }
      out.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(got));
      offset += got;
      remaining -= got;
      if (progress) progress->Advance(got);
    }
    in_file->Destroy();
    if (ok && !out) ok = Fail(error, fmt::format("Cannot write {}", rex::path_to_utf8(dest)));
  });
  return ok;
}

fs::path DlcInstallPath(const fs::path& user_data_dir, const PackageInfo& dlc) {
  // Same layout as the runtime's ContentManager::ResolvePackagePath.
  return user_data_dir / "0000000000000000" / fmt::format("{:08X}", dlc.title_id) / "00000002" /
         dlc.path.filename();
}

bool InstallDlc(const PackageInfo& dlc, const fs::path& user_data_dir, Progress* progress,
                std::string* error) {
  if (dlc.kind != PackageKind::kDlc) {
    return Fail(error, fmt::format("{} is not Harmony of Despair DLC", rex::path_to_utf8(dlc.path)));
  }
  if (!ExtractPackage(dlc.path, DlcInstallPath(user_data_dir, dlc), progress, error)) return false;
  return WriteDlcHeader(dlc, user_data_dir, error);
}

fs::path UpdateDirFor(const fs::path& game_dir) {
  auto absolute = fs::absolute(game_dir).lexically_normal();
  if (absolute.filename().empty()) absolute = absolute.parent_path();
  return absolute.parent_path() / "update";
}

namespace {
// Present while an install is in progress; a cancelled or failed install
// leaves it behind, so the half-copied game isn't taken for a complete one.
constexpr const char* kInstallingMarker = ".hodrecomp_installing";
}  // namespace

bool IsInstalled(const fs::path& game_dir) {
  std::error_code ec;
  return !game_dir.empty() && fs::is_regular_file(game_dir / "default.xex", ec) &&
         !fs::exists(game_dir / kInstallingMarker, ec);
}

Result Install(const Request& request, Progress* progress) {
  Result result;
  const auto& sources = request.sources;
  if (!sources.has_game()) {
    result.error = "No copy of the game was selected";
    return result;
  }
  const PackageInfo* title_update =
      request.install_title_update ? sources.best_title_update() : nullptr;
  // A title update names the media ID of the release it patches. Packages
  // don't always fill the field in, so only compare when both do.
  if (title_update && sources.game && title_update->media_id && sources.game->media_id &&
      title_update->media_id != sources.game->media_id) {
    result.error = fmt::format(
        "The title update is for a different release of the game (media ID {:08X}, game is {:08X})",
        title_update->media_id, sources.game->media_id);
    return result;
  }

  if (progress) {
    progress->SetStep("Preparing");
    uint64_t total = sources.game ? PackageContentBytes(sources.game->path)
                                  : FolderBytes(*sources.game_dir);
    if (title_update) total += PackageContentBytes(title_update->path);
    if (request.install_dlc) {
      for (const auto& dlc : sources.dlc) total += PackageContentBytes(dlc.path);
    }
    progress->Reset(total);
  }

  std::error_code ec;
  fs::create_directories(request.game_dir, ec);
  const auto marker = request.game_dir / kInstallingMarker;
  if (ec || !std::ofstream(marker)) {
    result.error = fmt::format("Cannot write to {}", rex::path_to_utf8(request.game_dir));
    return result;
  }

  // Base game.
  if (progress) progress->SetStep("Installing the game");
  if (sources.game) {
    if (!ExtractPackage(sources.game->path, request.game_dir, progress, &result.error)) {
      return result;
    }
  } else if (!SamePath(*sources.game_dir, request.game_dir)) {
    if (!CopyFolder(*sources.game_dir, request.game_dir, progress, &result.error)) return result;
  }
  auto xex = request.game_dir / "default.xex";
  auto digest = Sha256File(xex);
  if (!digest) {
    result.error = "The game files don't contain default.xex";
    return result;
  }
  result.xex_sha256 = *digest;
  const auto& known = KnownExecutables();
  result.xex_known = std::any_of(known.begin(), known.end(), [&](const KnownExecutable& k) {
    return result.xex_sha256 == k.sha256;
  });
  if (!known.empty() && !result.xex_known) {
    result.error = fmt::format(
        "This copy of the game (default.xex {}) is not a version this build supports",
        result.xex_sha256.substr(0, 16));
    return result;
  }

  // Title update: overlaid onto the game folder, where the SDK picks up
  // default.xexp and the per-DLL .dllp patches next to their modules, and
  // also kept whole for the UPDATE: mount the game reads data through.
  if (title_update) {
    if (progress) progress->SetStep("Applying the title update");
    if (!ExtractPackage(title_update->path, request.game_dir, progress, &result.error)) {
      return result;
    }
    // A title update patches the executable (default.xexp); it must not
    // replace the one checked above.
    if (Sha256File(xex) != digest) {
      result.error = "The title update tried to replace the game's executable";
      return result;
    }
    result.update_dir = UpdateDirFor(request.game_dir);
    if (!ExtractPackage(title_update->path, result.update_dir, nullptr, &result.error)) {
      return result;
    }
    result.title_update = title_update->display_name;
  }

  // DLC.
  if (request.install_dlc) {
    for (const auto& dlc : sources.dlc) {
      if (progress) progress->SetStep(fmt::format("Installing {}", dlc.display_name));
      if (!InstallDlc(dlc, request.user_data_dir, progress, &result.error)) return result;
      result.dlc.push_back(dlc.display_name);
    }
  }

  fs::remove(marker, ec);
  if (progress) {
    progress->SetStep("Done");
    progress->Advance(progress->total() - std::min(progress->total(), progress->done()));
  }
  result.ok = true;
  return result;
}

}  // namespace hod::install
