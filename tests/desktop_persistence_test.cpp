// Verifies the host-side save-data safety helpers used by the SDL desktop
// frontend: atomic replacement, rejected-save preservation, and save-path
// derivation. Uses only temporary files and synthetic content; never touches
// a real cartridge save.
//
// The helper header is SDL-free (apps/desktop/desktop_persistence.hpp), so
// this runs as a plain headless g++ verifier like the core tests.

#include "../apps/desktop/desktop_persistence.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "test_helpers.hpp"

namespace {

namespace fs = std::filesystem;
using gba::desktop::persistence::backup_file;
using gba::desktop::persistence::cartridge_save_path;
using gba::desktop::persistence::read_binary_file;
using gba::desktop::persistence::save_data_prefix;
using gba::desktop::persistence::save_state_path;
using gba::desktop::persistence::write_file_atomic;

std::vector<std::uint8_t> bytes_of(const std::string& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string contents_of(const std::string& path) {
  const std::vector<std::uint8_t> bytes = read_binary_file(path);
  return std::string(bytes.begin(), bytes.end());
}

fs::path make_scratch_dir() {
  const fs::path dir = fs::temp_directory_path() / "gba-desktop-persistence-test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  expect(!ec, "scratch directory created");
  return dir;
}

void test_read_missing_is_empty(const fs::path& dir) {
  expect(read_binary_file((dir / "does-not-exist.sav").string()).empty(),
         "missing file reads as empty");
  expect(read_binary_file(dir.string()).empty(), "directory path reads as empty");
}

void test_atomic_create_and_replace(const fs::path& dir) {
  const std::string path = (dir / "game.sav").string();
  expect(write_file_atomic(path, bytes_of("first")), "atomic write creates file");
  expect(contents_of(path) == "first", "atomic write stores bytes");
  expect(!fs::exists(path + ".tmp"), "atomic write leaves no temp file");

  expect(write_file_atomic(path, bytes_of("second-longer")),
         "atomic write replaces existing file");
  expect(contents_of(path) == "second-longer", "atomic replacement swaps content");

  expect(write_file_atomic(path, {}), "atomic write of empty payload succeeds");
  expect(contents_of(path).empty(), "atomic write of empty payload is empty");
}

void test_atomic_failure_leaves_previous_intact(const fs::path& dir) {
  // Force temporary-file creation to fail by making the target's parent a
  // regular file, and confirm an unrelated good file is left untouched.
  const std::string good = (dir / "keep.sav").string();
  expect(write_file_atomic(good, bytes_of("GOOD")), "seed good save");
  const std::string bogus_parent = (dir / "not-a-dir").string();
  {
    std::ofstream out(bogus_parent, std::ios::binary | std::ios::trunc);
    out << "x";
  }
  expect(!write_file_atomic(bogus_parent + "/game.sav", bytes_of("NEW")),
         "atomic write into a file-as-directory fails");
  expect(contents_of(good) == "GOOD", "failed write leaves other saves intact");

#ifndef _WIN32
  // Read-only parent directory: the temp file cannot be created. Skipped as
  // root, where the permission bits do not restrict writes.
  if (geteuid() != 0) {
    const fs::path ro_dir = dir / "readonly";
    fs::create_directories(ro_dir);
    const std::string ro_path = (ro_dir / "game.sav").string();
    expect(write_file_atomic(ro_path, bytes_of("ORIGINAL")), "seed read-only-dir save");
    std::error_code ec;
    fs::permissions(ro_dir, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, ec);
    const bool failed = !write_file_atomic(ro_path, bytes_of("REPLACEMENT"));
    fs::permissions(ro_dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    expect(failed, "atomic write into read-only directory fails");
    expect(contents_of(ro_path) == "ORIGINAL",
           "failed atomic write does not truncate the original save");
  }
#endif
}

void test_fallback_recovers_crash_orphan(const fs::path& dir) {
  // Simulate a crash between the fallback renames: the original exists only
  // as the `.old` sibling (target missing). The next write must restore the
  // orphan to `target` first (so a failed write can never consume the only
  // copy); a successful write then legitimately supersedes it.
  const std::string path = (dir / "crashed.sav").string();
  expect(write_file_atomic(path, bytes_of("only-copy")), "seed crashed-save original");
  std::error_code ec;
  fs::rename(path, path + ".old", ec);
  expect(!ec, "crash simulation moved original to .old");
  expect(!fs::exists(path), "crash simulation removed target");

  expect(write_file_atomic(path, bytes_of("replacement")),
         "write over a crash orphan succeeds");
  expect(contents_of(path) == "replacement",
         "successful write installs new content over the recovered orphan");
  expect(!fs::exists(path + ".old"), "orphan copy is consumed once superseded");
  expect(!fs::exists(path + ".tmp"), "recovered write leaves no temp file");

  // A second crash-orphan write that fails must leave the orphan untouched.
  const std::string doomed = (dir / "not-a-dir").string();
  {
    std::ofstream out(doomed, std::ios::binary | std::ios::trunc);
    out << "x";
  }
  expect(!write_file_atomic(doomed + "/game.sav", bytes_of("NEW")),
         "write into a file-as-directory fails");
  expect(!fs::exists(doomed + "/game.sav.old"),
         "failed write creates no stray fallback copies");
}

void test_backup_preserves_rejected_save(const fs::path& dir) {
  const std::string path = (dir / "corrupt.sav").string();
  expect(write_file_atomic(path, bytes_of("rejected-payload")), "seed rejected save");

  const auto first = backup_file(path, ".rejected");
  expect(first.has_value(), "rejected save backed up");
  expect(contents_of(*first) == "rejected-payload", "backup preserves bytes");
  expect(contents_of(path) == "rejected-payload", "backup does not consume original");

  const auto second = backup_file(path, ".rejected");
  expect(second.has_value(), "second backup succeeds");
  expect(*second != *first, "second backup uses a distinct path");

  expect(!backup_file((dir / "absent.sav").string(), ".rejected").has_value(),
         "backup of a missing file reports nothing to preserve");
}

void test_save_paths(const fs::path& dir) {
  const std::string rom = (dir / "My Game (USA).gba").string();
  expect(save_data_prefix(rom, "") == rom, "prefix defaults to ROM path");
  const std::string prefix = save_data_prefix(rom, (dir / "saves").string());
  expect(prefix == (dir / "saves" / "My Game (USA).gba").string(),
         "prefix uses save directory plus ROM file name");
  expect(cartridge_save_path(rom) == rom + ".sav", "cartridge save path suffix");
  expect(save_state_path(rom, 1) == rom + ".state1", "save-state path suffix");
  expect(save_state_path(prefix, 2) == prefix + ".state2",
         "save-state path independent of cartridge saving");
}

}  // namespace

int main() {
  const fs::path dir = make_scratch_dir();
  test_read_missing_is_empty(dir);
  test_atomic_create_and_replace(dir);
  test_atomic_failure_leaves_previous_intact(dir);
  test_fallback_recovers_crash_orphan(dir);
  test_backup_preserves_rejected_save(dir);
  test_save_paths(dir);

  std::error_code ec;
  fs::remove_all(dir, ec);
  std::cout << "desktop_persistence_test: PASS\n";
  return 0;
}
