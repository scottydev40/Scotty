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

#include "sharing/certificates/nearby_share_certificate_manager_impl.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <random>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "location/nearby/sharing/lib/account/account_manager.h"
#include "location/nearby/sharing/lib/rpc/sharing_rpc_client.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "internal/base/file_path.h"
#include "internal/platform/mac_address.h"
#include "sharing/certificates/common.h"
#include "sharing/certificates/constants.h"
#include "sharing/certificates/nearby_share_certificate_manager.h"
#include "sharing/certificates/nearby_share_certificate_storage.h"
#include "sharing/certificates/nearby_share_certificate_storage_impl.h"
#include "sharing/certificates/nearby_share_decrypted_public_certificate.h"
#include "sharing/certificates/nearby_share_encrypted_metadata_key.h"
#include "sharing/certificates/nearby_share_private_certificate.h"
#include "sharing/internal/api/bluetooth_adapter.h"
#include "sharing/internal/api/preference_manager.h"
#include "sharing/internal/api/public_certificate_database.h"
#include "sharing/internal/api/sharing_platform.h"
#include "sharing/internal/base/encode.h"
#include "sharing/internal/public/context.h"
#include "sharing/internal/public/logging.h"
#include "sharing/internal/public/pref_names.h"
#include "sharing/local_device_data/nearby_share_local_device_data_manager.h"
#include "sharing/proto/certificate_rpc.pb.h"
#include "sharing/proto/encrypted_metadata.pb.h"
#include "sharing/proto/enums.pb.h"
#include "sharing/proto/rpc_resources.pb.h"
#include "sharing/scheduling/nearby_share_scheduler.h"
#include "sharing/scheduling/nearby_share_scheduler_factory.h"

namespace nearby::sharing {
namespace {

using ::nearby::sharing::api::PreferenceManager;
using ::nearby::sharing::api::PublicCertificateDatabase;
using ::nearby::sharing::api::SharingPlatform;
using ::nearby::sharing::proto::DeviceVisibility;
using ::nearby::sharing::proto::EncryptedMetadata;
using ::nearby::sharing::proto::PublicCertificate;

constexpr std::array<DeviceVisibility, 2> kVisibilities = {
    DeviceVisibility::DEVICE_VISIBILITY_ALL_CONTACTS,
    DeviceVisibility::DEVICE_VISIBILITY_SELF_SHARE,
};

const absl::string_view kPublicCertificateDatabaseName =
    "NearbySharePublicCertificateDatabase";

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class GetDecryptedPublicCertificateResult {
  kSuccess = 0,
  kNoMatch = 1,
  kStorageFailure = 2,
  kMaxValue = kStorageFailure
};

size_t NumExpectedPrivateCertificates() {
  return kVisibilities.size() * kNearbyShareNumPrivateCertificates;
}

std::optional<EncryptedMetadata> BuildMetadata(
    std::string device_name, std::optional<std::string> full_name,
    std::optional<std::string> icon_url,
    std::optional<std::string> account_name, int32_t vendor_id,
    Context* context) {
  EncryptedMetadata metadata;
  if (device_name.empty()) {
    LOG(WARNING) << "Failed to create private certificate metadata; "
                    "missing device name.";
    return std::nullopt;
  }

  metadata.set_device_name(device_name);
  if (full_name.has_value()) {
    metadata.set_full_name(*full_name);
  }
  if (icon_url.has_value()) {
    metadata.set_icon_url(*icon_url);
  }
  if (account_name.has_value()) {
    metadata.set_account_name(*account_name);
  }
  metadata.set_vendor_id(vendor_id);

  MacAddress mac_address = context->GetBluetoothAdapter().GetAddress();
  if (mac_address.IsSet()) {
    std::string mac_address_string(6, ' ');
    if (mac_address.ToBytes(absl::MakeSpan(
            reinterpret_cast<uint8_t*>(mac_address_string.data()), 6))) {
      metadata.set_bluetooth_mac_address(mac_address_string);
    }
  }
  return metadata;
}

void TryDecryptPublicCertificates(
    const NearbyShareEncryptedMetadataKey& encrypted_metadata_key,
    NearbyShareCertificateManager::CertDecryptedCallback callback, bool success,
    std::unique_ptr<std::vector<PublicCertificate>> public_certificates) {
  if (!success || !public_certificates) {
    LOG(ERROR) << "Failed to read public certificates from storage.";
    std::move(callback)(std::nullopt);
    return;
  }

  for (const auto& cert : *public_certificates) {
    std::optional<NearbyShareDecryptedPublicCertificate> decrypted =
        NearbyShareDecryptedPublicCertificate::DecryptPublicCertificate(
            cert, encrypted_metadata_key);
    if (decrypted) {
      VLOG(1) << "Successfully decrypted public certificate with ID "
              << nearby::utils::HexEncode(decrypted->id());
      std::move(callback)(std::move(decrypted));
      return;
    }
  }
  VLOG(1) << "Metadata key could not decrypt any public certificates.";
  std::move(callback)(std::nullopt);
}

void DumpCertificateId(std::stringstream& sstream, absl::string_view cert_id,
                       bool is_public_cert) {
  if (is_public_cert) {
    sstream << "  Public certificates:[";
  } else {
    sstream << "  Private certificates:[";
  }
  sstream << absl::BytesToHexString(cert_id) << "]" << std::endl;
}

}  // namespace

// static
NearbyShareCertificateManagerImpl::Factory*
    NearbyShareCertificateManagerImpl::Factory::test_factory_ = nullptr;

// static
std::unique_ptr<NearbyShareCertificateManager>
NearbyShareCertificateManagerImpl::Factory::Create(
    Context* context, SharingPlatform& sharing_platform,
    NearbyShareLocalDeviceDataManager* local_device_data_manager,
    const FilePath& profile_path,
    nearby::sharing::api::CertTransportClient* absl_nonnull
        cert_transport_client) {
  DCHECK(context);

  if (test_factory_) {
    return test_factory_->CreateInstance(context, local_device_data_manager,
                                         profile_path, cert_transport_client);
  }

  FilePath database_path = profile_path;
  database_path.append(FilePath(kPublicCertificateDatabaseName));
  return absl::WrapUnique(new NearbyShareCertificateManagerImpl(
      context, sharing_platform.GetPreferenceManager(),
      sharing_platform.GetAccountManager(),
      sharing_platform.CreatePublicCertificateDatabase(database_path),
      local_device_data_manager, cert_transport_client));
}

// static
void NearbyShareCertificateManagerImpl::Factory::SetFactoryForTesting(
    Factory* test_factory) {
  test_factory_ = test_factory;
}

NearbyShareCertificateManagerImpl::Factory::~Factory() = default;

NearbyShareCertificateManagerImpl::NearbyShareCertificateManagerImpl(
    Context* context, PreferenceManager& preference_manager,
    AccountManager& account_manager,
    std::unique_ptr<PublicCertificateDatabase> public_certificate_database,
    NearbyShareLocalDeviceDataManager* local_device_data_manager,
    nearby::sharing::api::CertTransportClient* absl_nonnull
        cert_transport_client)
    : context_(context),
      account_manager_(account_manager),
      local_device_data_manager_(local_device_data_manager),
      preference_manager_(preference_manager),
      cert_transport_client_(cert_transport_client),
      certificate_storage_(NearbyShareCertificateStorageImpl::Factory::Create(
          preference_manager, std::move(public_certificate_database))),
      private_certificate_expiration_scheduler_(
          NearbyShareSchedulerFactory::CreateExpirationScheduler(
              context, preference_manager,
              [&] { return NextPrivateCertificateExpirationTime(); },
              /*retry_failures=*/true,
              /*require_connectivity=*/false,
              PrefNames::kSchedulerPrivateCertificateExpiration,
              [this]() {
                VLOG(1)
                    << "Private certificate expiration scheduler is called.";
                executor_->PostTask([this]() {
                  private_certificate_expiration_scheduler_->HandleResult(
                      RefreshPrivateCertificatesInExecutor(
                          /*force_upload=*/false));
                });
              })),
      public_certificate_expiration_scheduler_(
          NearbyShareSchedulerFactory::CreateExpirationScheduler(
              context, preference_manager,
              [&] { return NextPublicCertificateExpirationTime(); },
              /*retry_failures=*/true,
              /*require_connectivity=*/false,
              PrefNames::kSchedulerPublicCertificateExpiration,
              [this]() {
                VLOG(1)
                    << ": Public certificate expiration scheduler is called.";
                executor_->PostTask([this]() {
                  public_certificate_expiration_scheduler_->HandleResult(
                      RemoveExpiredPublicCertificatesInExecutor());
                });
              })),
      force_contacts_update_scheduler_(
          NearbyShareSchedulerFactory::CreatePeriodicScheduler(
              context, preference_manager,
              kNearbyShareLocalCertificateUploadPeriod,
              /*retry_failures=*/true,
              /*require_connectivity=*/true,
              PrefNames::kSchedulerUploadLocalDeviceCertificates,
              [this]() {
                VLOG(1)
                    << "Upload local device certificates scheduler is called.";
                executor_->PostTask([this]() {
                  force_contacts_update_scheduler_->HandleResult(
                      UploadDeviceCertificatesInExecutor(
                          certificate_storage_->GetPrivateCertificates(),
                          /*force_update_contacts=*/true));
                });
              })),
      download_public_certificates_scheduler_(
          NearbyShareSchedulerFactory::CreatePeriodicScheduler(
              context, preference_manager,
              kNearbySharePublicCertificateDownloadPeriod,
              /*retry_failures=*/true,
              /*require_connectivity=*/true,
              PrefNames::kSchedulerDownloadPublicCertificates,
              [this]() {
                LOG(INFO)
                    << "Download public certificates scheduler is called.";
                executor_->PostTask([this]() {
                  download_public_certificates_scheduler_->HandleResult(
                      DownloadPublicCertificatesInExecutor());
                });
              })),
      executor_(context->CreateSequencedTaskRunner()) {
  local_device_data_manager_->AddObserver(this);
}

NearbyShareCertificateManagerImpl::~NearbyShareCertificateManagerImpl() {
  local_device_data_manager_->RemoveObserver(this);
}

std::string NearbyShareCertificateManagerImpl::GetId() {
  std::string id = preference_manager_.GetString(PrefNames::kDeviceId, "");
  if (id.empty()) {
    // Linux has no server-assigned device id, so publish/download gate on an
    // empty id. Mint a stable random one (a positive int64, matching the
    // devices/{id} name the backend expects) and persist it.
    std::random_device rd;
    uint64_t v = ((static_cast<uint64_t>(rd()) << 32) ^ rd()) &
                 0x7fffffffffffffffULL;
    if (v == 0) v = 1;
    id = std::to_string(v);
    preference_manager_.SetString(PrefNames::kDeviceId, id);
  }
  return id;
}

void NearbyShareCertificateManagerImpl::CertificateDownloadContext::
    FetchCertificates() {
  LOG(INFO) << __func__ << ": Downloading public certificates";
  cert_transport_client_->DownloadCertificates(
      device_id_, api::CertTransportClient::kTimeout,
      [this](const absl::StatusOr<std::vector<std::string>>& resp) mutable {
        if (!resp.ok()) {
          LOG(WARNING) << "Failed to download public certificates: "
                       << resp.status();
          std::move(download_callback_)(resp.status());
          return;
        }
        for (const std::string& blob : *resp) {
          PublicCertificate certificate;
          if (!certificate.ParseFromString(blob)) {
            LOG(ERROR) << "Failed parsing to PublicCertificate, blob: "
                       << absl::BytesToHexString(blob);
            continue;
          }
          certificates_.push_back(certificate);
        }
        LOG(INFO) << "Completed download of " << certificates_.size()
                  << " certificates";
        std::move(download_callback_)(std::move(certificates_));
      });
}

bool NearbyShareCertificateManagerImpl::UpdatePublicCertificates(
    const std::vector<PublicCertificate>& certificates) {
  // Save certificates to store.
  absl::Notification notification;
  bool is_added_to_store = false;
  certificate_storage_->AddPublicCertificates(
      absl::MakeSpan(certificates.data(), certificates.size()),
      [&](bool success) {
        is_added_to_store = success;
        notification.Notify();
      });
  notification.WaitForNotification();
  if (!is_added_to_store) {
    LOG(ERROR) << "Failed to add certificates to store.";
    return false;
  }

  // Succeeded to download public certificates.
  NotifyPublicCertificatesDownloaded();

  // Recompute the expiration timer to account for new certificates.
  public_certificate_expiration_scheduler_->Reschedule();
  return true;
}

void NearbyShareCertificateManagerImpl::DownloadPublicCertificates() {
  download_public_certificates_scheduler_->MakeImmediateRequest();
}

bool NearbyShareCertificateManagerImpl::DownloadPublicCertificatesInExecutor() {
  LOG(INFO) << "Start to download public certificates in executor.";
  if (!is_running()) {
    LOG(WARNING) << "Ignore certificates download, manager is not running.";
    return false;
  }

  std::string device_id = GetId();
  if (device_id.empty() || !account_manager_.GetCurrentAccount().has_value()) {
    LOG(WARNING) << "Ignore certificates download, no logged in account.";
    // Return true to prevent retry.
    return true;
  }

  // Clear join_time if it is expired.
  if (join_time_.has_value() &&
      context_->GetClock()->Now() > join_time_discard_time_) {
    join_time_.reset();
  }
  bool download_succeeded = false;
  absl::Notification notification;
  auto context = std::make_unique<CertificateDownloadContext>(
      cert_transport_client_, std::move(device_id),
      [this, &download_succeeded, &notification](
          absl::StatusOr<std::vector<PublicCertificate>> certificates_status) {
        if (!certificates_status.ok()) {
          download_succeeded = false;
          LOG(WARNING) << "Failed to download public certificates: "
                       << certificates_status.status();
        } else {
          auto certificates = certificates_status.value();
          if (VLOG_IS_ON(1)) {
            for (const auto& certificate : certificates) {
              VLOG(1) << "Downloaded certificate id: "
                      << absl::BytesToHexString(certificate.secret_id());
            }
          }
          download_succeeded = UpdatePublicCertificates(certificates);
        }
        notification.Notify();
      });
  context->FetchCertificates();
  // Wait for all pages of certificates to be downloaded.
  // MUST not terminate early, otherwise notification will go out of scope, and
  // the callback will call Notify on a destroyed object.
  notification.WaitForNotification();
  LOG(INFO) << "Public certificates downloadws, success: "
            << download_succeeded;
  return download_succeeded;
}

void NearbyShareCertificateManagerImpl::RegeneratePrivateCertificates() {
  executor_->PostTask([this]() {
    LOG(INFO) << "Regenerating private certificates in executor.";
    certificate_storage_->ClearPrivateCertificates();
    private_certificate_expiration_scheduler_->HandleResult(
        RefreshPrivateCertificatesInExecutor(/*force_upload=*/false));
  });
}

bool NearbyShareCertificateManagerImpl::UploadDeviceCertificatesInExecutor(
    const std::vector<NearbySharePrivateCertificate>& private_certs,
    bool force_update_contacts) {
  // The plugin's UpdateDevice covers both visibilities via each cert's
  // internal for_self_share, so there is no separate "force contacts update"
  // request to shape here; the flag only controls scheduling upstream.
  static_cast<void>(force_update_contacts);
  LOG(INFO) << "Start to upload local device certificates in executor.";
  if (private_certs.empty()) {
    LOG(WARNING) << "Ignore local device certificates upload, no private "
                    "certificates found.";
    return false;
  }

  if (!is_running()) {
    LOG(WARNING)
        << "Ignore local device certificates upload, manager is not running.";
    return false;
  }
  std::string device_id = GetId();
  if (device_id.empty() || !account_manager_.GetCurrentAccount().has_value()) {
    LOG(WARNING)
        << "Ignore local device certificates upload, no logged in account.";
    // Return true to prevent retry.
    return true;
  }

  std::vector<std::string> blobs;
  blobs.reserve(private_certs.size());
  for (const NearbySharePrivateCertificate& private_cert : private_certs) {
    std::optional<PublicCertificate> public_cert =
        private_cert.ToPublicCertificate();
    if (!public_cert.has_value()) {
      LOG(WARNING) << "Failed to convert private certificate to public "
                      "certificate.";
      continue;
    }
    VLOG(1) << "Uploading public certificate id: "
            << absl::BytesToHexString(public_cert->secret_id()) << "for "
            << (public_cert->for_self_share() ? "self" : "contact");
    blobs.push_back(public_cert->SerializeAsString());
  }

  bool upload_certificates_succeeded = false;
  absl::Notification notification;
  cert_transport_client_->UploadCertificates(
      device_id, std::move(blobs), api::CertTransportClient::kTimeout,
      [&upload_certificates_succeeded,
       &notification](const absl::Status& status) {
        upload_certificates_succeeded = status.ok();
        if (!status.ok()) {
          LOG(WARNING) << __func__
                       << ": UploadCertificates failed: " << status;
        } else {
          LOG(INFO) << __func__ << ": UploadCertificates succeeded";
        }
        notification.Notify();
      });
  // MUST not terminate early, otherwise notification will go out of scope, and
  // the callback will call Notify on a destroyed object.
  notification.WaitForNotification();
  // check whether the manager is still running
  if (!is_running()) {
    LOG(WARNING) << __func__ << ": manager is stopped after call.";
    return false;
  }
  return upload_certificates_succeeded;
}

void NearbyShareCertificateManagerImpl::GetDecryptedPublicCertificate(
    NearbyShareEncryptedMetadataKey encrypted_metadata_key,
    CertDecryptedCallback callback) {
  certificate_storage_->GetPublicCertificates(
      [encrypted_metadata_key = std::move(encrypted_metadata_key),
       callback = std::move(callback)](
          bool success,
          std::unique_ptr<std::vector<PublicCertificate>> result) {
        TryDecryptPublicCertificates(encrypted_metadata_key,
                                     std::move(callback), success,
                                     std::move(result));
      });
}

void NearbyShareCertificateManagerImpl::ClearPublicCertificates(
    std::function<void(bool)> callback) {
  certificate_storage_->ClearPublicCertificates(std::move(callback));
}

void NearbyShareCertificateManagerImpl::OnStartScheduledTasks() {
  private_certificate_expiration_scheduler_->Start();
  public_certificate_expiration_scheduler_->Start();
  force_contacts_update_scheduler_->Start();
  download_public_certificates_scheduler_->Start();
}

void NearbyShareCertificateManagerImpl::OnStopScheduledTasks() {
  private_certificate_expiration_scheduler_->Stop();
  public_certificate_expiration_scheduler_->Stop();
  force_contacts_update_scheduler_->Stop();
  download_public_certificates_scheduler_->Stop();
}

std::optional<NearbySharePrivateCertificate>
NearbyShareCertificateManagerImpl::GetValidPrivateCertificate(
    DeviceVisibility visibility) const {
  if (visibility == DeviceVisibility::DEVICE_VISIBILITY_UNSPECIFIED ||
      visibility == DeviceVisibility::DEVICE_VISIBILITY_HIDDEN) {
    return std::nullopt;
  }

  // If the user already signed in, setup contacts certificate for everyone
  // mode to show correct user icon on remote device.
  if (visibility == DeviceVisibility::DEVICE_VISIBILITY_EVERYONE) {
    visibility = DeviceVisibility::DEVICE_VISIBILITY_ALL_CONTACTS;
  }

  std::vector<NearbySharePrivateCertificate> certs =
      certificate_storage_->GetPrivateCertificates();
  for (auto& cert : certs) {
    if (IsNearbyShareCertificateWithinValidityPeriod(
            context_->GetClock()->Now(), cert.not_before(), cert.not_after(),
            /*use_public_certificate_tolerance=*/false) &&
        cert.visibility() == visibility) {
      return std::move(cert);
    }
  }

  LOG(WARNING) << "No valid private certificate found with visibility "
               << static_cast<int>(visibility);
  return std::nullopt;
}

void NearbyShareCertificateManagerImpl::UpdatePrivateCertificateInStorage(
    const NearbySharePrivateCertificate& private_certificate) {
  certificate_storage_->UpdatePrivateCertificate(private_certificate);
}

void NearbyShareCertificateManagerImpl::OnLocalDeviceDataChanged(
    bool did_device_name_change, bool did_full_name_change,
    bool did_icon_change) {
  LOG(INFO) << "Handle local device data changed.";
  if (!did_device_name_change && !did_full_name_change && !did_icon_change)
    return;
  // Recreate all private certificates to ensure up-to-date metadata.
  RegeneratePrivateCertificates();
}

void NearbyShareCertificateManagerImpl::SetVendorId(int32_t vendor_id) {
  LOG(INFO) << "Setting certificate vendor ID to " << vendor_id;
  vendor_id_ = vendor_id;

  auto certificate = GetValidPrivateCertificate(
      proto::DeviceVisibility::DEVICE_VISIBILITY_ALL_CONTACTS);
  auto self_certificate = GetValidPrivateCertificate(
      proto::DeviceVisibility::DEVICE_VISIBILITY_SELF_SHARE);
  if (certificate.has_value() && self_certificate.has_value()) {
    if (certificate->unencrypted_metadata().vendor_id() == vendor_id_ &&
        self_certificate->unencrypted_metadata().vendor_id() == vendor_id_) {
      LOG(INFO) << "Requested vendor ID is already set in latest valid private "
                   "certificates. Skipping certificate refresh.";
      return;
    }
  }
  // Recreate all private certificates to ensure up-to-date metadata.
  RegeneratePrivateCertificates();
}

void NearbyShareCertificateManagerImpl::SetJoinBindingTime(
    absl::Time join_binding_time, absl::Duration life_time) {
  join_time_ = join_binding_time;
  join_time_discard_time_ = context_->GetClock()->Now() + life_time;
}

std::string NearbyShareCertificateManagerImpl::Dump() const {
  std::stringstream sstream;
  sstream << "Public Certificates" << std::endl;
  std::vector<std::string> ids =
      certificate_storage_->GetPublicCertificateIds();
  sstream << "  Total count:" << ids.size() << std::endl;
  for (const auto& id : ids) {
    DumpCertificateId(sstream, id, /*is_public_cert=*/true);
  }
  sstream << std::endl;

  sstream << "Private Certificates" << std::endl;
  std::vector<NearbySharePrivateCertificate> private_certs =
      certificate_storage_->GetPrivateCertificates();
  if (private_certs.empty()) {
    sstream << "  Total count: 0" << std::endl;
  } else {
    sstream << "  Total count:" << private_certs.size() << std::endl;
    for (const auto& cert : private_certs) {
      std::string id(cert.id().begin(), cert.id().end());
      DumpCertificateId(sstream, id, /*is_public_cert=*/false);
    }
  }

  return sstream.str();
}

absl::Time
NearbyShareCertificateManagerImpl::NextPrivateCertificateExpirationTime() {
  std::optional<AccountManager::Account> account =
      account_manager_.GetCurrentAccount();
  // If the user is not logged in, there are no certs and we don't need to check
  // for expiration.
  if (!account.has_value()) {
    return absl::InfiniteFuture();
  }
  return certificate_storage_->NextPrivateCertificateExpirationTime(
      NumExpectedPrivateCertificates());
}

bool NearbyShareCertificateManagerImpl::RefreshPrivateCertificatesInExecutor(
    bool force_upload) {
  LOG(INFO) << "Refreshing private certificates in executor.";
  absl::Time now = context_->GetClock()->Now();
  certificate_storage_->RemoveExpiredPrivateCertificates(now);

  std::optional<AccountManager::Account> account =
      account_manager_.GetCurrentAccount();
  if (!account.has_value()) {
    LOG(INFO) << "Not logged in on refreshing private certificates, ignoring";
    return true;
    ;
  }

  std::vector<NearbySharePrivateCertificate> certs =
      certificate_storage_->GetPrivateCertificates();
  if (certs.size() == NumExpectedPrivateCertificates()) {
    LOG(INFO) << "All private certificates are still valid. ";
    if (force_upload) {
      LOG(INFO) << "Force upload private certificates and update contacts.";
      force_contacts_update_scheduler_->MakeImmediateRequest();
    }
    return true;
  }

  // Determine how many private certificates of each visibility need to be
  // created, and determine the validity period for the new certificates.
  absl::flat_hash_map<DeviceVisibility, size_t> num_valid_certs;
  absl::flat_hash_map<DeviceVisibility, absl::Time> latest_not_after;
  for (DeviceVisibility visibility : kVisibilities) {
    num_valid_certs[visibility] = 0;
    latest_not_after[visibility] = now;
  }
  for (const NearbySharePrivateCertificate& cert : certs) {
    ++num_valid_certs[cert.visibility()];
    latest_not_after[cert.visibility()] =
        std::max(latest_not_after[cert.visibility()], cert.not_after());
  }

  std::optional<std::string> email =
      account.has_value()
          ? account->email
          : static_cast<std::optional<std::string>>(std::nullopt);

  std::optional<std::string> icon_url =
      account.has_value()
          ? (account->picture_url.empty()
                 ? static_cast<std::optional<std::string>>(std::nullopt)
                 : account->picture_url)
          : static_cast<std::optional<std::string>>(std::nullopt);

  std::optional<std::string> full_name =
      account.has_value()
          ? (account->display_name.empty()
                 ? static_cast<std::optional<std::string>>(std::nullopt)
                 : account->display_name)
          : static_cast<std::optional<std::string>>(std::nullopt);

  std::optional<EncryptedMetadata> metadata =
      BuildMetadata(local_device_data_manager_->GetDeviceName(), full_name,
                    icon_url, email, vendor_id_, context_);

  if (!metadata.has_value()) {
    LOG(WARNING)
        << "Failed to create private certificates; cannot create metadata";
    return false;
  }

  // Add new certificates if necessary. Each visibility should have
  // kNearbyShareNumPrivateCertificates.
  LOG(INFO)
      << "Creating "
      << kNearbyShareNumPrivateCertificates -
             num_valid_certs[DeviceVisibility::DEVICE_VISIBILITY_ALL_CONTACTS]
      << " all-contacts visibility, "
      << kNearbyShareNumPrivateCertificates -
             num_valid_certs[DeviceVisibility::DEVICE_VISIBILITY_SELF_SHARE]
      << " self-share visibility, ";

  for (DeviceVisibility visibility : kVisibilities) {
    while (num_valid_certs[visibility] < kNearbyShareNumPrivateCertificates) {
      certs.emplace_back(visibility,
                         /*not_before=*/latest_not_after[visibility],
                         *metadata);
      ++num_valid_certs[visibility];
      latest_not_after[visibility] = certs.back().not_after();
    }
  }

  certificate_storage_->ReplacePrivateCertificates(
      absl::MakeSpan(certs.data(), certs.size()));
  NotifyPrivateCertificatesChanged();
  // force_contacts_update_scheduler_ should only be updated if
  // force_update_contacts is true, otherwise we could miss the 24hr contact
  // update window.
  if (force_upload) {
    force_contacts_update_scheduler_->MakeImmediateRequest();
  } else {
    executor_->PostTask([this]() {
      VLOG(1) << "Begin UploadDeviceCertificatesInExecutor";
      UploadDeviceCertificatesInExecutor(
          certificate_storage_->GetPrivateCertificates(),
          /*force_update_contacts=*/false);
    });
  }
  return true;
}

void NearbyShareCertificateManagerImpl::ForceUploadPrivateCertificates() {
  executor_->PostTask([this]() {
    private_certificate_expiration_scheduler_->HandleResult(
        RefreshPrivateCertificatesInExecutor(/*force_upload=*/true));
  });
}

absl::Time
NearbyShareCertificateManagerImpl::NextPublicCertificateExpirationTime() {
  absl::Time next_expiration_time =
      certificate_storage_->NextPublicCertificateExpirationTime();

  // To account for clock skew between devices, we accept public certificates
  // that are slightly past their validity period. This conforms with the
  // GmsCore implementation.
  return next_expiration_time +
         kNearbySharePublicCertificateValidityBoundOffsetTolerance;
}

bool NearbyShareCertificateManagerImpl::
    RemoveExpiredPublicCertificatesInExecutor() {
  LOG(INFO) << "Removing expired public certificates in executor.";
  absl::Notification notification;
  bool result = false;
  certificate_storage_->RemoveExpiredPublicCertificates(
      context_->GetClock()->Now(), [&](bool success) {
        result = success;
        notification.Notify();
      });
  notification.WaitForNotification();
  if (!result) {
    LOG(ERROR) << "Failed to remove expired public certificates.";
  }
  return result;
}

}  // namespace nearby::sharing
