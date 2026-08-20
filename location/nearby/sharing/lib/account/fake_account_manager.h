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

#ifndef LOCATION_NEARBY_SHARING_LIB_ACCOUNT_FAKE_ACCOUNT_MANAGER_H_
#define LOCATION_NEARBY_SHARING_LIB_ACCOUNT_FAKE_ACCOUNT_MANAGER_H_

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "location/nearby/sharing/lib/account/account_manager.h"

namespace nearby::sharing {

// A minimal test double for AccountManager: the current account is set
// directly via SetAccount(); Login()/Logout() just record what they were
// called with instead of doing real network I/O.
class FakeAccountManager : public AccountManager {
 public:
  std::optional<Account> GetCurrentAccount() override { return account_; }

  std::unique_ptr<SigninAttempt> Login(
      absl::string_view client_id,
      absl::string_view client_secret) override {
    last_login_client_id_ = std::string(client_id);
    last_login_client_secret_ = std::string(client_secret);
    return nullptr;
  }

  void Logout(
      absl::AnyInvocable<void(absl::Status)> logout_callback) override {
    account_.reset();
    if (logout_callback) {
      std::move(logout_callback)(absl::OkStatus());
    }
  }

  bool GetAccessToken(
      absl::AnyInvocable<void(absl::StatusOr<std::string>)> callback)
      override {
    if (callback) {
      std::move(callback)(std::string("fake_access_token"));
    }
    return true;
  }

  std::pair<std::string, std::string> GetOAuthClientCredential() override {
    return {last_login_client_id_, last_login_client_secret_};
  }

  void AddObserver(Observer* observer) override {
    observers_.push_back(observer);
  }

  void RemoveObserver(Observer* observer) override {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer),
                     observers_.end());
  }

  void SaveAccountPrefs(absl::string_view user_id, absl::string_view client_id,
                        absl::string_view client_secret) override {}

  // Test-only: directly sets (or clears, via std::nullopt) the account
  // GetCurrentAccount() returns.
  void SetAccount(std::optional<Account> account) {
    account_ = std::move(account);
  }

 private:
  std::optional<Account> account_;
  std::string last_login_client_id_;
  std::string last_login_client_secret_;
  std::vector<Observer*> observers_;
};

}  // namespace nearby::sharing

#endif  // LOCATION_NEARBY_SHARING_LIB_ACCOUNT_FAKE_ACCOUNT_MANAGER_H_
