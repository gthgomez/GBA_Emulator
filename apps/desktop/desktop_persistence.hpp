#pragma once

// Host-side file persistence helpers for the SDL desktop frontend.
//
// Deliberately free of SDL and of emulator core state so the data-safety
// behaviour (atomic replacement, rejected-save preservation, save-path
// derivation) can be verified headlessly by tests/desktop_persistence_test.cpp
// without a window or audio device.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace gba::desktop::persistence {

// Narrow path strings crossing this API are UTF-8. SDL hands the host UTF-8
// paths for file dialogs and drag-and-drop, and main() converts command-line
// arguments (ANSI code page) to UTF-8 on the way in. All file access goes
// through this helper, so non-ASCII ROM names work on Windows regardless of
// the system's active code page (a plain fs::path(std::string) would decode
// with the ANSI code page and fail).
inline std::filesystem::path native_path(const std::string& utf8_path) {
  return std::filesystem::u8path(utf8_path);
}

// Reads a whole file. Returns an empty vector when the file is missing,
// unreadable, or genuinely empty; callers treat all three as "no data".
inline std::vector<std::uint8_t> read_binary_file(
    const std::string& path,
    std::size_t max_size = std::numeric_limits<std::size_t>::max()) {
  std::error_code ec;
  const std::filesystem::path native = native_path(path);
  // Directories can be opened on some platforms; reject anything that is not a
  // regular file so tellg() cannot report a nonsense size.
  if (!std::filesystem::is_regular_file(native, ec)) {
    return {};
  }
  std::ifstream file(native, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const std::streamsize size = file.tellg();
  if (size < 0 || static_cast<std::uintmax_t>(size) > max_size) {
    return {};
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file || file.gcount() != size) {
      return {};
    }
  }
  return bytes;
}

// Restore the original left by older versions' two-rename fallback.
inline bool recover_file_orphan(const std::filesystem::path& target,
                                std::error_code& ec) {
  namespace fs = std::filesystem;
  const auto target_status = fs::symlink_status(target, ec);
  if (target_status.type() != fs::file_type::not_found) {
    return !ec && fs::is_regular_file(target_status);
  }

  const auto backup = native_path(target.u8string() + ".old");
  const auto backup_status = fs::symlink_status(backup, ec);
  if (backup_status.type() == fs::file_type::not_found) {
    ec.clear();
    return true;
  }
  if (ec || !fs::is_regular_file(backup_status)) {
    return false;
  }
  fs::rename(backup, target, ec);
  return !ec;
}

inline std::vector<std::uint8_t> read_checkpoint(const std::string& path) {
  std::error_code ec;
  const auto target = native_path(path);
  if (std::filesystem::symlink_status(target, ec).type() ==
      std::filesystem::file_type::not_found) {
    const auto orphan = native_path(path + ".old");
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(orphan, ec))
               ? read_binary_file(path + ".old")
               : std::vector<std::uint8_t>{};
  }
  return read_binary_file(path);
}

// Stage in a unique directory on the destination filesystem and commit with
// one rename. An interrupted process can leave staging debris, while the live
// file always retains a complete payload. No power-loss durability is implied.
inline bool write_file_atomic(const std::string& path,
                              const std::vector<std::uint8_t>& bytes) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path target = native_path(path);
  const auto target_status = fs::symlink_status(target, ec);
  if (target_status.type() != fs::file_type::not_found &&
      (ec || !fs::is_regular_file(target_status))) {
    return false;
  }
  const fs::path dir =
      target.has_parent_path() ? target.parent_path() : fs::path(".");
  static std::atomic<std::uint64_t> next_id{0};
  fs::path staging;
  for (int attempt = 0; attempt < 16; ++attempt) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    staging = dir / (".gba-save-" + std::to_string(stamp) + "-" +
                     std::to_string(next_id.fetch_add(1, std::memory_order_relaxed)));
    if (fs::create_directory(staging, ec)) {
      break;
    }
    if (ec && ec != std::errc::file_exists) {
      return false;
    }
    staging.clear();
  }
  if (staging.empty()) {
    return false;
  }

  const fs::path temp = staging / "payload";
  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  if (out && !bytes.empty()) {
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
  out.close();
  bool written = false;
  if (out) {
    fs::rename(temp, target, ec);
    written = !ec;
  }
  std::error_code ignored;
  fs::remove(temp, ignored);
  fs::remove(staging, ignored);
  return written;
}

// Copies `path` to a unique sibling named `<path><suffix>` (appending
// ".1", ".2", ... if needed) so a rejected/corrupt save is preserved instead
// of being silently overwritten. Returns the backup path, or nullopt when
// there is nothing to back up or the copy failed.
inline std::optional<std::string> backup_file(const std::string& path,
                                              const std::string& suffix) {
  std::error_code ec;
  const std::filesystem::path native = native_path(path);
  const std::string base = path + suffix;
  for (int attempt = 0; attempt < 1000; ++attempt) {
    const std::string name = attempt == 0 ? base : base + "." + std::to_string(attempt);
    if (std::filesystem::copy_file(native, native_path(name), ec)) {
      return name;
    }
    if (ec != std::errc::file_exists) {
      break;
    }
  }
  return std::nullopt;
}

// Prefix shared by a ROM's cartridge save and save states. Lives next to the
// ROM by default, or in `save_directory` under the ROM's file name.
inline std::string save_data_prefix(const std::string& rom_path,
                                    const std::string& save_directory) {
  if (save_directory.empty()) {
    return rom_path;
  }
  return (native_path(save_directory) / native_path(rom_path).filename())
      .u8string();
}

inline std::string cartridge_save_path(const std::string& prefix) {
  return prefix + ".sav";
}

// Save states are keyed to the ROM, independent of whether the game uses
// cartridge-backed saving, so a game with no SRAM/flash still gets states.
inline std::string save_state_path(const std::string& prefix, int slot) {
  return prefix + ".state" + std::to_string(slot);
}

}  // namespace gba::desktop::persistence
