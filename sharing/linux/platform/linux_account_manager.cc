// Copyright 2026 Google LLC
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

#include "sharing/linux/platform/linux_account_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "internal/platform/logging.h"

namespace nearby::sharing::linux::internal {
namespace {

// The opt-in My-Devices plugin's session-bus seam (see
// scotty-mydevices/interface/dev.scotty.MyDevices1.xml). The core links no grey
// code; it only talks to this interface. If the name has no owner and is not
// activatable, every call throws and this manager stays inert -- i.e. the stock
// Everyone / No-one core with no account.
constexpr char kPluginBusName[] = "dev.scotty.MyDevices1";
constexpr char kPluginObjPath[] = "/dev/scotty/MyDevices1";
constexpr char kPluginIface[] = "dev.scotty.MyDevices1";

class FailedSigninAttempt final : public SigninAttempt {
 public:
  std::string Start(
      absl::AnyInvocable<void(AuthStatus, absl::string_view, absl::string_view,
                              const AccountInfo&)>
          callback) override {
    if (callback) {
      std::move(callback)(UNSUPPORTED, "", "", AccountInfo{});
    }
    return {};
  }

  void Close() override {}
};

// AccountManager backed by the My-Devices plugin over session D-Bus. The plugin
// owns all account/token/RPC (grey) code; this class is a thin proxy that
// exposes the plugin's account state to the in-process cert manager, and is
// completely inert when the plugin is absent.
class LinuxAccountManager final : public AccountManager {
 public:
  LinuxAccountManager() {
    // A missing session bus (headless) or a missing plugin must both degrade to
    // "no account", never crash. Everything here is best-effort.
    try {
      connection_ = sdbus::createSessionBusConnection();
    } catch (const sdbus::Error& e) {
      LOG(INFO) << "My-Devices: no session bus (" << e.what()
                << "); account features disabled.";
      return;
    }
    try {
      proxy_ = sdbus::createProxy(*connection_,
                                  sdbus::ServiceName{kPluginBusName},
                                  sdbus::ObjectPath{kPluginObjPath});
      proxy_->uponSignal("AccountChanged")
          .onInterface(kPluginIface)
          .call([this](const bool& signed_in, const std::string& email) {
            OnAccountChanged(signed_in, email);
          });
      connection_->enterEventLoopAsync();
    } catch (const sdbus::Error& e) {
      LOG(INFO) << "My-Devices: plugin proxy unavailable (" << e.what() << ").";
      proxy_.reset();
      return;
    }
    // Best-effort: if the plugin is already up and signed in, seed the cache.
    RefreshAccountFromPlugin();
  }

  std::optional<Account> GetCurrentAccount() override {
    absl::MutexLock lock(&mutex_);
    return account_;
  }

  std::unique_ptr<SigninAttempt> Login(absl::string_view client_id,
                                       absl::string_view client_secret)
      override {
    // Sign-in is plugin-driven (UI -> StartSignIn), not service-driven.
    last_client_id_ = std::string(client_id);
    last_client_secret_ = std::string(client_secret);
    return std::make_unique<FailedSigninAttempt>();
  }

  void Logout(absl::AnyInvocable<void(absl::Status)> logout_callback) override {
    absl::Status status = absl::OkStatus();
    if (proxy_ != nullptr) {
      try {
        proxy_->callMethod("SignOut").onInterface(kPluginIface);
      } catch (const sdbus::Error& e) {
        status = absl::UnavailableError(e.what());
      }
    }
    if (logout_callback) {
      std::move(logout_callback)(status);
    }
  }

  bool GetAccessToken(
      absl::AnyInvocable<void(absl::StatusOr<std::string>)> callback)
      override {
    if (!callback) {
      return false;
    }
    // The core never handles tokens; the plugin holds them and makes the RPCs
    // itself. Core RPC paths route through the plugin, not this token.
    std::move(callback)(absl::UnavailableError(
        "access tokens are held by the My-Devices plugin, not the core"));
    return true;
  }

  std::pair<std::string, std::string> GetOAuthClientCredential() override {
    return {last_client_id_, last_client_secret_};
  }

  void AddObserver(Observer* observer) override {
    absl::MutexLock lock(&mutex_);
    observers_.insert(observer);
  }
  void RemoveObserver(Observer* observer) override {
    absl::MutexLock lock(&mutex_);
    observers_.erase(observer);
  }

  void SaveAccountPrefs(absl::string_view user_id, absl::string_view client_id,
                        absl::string_view client_secret) override {
    static_cast<void>(user_id);
    last_client_id_ = std::string(client_id);
    last_client_secret_ = std::string(client_secret);
  }

 private:
  // Synchronous GetAccountInfo against the plugin. Safe to call from any thread
  // EXCEPT the D-Bus event-loop thread (a sync call from inside a signal
  // callback on the same connection can deadlock) -- so it is only called at
  // construction, never from OnAccountChanged.
  void RefreshAccountFromPlugin() {
    if (proxy_ == nullptr) {
      return;
    }
    bool signed_in = false;
    std::string email;
    std::string account_id;
    try {
      proxy_->callMethod("GetAccountInfo")
          .onInterface(kPluginIface)
          .storeResultsTo(signed_in, email, account_id);
    } catch (const sdbus::Error& e) {
      // Plugin installed but not running / not signed in -> stay inert.
      return;
    }
    absl::MutexLock lock(&mutex_);
    SetAccountLocked(signed_in, email, account_id);
  }

  // Push update from the plugin. Runs on the D-Bus event-loop thread, so it must
  // NOT make a synchronous call back on the same connection. account_id is not
  // carried by the signal; the query path uses the symbolic "devices/me", so the
  // email is sufficient identity here. (A later milestone may async-refresh the
  // dusi id if a call site needs it.)
  void OnAccountChanged(bool signed_in, const std::string& email) {
    absl::flat_hash_set<Observer*> to_notify;
    std::string id;
    {
      absl::MutexLock lock(&mutex_);
      SetAccountLocked(signed_in, email, /*account_id=*/email);
      to_notify = observers_;
      id = email;
    }
    for (Observer* observer : to_notify) {
      if (signed_in) {
        observer->OnLoginSucceeded(id);
      } else {
        observer->OnLogoutSucceeded(id, /*credential_error=*/false);
      }
    }
  }

  void SetAccountLocked(bool signed_in, const std::string& email,
                        const std::string& account_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    if (signed_in && !email.empty()) {
      Account account;
      account.id = account_id.empty() ? email : account_id;
      account.email = email;
      account.display_name = email;
      account_ = std::move(account);
    } else {
      account_ = std::nullopt;
    }
  }

  std::unique_ptr<sdbus::IConnection> connection_;
  std::unique_ptr<sdbus::IProxy> proxy_;

  absl::Mutex mutex_;
  std::optional<Account> account_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<Observer*> observers_ ABSL_GUARDED_BY(mutex_);
  std::string last_client_id_;
  std::string last_client_secret_;
};

}  // namespace

std::unique_ptr<AccountManager> CreateLinuxAccountManager() {
  return std::make_unique<LinuxAccountManager>();
}

}  // namespace nearby::sharing::linux::internal
