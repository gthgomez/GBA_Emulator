#pragma once

// Host-side file persistence helpers for the SDL desktop frontend.
//
// Deliberately free of SDL and of emulator core state so the data-safety
// behaviour (atomic replacement, rejected-save preservation, save-path
// derivation) can be verified headlessly by tests/desktop_persistence_test.cpp
// without a window or audio device.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace gba::desktop::persistence {

// Reads a whole file. Returns an empty vector when the file is missing,
// unreadable, or genuinely empty; callers treat all three as "no data".
inline std::vector<std::uint8_t> read_binary_file(const std::string& path) {
  std::error_code ec;
  // Directories can be opened on some platforms; reject anything that is not a
  // regular file so tellg() cannot report a nonsense size.
  if (!std::filesystem::is_regular_file(path, ec)) {
    return {};
  }
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return {};
  }
  const std::streamsize size = file.tellg();
  if (size < 0) {
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

// Writes `bytes` by first materializing a sibling temporary file, flushing it,
// then renaming it over `path`. On POSIX rename(2) atomically replaces the
// destination; std::filesystem::rename does the same on Windows (it maps to a
// replace-existing move), with an explicit fallback for filesystems that
// refuse the replace.
//
// Only the rename path is atomic. The fallback (move the old file aside, put
// the new one in place, restore the old one on failure) has a crash window in
// which the original exists only as the `.old` sibling; the next write
// therefore restores such an orphan BEFORE writing anything, so a failed
// follow-up write can never consume the only surviving copy.
//
// The `<name>.tmp` / `<name>.old` sibling names are owned by this helper and
// may be reclaimed between calls; they are not safe as user storage. The temp
// name is deterministic, so two processes writing the same file concurrently
// can collide -- single-writer per save file is assumed.
//
// This is crash-safe against process termination, but it is NOT a claim of
// power-loss durability: no fsync of the file or its directory is performed.
inline bool write_file_atomic(const std::string& path,
                              const std::vector<std::uint8_t>& bytes) {
  std::error_code ec;
  const std::filesystem::path target(path);
  const std::filesystem::path dir =
      target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
  const std::filesystem::path temp = dir / (target.filename().string() + ".tmp");

  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    if (!bytes.empty()) {
      out.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    out.flush();
    if (!out) {
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
      return false;
    }
  }

  // `.old` is the fallback's move-aside slot.
  const std::filesystem::path backup =
      dir / (target.filename().string() + ".old");

  // A previous run may have crashed between the fallback's two renames: the
  // original then exists only as `backup`. Put it back first -- if this write
  // subsequently fails, the orphan must still be sitting at `target`, not
  // consumed as garbage.
  std::error_code probe_ec;
  const bool target_missing = !std::filesystem::exists(target, probe_ec);
  const bool backup_exists = std::filesystem::exists(backup, probe_ec);
  if (target_missing && backup_exists) {
    std::error_code recover_ec;
    std::filesystem::rename(backup, target, recover_ec);
    if (recover_ec) {
      // Recovery failed: `backup` is still the only copy; fail without
      // touching it.
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
      return false;
    }
  }

  std::filesystem::rename(temp, target, ec);
  if (!ec) {
    // The live file is at `target`; any `.old` left here is a stale leftover
    // (older content of the same save) and can be reclaimed.
    std::error_code ignored;
    std::filesystem::remove(backup, ignored);
    return true;
  }
  // The platform refused to replace in place (locked or read-only target).
  // Move the old file aside, put the new one in place, and restore the old
  // one if that fails -- the original is never destroyed before its
  // replacement is committed.
  std::error_code move_ec;
  std::filesystem::remove(backup, move_ec);
  move_ec.clear();
  std::filesystem::rename(target, backup, move_ec);
  if (move_ec) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return false;
  }
  std::error_code replace_ec;
  std::filesystem::rename(temp, target, replace_ec);
  if (replace_ec) {
    std::error_code ignored;
    std::filesystem::rename(backup, target, ignored);
    std::filesystem::remove(temp, ignored);
    return false;
  }
  std::error_code ignored;
  std::filesystem::remove(backup, ignored);
  return true;
}

// Copies `path` to a unique sibling named `<path><suffix>` (appending
// ".1", ".2", ... if needed) so a rejected/corrupt save is preserved instead
// of being silently overwritten. Returns the backup path, or nullopt when
// there is nothing to back up or the copy failed.
inline std::optional<std::string> backup_file(const std::string& path,
                                              const std::string& suffix) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return std::nullopt;
  }
  const std::filesystem::path base(path + suffix);
  std::filesystem::path candidate = base;
  for (int attempt = 1; std::filesystem::exists(candidate, ec) && attempt < 1000;
       ++attempt) {
    candidate = std::filesystem::path(base.string() + "." + std::to_string(attempt));
  }
  std::filesystem::copy_file(path, candidate,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return std::nullopt;
  }
  return candidate.string();
}

// Prefix shared by a ROM's cartridge save and save states. Lives next to the
// ROM by default, or in `save_directory` under the ROM's file name.
inline std::string save_data_prefix(const std::string& rom_path,
                                    const std::string& save_directory) {
  if (save_directory.empty()) {
    return rom_path;
  }
  return (std::filesystem::path(save_directory) /
          std::filesystem::path(rom_path).filename())
      .string();
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
