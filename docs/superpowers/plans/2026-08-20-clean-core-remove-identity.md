# Clean-Core: Remove RE-derived identity.v1 Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete the reverse-engineered `google.nearby.identity.v1` layer from core and have the certificate manager publish/download raw `PublicCertificate` blobs through the clean plugin's existing `UploadCertificates`/`DownloadCertificates` D-Bus contract.

**Architecture:** Core's cert manager currently wraps each `PublicCertificate` in an identity.v1 `SharedCredential`/`PerVisibilitySharedCredentials` envelope and calls `PublishDevice`/`QuerySharedCredentials` over D-Bus (`DBusIdentityRpcClient`), using a hand-rolled wire codec built from field numbers reverse-engineered from the Windows binary. We replace the `IdentityRpcClient` abstraction with a minimal `CertTransportClient` (two methods, both trading serialized `nearby::sharing::proto::PublicCertificate` blobs), point its D-Bus implementation at the plugin methods that already exist (`UploadCertificates(s,aay)->b`, `DownloadCertificates(s)->aay`), and delete the identity.v1 protos + wire codec + types. The plugin's Google-facing transport (Chromium BSD REST) is unchanged. Core stops carrying any RE surface.

**Tech Stack:** C++20, Bazel (engine lib incl. cert manager), sdbus-c++ (core↔plugin session D-Bus), CMake+Qt6 (the plugin and the tray app), protobuf (`nearby::sharing::proto::PublicCertificate` from Apache `sharing/proto/rpc_resources.proto`).

**Spec:** This file's header + the investigation recorded in memory `project-scotty-clean-transport-plugin`. Design decided with the user 2026-08-20: option 2 (Chromium-REST transport) + remove identity RE from core.

## Global Constraints

- **Core links no RE identity code.** After this plan, `grep -rn "identity::v1\|identity_rpc_wire\|identity_rpc_types" sharing/ location/ google/` returns nothing in shipped targets.
- **Plugin is untouched.** `~/Desktop/scotty-mydevices-clean` already exposes `UploadCertificates`/`DownloadCertificates` at `dev.scotty.MyDevices1` / `/dev/scotty/MyDevices1`. Do not modify it in this plan.
- **Wire payloads are serialized `nearby::sharing::proto::PublicCertificate`** on both directions — the exact bytes core already produces via `PublicCertificate::SerializeAsString()` and consumes via `PublicCertificate::ParseFromString()`.
- **D-Bus signatures the plugin expects:** `UploadCertificates(s device_id, aay certificates) -> b success`; `DownloadCertificates(s device_id) -> aay certificates`. Object `/dev/scotty/MyDevices1`, interface/bus name `dev.scotty.MyDevices1`.
- **`device_id`** is the same value core already uses: `GetId()` (was formatted as `absl::StrCat("devices/", device_id)` for the RE `Device.name`; the plugin wants the bare id, no `devices/` prefix).
- **Acceptance is runtime, not compile.** The prior attempt (`feature/clean-certificate-transport`) compiled around the layer but broke send at runtime. The final task is a live send to the user's phone; nothing is "done" until that passes.
- **Work branch:** `clean-core-remove-identity` (already created off known-good `scotty`).

---

### Task 1: Green baseline + pin the active RPC path

**Files:**
- Read only: `sharing/certificates/BUILD`, `location/nearby/sharing/lib/rpc/BUILD`, `debian/rules`, top-level build script(s).

**Interfaces:**
- Produces: a one-line note (added to this plan's bottom "Findings" section) naming the exact build command that produces the shipped binary and confirming that the cert manager links `location/nearby/sharing/lib/rpc:sharing_rpc_client` (real `DBusIdentityRpcClient`), NOT `sharing/linux/stubs` (NoOp).

- [ ] **Step 1: Find the build command.** Inspect `debian/rules`, any `build*.sh`, and `MODULE.bazel`. The engine lib is Bazel. Record the exact target that builds the cert manager + the app binary (expected: a `bazel build //sharing/...` target).
- [ ] **Step 2: Build known-good `scotty` HEAD unchanged.** Run the command from Step 1. Expected: PASS (this is the last-known-good tree).
- [ ] **Step 3: Confirm the active rpc dep.** `grep -n rpc sharing/certificates/BUILD` → confirms `//location/nearby/sharing/lib/rpc:sharing_rpc_client`. Confirm the app target transitively links the real factory (`DBusIdentityRpcClient`), not the stub. Note it in Findings.
- [ ] **Step 4: Commit the plan + Findings note.**

```bash
git add docs/superpowers/plans/2026-08-20-clean-core-remove-identity.md
git commit -m "docs: clean-core identity-removal plan + build baseline findings"
```

---

### Task 2: Introduce `CertTransportClient` interface (replace `IdentityRpcClient`)

**Files:**
- Modify: `location/nearby/sharing/lib/rpc/sharing_rpc_client.h` (replace `class IdentityRpcClient`)
- Modify: `sharing/linux/stubs/sharing_rpc_client.h` (mirror, NoOp path)
- Test: `sharing/certificates/nearby_share_certificate_manager_impl_test.cc` (fake client — updated in Task 6; here just make it compile against the new iface via the fake in Task 5)

**Interfaces:**
- Produces:
```cpp
namespace nearby::sharing::api {
class CertTransportClient {
 public:
  // Upload serialized nearby::sharing::proto::PublicCertificate blobs for this
  // device. success=false on any transport/auth failure.
  using UploadCallback = std::function<void(const absl::Status&)>;
  // Download serialized PublicCertificate blobs shared with this device.
  using DownloadCallback =
      std::function<void(const absl::StatusOr<std::vector<std::string>>&)>;

  virtual ~CertTransportClient() = default;
  virtual void UploadCertificates(std::string device_id,
                                  std::vector<std::string> certificates,
                                  absl::Duration timeout,
                                  UploadCallback callback) = 0;
  virtual void DownloadCertificates(std::string device_id,
                                    absl::Duration timeout,
                                    DownloadCallback callback) = 0;
  static constexpr absl::Duration kTimeout = absl::Seconds(30);
};
}  // namespace nearby::sharing::api
```

- [ ] **Step 1: Replace `IdentityRpcClient` in `location/.../sharing_rpc_client.h`** with `CertTransportClient` above. Remove the `#include` of identity types and the `SharingRpcClient::ListContactPeople` only if unused (leave `SharingRpcClient` as-is; only the identity client changes).
- [ ] **Step 2: Mirror the same class into `sharing/linux/stubs/sharing_rpc_client.h`**, dropping its identity includes.
- [ ] **Step 3: Build the header's reverse-deps to surface every call site.** Expected: FAIL at `nearby_share_certificate_manager_impl.cc` (still calls `PublishDevice`) — that's the next tasks. Record the failing call sites.
- [ ] **Step 4: Commit** (compile-broken WIP is fine on this branch; note in message).

```bash
git add location/nearby/sharing/lib/rpc/sharing_rpc_client.h sharing/linux/stubs/sharing_rpc_client.h
git commit -m "refactor(core): replace IdentityRpcClient with CertTransportClient iface"
```

---

### Task 3: Point the D-Bus client at the plugin's Upload/Download methods

**Files:**
- Modify: `location/nearby/sharing/lib/rpc/grpc_async_client_factory.h` (rewrite `DBusIdentityRpcClient` → `DBusCertTransportClient`; keep `GrpcAsyncClientFactory::CreateIdentityInstance` returning the new type, renamed `CreateCertTransportInstance`)
- Modify: `sharing/linux/stubs/grpc_async_client_factory.h` (NoOp mirror → `NoOpCertTransportClient`)

**Interfaces:**
- Consumes: `CertTransportClient` (Task 2).
- Produces: `GrpcAsyncClientFactory::CreateCertTransportInstance() -> std::unique_ptr<CertTransportClient>`.

- [ ] **Step 1: Rewrite the D-Bus proxy calls.** Replace the `Call(method, ay, &ay)` proto path with two typed calls:

```cpp
void UploadCertificates(std::string device_id,
                        std::vector<std::string> certificates,
                        absl::Duration, UploadCallback callback) override {
  if (connection_ == nullptr) { callback(absl::UnavailableError("no session bus")); return; }
  try {
    auto proxy = sdbus::createProxy(*connection_,
        sdbus::ServiceName{"dev.scotty.MyDevices1"},
        sdbus::ObjectPath{"/dev/scotty/MyDevices1"});
    std::vector<std::vector<uint8_t>> certs;
    certs.reserve(certificates.size());
    for (auto& c : certificates) certs.emplace_back(c.begin(), c.end());
    bool ok = false;
    proxy->callMethod("UploadCertificates").onInterface("dev.scotty.MyDevices1")
        .withArguments(device_id, certs).storeResultsTo(ok);
    callback(ok ? absl::OkStatus() : absl::UnavailableError("plugin rejected upload"));
  } catch (const sdbus::Error& e) {
    callback(absl::UnavailableError(e.getName() + ": " + e.getMessage()));
  }
}
```

```cpp
void DownloadCertificates(std::string device_id, absl::Duration,
                          DownloadCallback callback) override {
  if (connection_ == nullptr) { callback(absl::UnavailableError("no session bus")); return; }
  try {
    auto proxy = sdbus::createProxy(*connection_,
        sdbus::ServiceName{"dev.scotty.MyDevices1"},
        sdbus::ObjectPath{"/dev/scotty/MyDevices1"});
    std::vector<std::vector<uint8_t>> out;
    proxy->callMethod("DownloadCertificates").onInterface("dev.scotty.MyDevices1")
        .withArguments(device_id).storeResultsTo(out);
    std::vector<std::string> certs;
    certs.reserve(out.size());
    for (auto& c : out) certs.emplace_back(c.begin(), c.end());
    callback(certs);
  } catch (const sdbus::Error& e) {
    callback(absl::UnavailableError(e.getName() + ": " + e.getMessage()));
  }
}
```

- [ ] **Step 2: Delete** the old `Call()` helper, the `wire::` includes, and `GetAccountInfo`/`QuerySharedCredentials*`/`PublishDevice` methods.
- [ ] **Step 3: Mirror NoOp** in the stubs factory (both methods invoke `callback` with `absl::UnavailableError("no plugin")` / empty).
- [ ] **Step 4: Rename factory method** `CreateIdentityInstance`→`CreateCertTransportInstance` here and at its one caller (surfaced in Task 4).
- [ ] **Step 5: Commit.**

```bash
git add location/nearby/sharing/lib/rpc/grpc_async_client_factory.h sharing/linux/stubs/grpc_async_client_factory.h
git commit -m "refactor(core): DBusCertTransportClient calls plugin Upload/DownloadCertificates"
```

---

### Task 4: Rework cert-manager UPLOAD path

**Files:**
- Modify: `sharing/certificates/nearby_share_certificate_manager_impl.cc` (`AddCertifactesToPublishDeviceRequest` + `UploadDeviceCertificatesInExecutor` region, ~lines 542–670)
- Modify: `sharing/certificates/nearby_share_certificate_manager_impl.h` (member type `nearby_identity_client_` → `cert_transport_client_`)

**Interfaces:**
- Consumes: `CertTransportClient::UploadCertificates` (Task 2/3).

- [ ] **Step 1: Replace envelope construction** with a flat blob list. Delete `AddCertifactesToPublishDeviceRequest`; inline:

```cpp
std::vector<std::string> blobs;
for (const NearbySharePrivateCertificate& pc : private_certs) {
  std::optional<PublicCertificate> pub = pc.ToPublicCertificate();
  if (!pub.has_value()) { LOG(WARNING) << "skip: to-public failed"; continue; }
  blobs.push_back(pub->SerializeAsString());
}
```

- [ ] **Step 2: Replace `PublishDevice(...)` call** with:

```cpp
cert_transport_client_->UploadCertificates(
    GetId(), std::move(blobs), api::CertTransportClient::kTimeout,
    [this, &notification, &ok](const absl::Status& s) {
      ok = s.ok();
      if (!ok) LOG(ERROR) << "UploadCertificates failed: " << s;
      else LOG(INFO) << "UploadCertificates succeeded";
      notification.Notify();
    });
```
(Drop the `contact_updates`/`contact_removed` second-call logic — the plugin's UpdateDevice covers both visibilities via each cert's internal `for_self_share`. Note removal in Findings.)

- [ ] **Step 3: Build.** Expected: PASS for the upload TU (download still references identity — fixed in Task 5). If download blocks the build, do Tasks 4–5 as one commit.
- [ ] **Step 4: Commit.**

```bash
git add sharing/certificates/nearby_share_certificate_manager_impl.*
git commit -m "refactor(certs): upload raw PublicCertificate blobs via CertTransportClient"
```

---

### Task 5: Rework cert-manager DOWNLOAD path + fakes

**Files:**
- Modify: `sharing/certificates/nearby_share_certificate_manager_impl.cc` (`CertificateDownloadContext`, ~lines 345–470)
- Modify: `sharing/certificates/fake_nearby_share_certificate_manager.{h,cc}`

**Interfaces:**
- Consumes: `CertTransportClient::DownloadCertificates` (Task 2/3).

- [ ] **Step 1: Collapse `QuerySharedCredentialsFetchNextPage` + `...WithBindingIdsFetchNextPage`** into one non-paged download:

```cpp
void CertificateDownloadContext::FetchCertificates() {
  cert_transport_client_->DownloadCertificates(
      device_id_, api::CertTransportClient::kTimeout,
      [this](const absl::StatusOr<std::vector<std::string>>& resp) mutable {
        if (!resp.ok()) { std::move(download_callback_)(resp.status()); return; }
        for (const std::string& blob : *resp) {
          PublicCertificate cert;
          if (!cert.ParseFromString(blob)) { LOG(ERROR) << "bad cert blob"; continue; }
          certificates_.push_back(cert);
        }
        LOG(INFO) << "Downloaded " << certificates_.size() << " certificates";
        std::move(download_callback_)(std::move(certificates_));
      });
}
```

- [ ] **Step 2: Remove** `next_page_token_`, `page_number_`, `join_time_`-binding members that are now unused; update the single call site that kicked off `QuerySharedCredentials*FetchNextPage()` to call `FetchCertificates()`.
- [ ] **Step 3: Update the fake** to store a `std::function` matching `CertTransportClient` so `_test.cc` can inject canned blobs.
- [ ] **Step 4: Build.** Expected: PASS for the whole cert-manager library.
- [ ] **Step 5: Commit.**

```bash
git add sharing/certificates/nearby_share_certificate_manager_impl.* sharing/certificates/fake_nearby_share_certificate_manager.*
git commit -m "refactor(certs): download PublicCertificate blobs via CertTransportClient"
```

---

### Task 6: Delete the RE layer + fix build graph

**Files:**
- Delete: `location/nearby/sharing/lib/rpc/identity_rpc_wire.h`, `location/nearby/sharing/lib/rpc/identity_rpc_types.h`
- Delete: `sharing/linux/stubs/identity_rpc_types.h`
- Delete: `google/nearby/identity/v1/` (`resources.pb.h`, `rpcs.pb.h`, `BUILD`)
- Modify: `sharing/certificates/BUILD` (drop `//google/nearby/identity/v1:*_cc_proto` deps), `location/nearby/sharing/lib/rpc/BUILD`, any `BUILD` referencing identity
- Modify: `sharing/nearby_sharing_service_impl.{cc,h}`, `sharing/nearby_sharing_service_factory.h`, `sharing/fake_nearby_sharing_service.h`, `location/nearby/sharing/lib/sync/sync_manager.h` — drop identity includes/uses

- [ ] **Step 1: Grep for every remaining reference.** `grep -rln "identity::v1\|identity_rpc_wire\|identity_rpc_types\|google/nearby/identity" sharing/ location/ google/`. Each hit is a fix in this task.
- [ ] **Step 2: Delete files + BUILD deps** listed above.
- [ ] **Step 3: Fix the stragglers** (service impl/factory/sync_manager) to use `CertTransportClient`/`CreateCertTransportInstance`.
- [ ] **Step 4: Full build.** Expected: PASS with zero identity references. Re-run the Step-1 grep → empty.
- [ ] **Step 5: Commit.**

```bash
git rm location/nearby/sharing/lib/rpc/identity_rpc_wire.h location/nearby/sharing/lib/rpc/identity_rpc_types.h sharing/linux/stubs/identity_rpc_types.h
git rm -r google/nearby/identity/v1
git add -A
git commit -m "refactor(core): delete reverse-engineered identity.v1 layer"
```

---

### Task 7: Update tests to the new contract

**Files:**
- Modify: `sharing/certificates/nearby_share_certificate_manager_impl_test.cc`
- Modify: `sharing/nearby_sharing_service_impl_test.cc`

- [ ] **Step 1: Rewrite upload test** — inject the fake `CertTransportClient`, trigger upload, assert the fake received the expected number of serialized `PublicCertificate` blobs whose `ParseFromString` round-trips and whose `for_self_share` matches the seeded private certs.
- [ ] **Step 2: Rewrite download test** — feed the fake a canned list of serialized `PublicCertificate` blobs, assert they land in the store via `UpdatePublicCertificates` and `NotifyPublicCertificatesDownloaded` fires once.
- [ ] **Step 3: Delete** any test asserting identity.v1 envelope shape (`per_visibility_shared_credentials`, `SharedCredential.data_type`).
- [ ] **Step 4: Run the cert-manager + service tests.** Expected: PASS.
- [ ] **Step 5: Commit.**

```bash
git add sharing/certificates/nearby_share_certificate_manager_impl_test.cc sharing/nearby_sharing_service_impl_test.cc
git commit -m "test(certs): cover CertTransportClient upload/download contract"
```

---

### Task 8: Live E2E acceptance gate (needs the user's phone)

**Files:** none (runtime verification).

- [ ] **Step 1: Build + install** the reworked core and ensure the clean plugin (`scotty-mydevices-clean`, signed in) is running on the session bus.
- [ ] **Step 2: `busctl --user introspect dev.scotty.MyDevices1 /dev/scotty/MyDevices1`** → confirm `UploadCertificates`/`DownloadCertificates` present and core reaches them (watch plugin logs for an `UpdateDevice` PATCH on publish).
- [ ] **Step 3: Trigger a cert publish** (sign-in/startup) → confirm plugin logs a 2xx from `nearbysharing-pa.googleapis.com`; core logs `UploadCertificates succeeded`.
- [ ] **Step 4: Send a file laptop→own phone.** Expected: phone auto-accepts (laptop recognized) — the original broken symptom now works.
- [ ] **Step 5: Update memory** `project-scotty-clean-transport-plugin` with the result, then `finishing-a-development-branch` to decide the merge to `scotty`.

---

## Findings (filled during execution)

- Build command: _TBD Task 1_
- Active rpc factory on shipped binary: _TBD Task 1_
- Behaviors intentionally dropped: identity `contact_updates` re-publish loop (Task 4); `QuerySharedCredentialsWithBindingIds` join-time binding (Task 5) — revisit if contacts-binding regresses.
