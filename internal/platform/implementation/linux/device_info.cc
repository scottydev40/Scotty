// Copyright 2023 Google LLC
//
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

#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>

#include "absl/synchronization/mutex.h"
#include "internal/platform/implementation/device_info.h"
#include "internal/platform/implementation/linux/avahi.h"
#include "internal/platform/implementation/linux/dbus.h"
#include "internal/platform/implementation/linux/device_info.h"
#include "internal/platform/logging.h"

namespace nearby {
namespace linux {
void CurrentUserSession::RegisterScreenLockedListener(
    absl::string_view listener_name,
    std::function<void(api::DeviceInfo::ScreenStatus)> callback) {
  absl::MutexLock l(&screen_lock_listeners_mutex_);
  screen_lock_listeners_[listener_name] = std::move(callback);
}

void CurrentUserSession::UnregisterScreenLockedListener(
    absl::string_view listener_name) {
  absl::MutexLock l(&screen_lock_listeners_mutex_);
  screen_lock_listeners_.erase(listener_name);
}

void CurrentUserSession::onLock() {
  absl::ReaderMutexLock l(&screen_lock_listeners_mutex_);
  for (auto &[_, callback] : screen_lock_listeners_) {
    callback(api::DeviceInfo::ScreenStatus::kLocked);
  }
}

void CurrentUserSession::onUnlock() {
  absl::ReaderMutexLock l(&screen_lock_listeners_mutex_);
  for (auto &[_, callback] : screen_lock_listeners_) {
    callback(api::DeviceInfo::ScreenStatus::kUnlocked);
  }
}

DeviceInfo::DeviceInfo(std::shared_ptr<sdbus::IConnection> system_bus)
    : system_bus_(std::move(system_bus)),
      current_user_session_(std::make_unique<CurrentUserSession>(*system_bus_)),
      login_manager_(std::make_unique<LoginManager>(*system_bus_)) {}

std::optional<std::string> DeviceInfo::GetOsDeviceName() const {
  avahi::Server avahi(*system_bus_);
  try {
    return avahi.GetHostNameFqdn();
  } catch (const sdbus::Error &e) {
    DBUS_LOG_PROPERTY_GET_ERROR(&avahi, "GetHostNameFqdn", e);
    return std::nullopt;
  }
}

api::DeviceInfo::DeviceType DeviceInfo::GetDeviceType() const {
  Hostnamed hostnamed(*system_bus_);
  try {
    std::string chasis = hostnamed.Chassis();
    api::DeviceInfo::DeviceType device = api::DeviceInfo::DeviceType::kUnknown;
    if (chasis == "phone" || chasis == "handset") {
      device = api::DeviceInfo::DeviceType::kPhone;
    } else if (chasis == "laptop" || chasis == "desktop") {
      device = api::DeviceInfo::DeviceType::kLaptop;
    } else if (chasis == "tablet") {
      device = api::DeviceInfo::DeviceType::kTablet;
    }
    return device;
  } catch (const sdbus::Error &e) {
    DBUS_LOG_PROPERTY_GET_ERROR(&hostnamed, "Chasis", e);
    return api::DeviceInfo::DeviceType::kUnknown;
  }
}


namespace {

// The user's home directory, from $HOME or the passwd database. Empty if it
// cannot be determined.
std::string HomeDir() {
  const char *home = getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return home;
  }
  struct passwd *pw = getpwuid(getuid());
  if (pw != nullptr && pw->pw_dir != nullptr && pw->pw_dir[0] != '\0') {
    return pw->pw_dir;
  }
  return {};
}

// Resolves an XDG base directory: $env if set, else $HOME/<home_rel>. As a last
// resort (no HOME) a uid-namespaced directory under the system temp dir is used
// instead of a world-shared path. Never returns a bare, predictable "/tmp".
std::filesystem::path XdgBase(const char *env, const char *home_rel) {
  const char *val = getenv(env);
  if (val != nullptr && val[0] != '\0') {
    return std::filesystem::path(val);
  }
  std::string home = HomeDir();
  if (!home.empty()) {
    return std::filesystem::path(home) / home_rel;
  }
  return std::filesystem::path("/tmp") / ("scotty-" + std::to_string(getuid()));
}

// Creates `dir` (and parents) and restricts it to owner-only (0700) so private
// Nearby identity material (certificates, key pairs, secret keys) is not
// readable or traversable by other local users. Best-effort; returns the path.
FilePath EnsurePrivateDir(const std::filesystem::path &dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) {
    LOG(WARNING) << "Failed to create private directory " << dir.string()
                 << ": " << ec.message();
  }
  if (chmod(dir.c_str(), S_IRWXU) != 0) {
    LOG(WARNING) << "Failed to restrict permissions on " << dir.string() << ": "
                 << std::strerror(errno);
  }
  return FilePath(dir.string());
}

}  // namespace

FilePath DeviceInfo::GetDownloadPath() const {
  // User-facing downloads; not secret, so no 0700 tightening, but never /tmp.
  return FilePath(XdgBase("XDG_DOWNLOAD_DIR", "Downloads").string());
}

FilePath DeviceInfo::GetLocalAppDataPath(FilePath sub_path) const {
  return EnsurePrivateDir(XdgBase("XDG_CONFIG_HOME", ".config") /
                          "Google Nearby");
}

FilePath DeviceInfo::GetTemporaryPath() const {
  return EnsurePrivateDir(XdgBase("XDG_RUNTIME_DIR", ".cache") / "Google Nearby");
}

FilePath DeviceInfo::GetLogPath() const {
  return EnsurePrivateDir(XdgBase("XDG_STATE_HOME", ".local/state") /
                          "Google Nearby" / "logs");
}


bool DeviceInfo::IsScreenLocked() const {
  try {
    return current_user_session_->LockedHint();
  } catch (const sdbus::Error &e) {
    DBUS_LOG_PROPERTY_GET_ERROR(current_user_session_, "LockedHint", e);
    return false;
  }
}

bool DeviceInfo::PreventSleep() {
  try {
    inhibit_fd_ = login_manager_->Inhibit("sleep", "Google Nearby",
                                          "Google Nearby", "block");
    return true;
  } catch (const sdbus::Error &e) {
    DBUS_LOG_METHOD_CALL_ERROR(login_manager_, "Inhibit", e);
    return false;
  }
}

bool DeviceInfo::AllowSleep() {
  if (!inhibit_fd_.has_value()) {
    LOG(ERROR) << __func__
                       << "No inhibit lock is acquired at the moment";
    return false;
  }

  inhibit_fd_.reset();
  return true;
}

}  // namespace linux
}  // namespace nearby
