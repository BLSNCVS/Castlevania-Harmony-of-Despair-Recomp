// harmony-recomp - minimal SHA-256 (FIPS 180-4) used to fingerprint dumps.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace hod {

class Sha256 {
 public:
  Sha256();
  void Update(std::span<const uint8_t> data);
  std::array<uint8_t, 32> Finish();

 private:
  void Block(const uint8_t* p);

  std::array<uint32_t, 8> h_;
  std::array<uint8_t, 64> buffer_{};
  size_t buffered_ = 0;
  uint64_t total_bytes_ = 0;
};

std::string ToHex(std::span<const uint8_t> bytes);

// Returns the lowercase hex digest of a file, or nullopt if it can't be read.
std::optional<std::string> Sha256File(const std::filesystem::path& path);

}  // namespace hod
