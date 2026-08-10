// Copyright 2023 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <exception>
#include <filesystem>  // NOLINT(build/c++17)
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

#include "internal/platform/implementation/linux/preferences_repository.h"
#include "internal/platform/logging.h"
#include "nlohmann/json.hpp"
#include "nlohmann/json_fwd.hpp"

namespace nearby {
namespace linux {
namespace {
using json = ::nlohmann::json;

constexpr char kPreferencesFileName[] = "preferences.json";
constexpr char kPreferencesBackupFileName[] = "preferences_bak.json";

// Writes `content` to `final_path` atomically and privately. The preferences
// hold serialized private certificates, key pairs, and secret keys, so the file
// must never be world-readable and must never be written through a symlink an
// attacker planted. Flow: write a fresh temp in the same directory (O_EXCL +
// O_NOFOLLOW, mode 0600) -> fsync -> rename over the target -> fsync the dir so
// the rename survives a crash. The target is thus always either the old or the
// new complete file, never truncated or missing.
bool WriteFileAtomic0600(const std::filesystem::path& dir,
                         const std::filesystem::path& final_path,
                         const std::string& content) {
  std::filesystem::path tmp = final_path;
  tmp += ".tmp." + std::to_string(::getpid());

  int fd = ::open(tmp.c_str(),
                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0 && errno == EEXIST) {
    // Stale temp from a killed run; drop it and retry once.
    ::unlink(tmp.c_str());
    fd = ::open(tmp.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  }
  if (fd < 0) {
    LOG(ERROR) << "Failed to open temp preferences file " << tmp.string()
               << ": " << std::strerror(errno);
    return false;
  }

  const char* p = content.data();
  size_t remaining = content.size();
  while (remaining > 0) {
    ssize_t n = ::write(fd, p, remaining);
    if (n < 0) {
      if (errno == EINTR) continue;
      LOG(ERROR) << "Failed to write preferences: " << std::strerror(errno);
      ::close(fd);
      ::unlink(tmp.c_str());
      return false;
    }
    p += static_cast<size_t>(n);
    remaining -= static_cast<size_t>(n);
  }

  if (::fsync(fd) != 0) {
    LOG(WARNING) << "fsync of preferences failed: " << std::strerror(errno);
  }
  if (::close(fd) != 0) {
    LOG(ERROR) << "close of preferences failed: " << std::strerror(errno);
    ::unlink(tmp.c_str());
    return false;
  }

  std::error_code ec;
  std::filesystem::rename(tmp, final_path, ec);
  if (ec) {
    LOG(ERROR) << "Failed to rename temp preferences into place: "
               << ec.message();
    ::unlink(tmp.c_str());
    return false;
  }

  // Persist the directory entry so the rename is durable across a crash.
  int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd >= 0) {
    ::fsync(dfd);
    ::close(dfd);
  }
  return true;
}

}  // namespace

json PreferencesRepository::LoadPreferences() {
  absl::MutexLock lock(&mutex_);
  std::optional<json> preferences = AttemptLoad();
  if (preferences.has_value()) {
    // The top level root should be an object, if it's not then something went
    // wrong or the file was corrupted.
    if (!preferences.value().is_object()) {
      LOG(ERROR) << "Preferences loaded was not a valid object: "
                         << preferences.value().dump(4);

      return json::object();
    }

    return preferences.value();
  }

  LOG(ERROR) << "Could not load preferences file, trying backup.";

  // In the future we should switch to using a transaction log or another
  // stable method which doesn't pose a risk of losing settings
  preferences = RestoreFromBackup();
  if (preferences.has_value()) {
    LOG(ERROR) << "Successfully recovered from backup.";
    return preferences.value();
  }

  LOG(ERROR) << "Failed to load preferences file from back up.";

  return json::object();
}

bool PreferencesRepository::SavePreferences(json preferences) {
  absl::MutexLock lock(&mutex_);
  try {
    std::filesystem::path path = path_;
    if (!std::filesystem::exists(path) &&
        !std::filesystem::create_directories(path)) {
      LOG(ERROR) << "Failed to create preferences path.";
      return false;
    }
    // Private identity material lives here; keep the directory owner-only so a
    // 0600 file cannot be reached by traversing a loose parent.
    if (::chmod(path.c_str(), S_IRWXU) != 0) {
      LOG(WARNING) << "Failed to restrict preferences dir permissions: "
                   << std::strerror(errno);
    }

    std::filesystem::path full_name = path / kPreferencesFileName;
    std::filesystem::path full_name_backup = path / kPreferencesBackupFileName;

    // Refresh the backup as a private copy first. Unlike a rename, this leaves
    // the live file in place, so a crash mid-save never loses the current
    // settings.
    if (std::filesystem::exists(full_name)) {
      std::error_code ec;
      std::filesystem::copy_file(
          full_name, full_name_backup,
          std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        LOG(WARNING) << "Failed to refresh preferences backup: " << ec.message();
      } else if (::chmod(full_name_backup.c_str(), S_IRUSR | S_IWUSR) != 0) {
        LOG(WARNING) << "Failed to restrict backup permissions: "
                     << std::strerror(errno);
      }
    }

    if (!WriteFileAtomic0600(path, full_name, preferences.dump())) {
      LOG(ERROR) << "Failed to write preferences file securely.";
      return false;
    }

    // Make sure the file wasn't saved in a corrupted state
    if (!AttemptLoad().has_value()) {
      LOG(ERROR) << "Preferences saved to disk in corrupted state. "
                            "Restoring from backup.";

      if (!RestoreFromBackup().has_value()) {
        LOG(ERROR) << "Failed to restore preferences file.";
        return false;
      }
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to save preferences file: " << e.what();
    return false;
  }

  return true;
}

std::optional<json> PreferencesRepository::AttemptLoad() {
  std::filesystem::path path = path_;
  std::filesystem::path full_name = path / kPreferencesFileName;
  if (!std::filesystem::exists(path) || !std::filesystem::exists(full_name)) {
    return std::nullopt;
  }

  try {
    std::ifstream preferences_file(full_name.c_str());
    if (!preferences_file.good()) {
      return std::nullopt;
    }

    json preferences = json::parse(preferences_file, nullptr, false);
    preferences_file.close();

    if (preferences.is_discarded()) {
      LOG(ERROR) << "Preferences file corrupted.";
      return std::nullopt;
    }

    return preferences;
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception while loading preferences: " << e.what();
    return std::nullopt;
  }
}

std::optional<json> PreferencesRepository::RestoreFromBackup() {
  std::filesystem::path path = path_;
  std::filesystem::path full_name = path / kPreferencesFileName;
  std::filesystem::path full_name_backup = path / kPreferencesBackupFileName;

  if (!std::filesystem::exists(full_name_backup)) {
    LOG(WARNING)
        << "Backup requested but no backup preferences file found.";
    return std::nullopt;
  }

  std::filesystem::rename(full_name_backup, full_name);

  LOG(INFO) << "Attempting load from backup preferences.";
  return AttemptLoad();
}

}  // namespace linux
}  // namespace nearby
