#include "file_share_tray_controller.h"

#include "app_paths.h"

#include <chrono>
#include <future>
#include <iostream>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QSharedPointer>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "string_utils.h"
#include "status_mapper.h"
#include "qr_code_generator.h"

FileShareTrayController::FileShareTrayController(QObject* parent)
    : QObject(parent) {
  const QString host = QSysInfo::machineHostName().trimmed();
  if (!host.isEmpty()) {
    state_.SetDeviceName(host);
  }

  loadSettings();
  setupMyDevicesBus();
  refreshAutostartFile();
  initializeService();

  // Sweep finished transfer rows a while after they end so the list settles
  // instead of accumulating completed entries — long enough to read the result
  // and open the received file. Matches kSendReturnToReceiveMs so a successful
  // send's "Sent ✓" row stays put until the sheet returns to receive on its own,
  // rather than fading back to a tappable target first.
  constexpr qlonglong kFinishedTtlMs = kSendReturnToReceiveMs;
  transfer_sweep_timer_ = new QTimer(this);
  transfer_sweep_timer_->setInterval(1000);
  connect(transfer_sweep_timer_, &QTimer::timeout, this, [this] {
    const qlonglong now = QDateTime::currentMSecsSinceEpoch();
    if (state_.SweepExpiredTransfers(now, kFinishedTtlMs)) {
      emit transfersChanged();
    }
    if (!state_.HasActiveTransfers() && state_.transfers().isEmpty()) {
      transfer_sweep_timer_->stop();
    }
  });

  // Returns the send sheet to the receive home a while after a send finishes.
  send_return_timer_ = new QTimer(this);
  send_return_timer_->setSingleShot(true);
  send_return_timer_->setInterval(kSendReturnToReceiveMs);
  connect(send_return_timer_, &QTimer::timeout, this, [this] {
    // Only if nothing new is going on: still idle, still in send mode. A new
    // transfer or a manual switch in the meantime cancels the auto-return.
    if (state_.running() && !state_.HasActiveTransfers() &&
        state_.mode() == QStringLiteral("Send")) {
      clearTransfers();
      switchToReceiveMode();
    }
  });

  // Discovery self-heal. 10 s cadence: long enough not to thrash a device that
  // takes an extra second to broadcast, or a peer flipping Contacts→Everyone,
  // short enough to feel live. The tick itself decides whether to act.
  discovery_watchdog_timer_ = new QTimer(this);
  discovery_watchdog_timer_->setInterval(10000);
  connect(discovery_watchdog_timer_, &QTimer::timeout, this,
          [this] { onDiscoveryWatchdogTick(); });

  readvertise_timer_ = new QTimer(this);
  readvertise_timer_->setInterval(kReadvertiseIntervalMs);
  connect(readvertise_timer_, &QTimer::timeout, this,
          [this] { onReadvertiseTick(); });

  // Collapse the stream of row updates into a single edge whenever a transfer
  // starts or the last active one ends. One place catches every transfersChanged
  // emit site, so callers never have to remember to fire this too.
  connect(this, &FileShareTrayController::transfersChanged, this, [this]() {
    const bool active = state_.HasActiveTransfers();
    if (active != last_transfer_active_) {
      last_transfer_active_ = active;
      emit transferActiveChanged();
    }
  });
}

FileShareTrayController::~FileShareTrayController() {
  stop();
  shutdownServiceBlocking();
}

void FileShareTrayController::shutdownServiceBlocking() {
  if (!service_) {
    return;
  }
  // Shutdown() is asynchronous: it posts a task to the service thread that still
  // dereferences the platform/context (e.g. StopFastInitiationAdvertising ->
  // GetFastInitiationManager). If service_ (and the platform it owns) destructs
  // before that task runs, the task makes a pure-virtual call on the
  // half-destroyed platform and aborts. Block until the shutdown callback fires
  // (bounded) so teardown is ordered.
  std::promise<void> shutdown_done;
  std::future<void> done = shutdown_done.get_future();
  service_->Shutdown([&shutdown_done](NearbySharingApi::StatusCode) {
    shutdown_done.set_value();
  });
  done.wait_for(std::chrono::seconds(5));
}

void FileShareTrayController::rebuildService() {
  // Ordered teardown before rebuilding: stop the surfaces, wait for the engine
  // to finish shutting down, THEN replace it (initializeService destroys the old
  // service). Without the blocking wait this races a pending shutdown task and
  // aborts — see shutdownServiceBlocking().
  if (state_.running()) {
    stop();
  }
  shutdownServiceBlocking();
  initializeService();
  start();
}

void FileShareTrayController::initializeService() {
  service_ = std::make_unique<NearbySharingApi>(state_.deviceName().toStdString());
  service_->Set5GhzHotspotEnabled(state_.enable5GhzHotspot());
  service_->SetHotspotBoostEnabled(state_.hotspotBoost());
  service_->SetVisibility(visibility_);
  if (!state_.savePath().isEmpty()) {
    service_->SetSavePath(state_.savePath().toStdString());
  }
  state_.SetQrCodeData(QString::fromStdString(service_->GetQrCodeUrl()), {}, 0);
  updateQrCodeData();
  emit qrCodeUrlChanged();
  emit qrCodeChanged();
  attachServiceListeners();
  // service_ ->StartFastInitiationScanning([](auto a)
  // {
  //   std::cout << "Probably fine";
  // });
}

void FileShareTrayController::updateQrCodeData() {
  const auto qr_data = QrCodeGenerator::GenerateQrCode(state_.qrCodeUrl());
  state_.SetQrCodeData(state_.qrCodeUrl(), qr_data.rows, qr_data.size);
  emit qrCodeChanged();
}

void FileShareTrayController::showQrCode() {
  // Mint a fresh session so the code we reveal is new; a peer is auto-authorized
  // purely by scanning it, so an old/photographed QR must not still match.
  if (service_) service_->RefreshQrCodeSession();
  auto_sent_qr_targets_.clear();
  state_.SetQrCodeData(
      service_ ? QString::fromStdString(service_->GetQrCodeUrl()) : QString(),
      {}, 0);
  emit qrCodeUrlChanged();
  updateQrCodeData();
  if (!qr_visible_) {
    qr_visible_ = true;
    emit qrVisibleChanged();
  }
}

void FileShareTrayController::hideQrCode() {
  // Burn the key that was on screen (rotate to a fresh, unshown one) and drop
  // the rendered code, so nothing that scanned it can still connect.
  if (service_) service_->RefreshQrCodeSession();
  auto_sent_qr_targets_.clear();
  state_.SetQrCodeData(QString(), {}, 0);
  emit qrCodeUrlChanged();
  emit qrCodeChanged();
  if (qr_visible_) {
    qr_visible_ = false;
    emit qrVisibleChanged();
  }
}

void FileShareTrayController::attachServiceListeners() {
  NearbySharingApi::Listener listener;

  listener.target_discovered_cb = [this](const NearbySharingApi::ShareTargetInfo& info) {
    QMetaObject::invokeMethod(this, [this, info]() { updateTargetFromInfo(info); },
                              Qt::QueuedConnection);
  };

  listener.target_updated_cb = [this](const NearbySharingApi::ShareTargetInfo& info) {
    QMetaObject::invokeMethod(this, [this, info]() { updateTargetFromInfo(info); },
                              Qt::QueuedConnection);
  };

  listener.target_lost_cb = [this](int64_t share_target_id) {
    QMetaObject::invokeMethod(
        this, [this, share_target_id]() {
          state_.RemoveTarget(share_target_id);
          device_id_by_target_.remove(share_target_id);
          emit discoveredTargetsChanged();
        },
        Qt::QueuedConnection);
  };

  listener.transfer_update_cb = [this](const NearbySharingApi::TransferUpdateInfo& update) {
    QMetaObject::invokeMethod(this, [this, update]() { handleTransferUpdate(update); },
                              Qt::QueuedConnection);
  };

  service_->SetListener(std::move(listener));
}

void FileShareTrayController::updateTargetFromInfo(
    const NearbySharingApi::ShareTargetInfo& info) {
  const QString name = StringUtils::TrimmedOrFallback(
      StringUtils::FromStdString(info.device_name),
      QStringLiteral("Unknown device"));

  // A device that scanned our QR is not a pick-from-a-list target: scanning is
  // the intent to receive, so send to it directly and keep it out of the sheet.
  if (info.is_qr_code_peer && !info.is_incoming) {
    MaybeAutoSendToQrPeer(info, name);
    return;
  }

  const QString trust = info.for_self_share ? QStringLiteral("own")
                        : info.is_known     ? QStringLiteral("contact")
                                            : QStringLiteral("stranger");
  state_.AddOrUpdateTarget(info.id, name, info.is_incoming, info.device_type,
                           trust);
  if (!info.device_id.empty()) {
    device_id_by_target_[info.id] = StringUtils::FromStdString(info.device_id);
  }
  emit discoveredTargetsChanged();

  // If a send is waiting for this device to become reachable again, fire it.
  maybeFireSendRetry(info);
}

void FileShareTrayController::MaybeAutoSendToQrPeer(
    const NearbySharingApi::ShareTargetInfo& info, const QString& name) {
  // A device that scanned our "share via QR code" QR is verified by the engine
  // (MatchQrCodeToken) and flagged is_qr_code_peer. Scanning is the intent to
  // receive, so send to it immediately instead of waiting for a tap. (The name
  // is now the peer's real decrypted name, so gate on the flag, not the name.)
  if (info.is_incoming) return;
  if (!info.is_qr_code_peer) return;
  if (state_.pendingSendFilePaths().isEmpty()) return;
  if (transferActive()) return;
  if (auto_sent_qr_targets_.contains(info.id)) return;

  auto_sent_qr_targets_.insert(info.id);
  sendPendingFileToTarget(info.id);
}

void FileShareTrayController::handleTransferUpdate(
    const NearbySharingApi::TransferUpdateInfo& update) {
  const QString target_name = StringUtils::TrimmedFromStdString(update.device_name);
  if (!target_name.isEmpty()) {
    state_.AddOrUpdateTarget(update.share_target_id, target_name, update.is_incoming);
  }

  const QString name = state_.GetTargetName(update.share_target_id);
  const QString status = StatusMapper::TransferStatusToString(update.status);
  const QString direction =
      update.is_incoming ? QStringLiteral("incoming") : QStringLiteral("outgoing");

  QString file_name = StringUtils::FromStdString(update.first_file_name);
  if (file_name.isEmpty() && !update.is_incoming &&
      state_.pendingSendTargetId() == update.share_target_id &&
      !state_.pendingSendFileName().isEmpty()) {
    file_name = state_.pendingSendFileName();
  }
  // Collapse a multi-attachment set into "<first> +N more" for the row label.
  const int attachment_count =
      update.total_attachments > 0
          ? update.total_attachments
          : (state_.pendingSendTargetId() == update.share_target_id
                 ? state_.pendingSendFileCount()
                 : 1);
  if (attachment_count > 1 && !file_name.isEmpty()) {
    file_name = QStringLiteral("%1 +%2 more")
                    .arg(file_name)
                    .arg(attachment_count - 1);
  }

  // Throughput: bytes since the last update for this target over the elapsed
  // wall-clock time, smoothed so the readout doesn't jitter. Reset once the
  // transfer is no longer in flight.
  const bool in_flight = status == QStringLiteral("InProgress");
  double speed_bps = 0.0;
  const qlonglong now_ms = QDateTime::currentMSecsSinceEpoch();
  if (in_flight) {
    auto& sample = speed_samples_[update.share_target_id];
    if (sample.ms > 0 && now_ms > sample.ms &&
        update.transferred_bytes >= sample.bytes) {
      const double instant = static_cast<double>(update.transferred_bytes -
                                                 sample.bytes) *
                             1000.0 / static_cast<double>(now_ms - sample.ms);
      // Exponential smoothing; seed on the first real sample.
      sample.bps = sample.bps > 0.0 ? 0.6 * sample.bps + 0.4 * instant : instant;
    }
    sample.bytes = update.transferred_bytes;
    sample.ms = now_ms;
    speed_bps = sample.bps;
  } else {
    speed_samples_.remove(update.share_target_id);
  }

  // 1-based index of the file currently in flight, and the batch size.
  const int total_files = update.total_attachments;
  const int current_file =
      total_files > 0
          ? std::min(update.transferred_attachments + 1, total_files)
          : 0;

  state_.AddOrUpdateTransfer(update.share_target_id, name, status, update.progress,
                             update.transferred_bytes, direction, file_name,
                             StringUtils::FromStdString(update.first_file_path),
                             speed_bps, current_file, total_files);
  if (transfer_sweep_timer_ && !transfer_sweep_timer_->isActive()) {
    transfer_sweep_timer_->start();
  }
  emit transfersChanged();

  setStatus(QStringLiteral("%1 (%2)").arg(status, name));

  if (update.status !=
      NearbySharingApi::TransferStatus::kAwaitingLocalConfirmation) {
    // Answered here, cancelled, or finished — nothing left to decide, so any
    // desktop prompt still on screen is stale.
    emit dismissIncomingDecision(update.share_target_id);
  }

  if (update.status ==
      NearbySharingApi::TransferStatus::kAwaitingLocalConfirmation) {
    if (state_.autoAcceptIncoming()) {
      service_->Accept(update.share_target_id,
                       [](NearbySharingApi::StatusCode) {});
    } else if (!window_visible_) {
      // Only worth asking on the desktop when the window isn't up: if it is,
      // its own Accept/Decline row is already on screen and a notification
      // just duplicates it.
      emit requestIncomingDecision(update.share_target_id, name, file_name);
    }
  }

  // Handle final transfer status
  if (StatusMapper::IsFinalTransferStatus(update.status)) {
    handleTransferComplete(update);
  }
}

void FileShareTrayController::handleTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update) {
  const bool success = update.status == NearbySharingApi::TransferStatus::kComplete;
  const QString name = state_.GetTargetName(update.share_target_id);

  if (update.is_incoming) {
    handleIncomingTransferComplete(update, name, success);
  } else {
    handleOutgoingTransferComplete(update, name, success);
  }

  // Keep the staged file but release the target so the user stays in send
  // mode and can send the same file to another device.
  if (state_.pendingSendTargetId() == update.share_target_id) {
    state_.ClearPendingSendTarget();
  }
  // NOTE: do not re-arm receive here. The connections lib already
  // auto-re-advertises ~0.5s after disconnect. A manual StopReceiveMode/
  // StartReceiveMode rotates the endpoint id right after the sender discovered
  // us, so its reconnect for a second transfer targets a dead endpoint.

  // Deferred target removal
  if (state_.IsPendingTargetRemoval(update.share_target_id)) {
    QTimer::singleShot(1400, this, [this, target_id = update.share_target_id]() {
      if (state_.IsPendingTargetRemoval(target_id)) {
        state_.RemoveTarget(target_id);
        emit discoveredTargetsChanged();
      }
    });
  }

  // The blob now reads e.g. "Complete (Pixel)". Revert it to the idle "Ready to
  // receive" a few seconds later, unless another transfer became active in the
  // meantime.
  QTimer::singleShot(4000, this, [this]() {
    if (state_.running() && !state_.HasActiveTransfers()) {
      setStatus(QStringLiteral("Ready to receive"));
    }
  });

  // After a successful send, leave the "Sent ✓" row up for a beat, then return
  // the sheet to the receive home on its own. Failures stay put so the user can
  // read why / retry.
  if (!update.is_incoming && success) {
    armSendReturnToReceive();
  }
}

void FileShareTrayController::armSendReturnToReceive() {
  if (send_return_timer_) send_return_timer_->start();  // restarts the interval
}

void FileShareTrayController::cancelSendReturnToReceive() {
  if (send_return_timer_) send_return_timer_->stop();
}

void FileShareTrayController::handleIncomingTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update, const QString& name,
    bool success) {
  if (!success) {
    // Distinguish an intentional cancel/decline/timeout from a genuine failure
    // so the notification doesn't cry "failed" when nothing went wrong.
    QString title = QStringLiteral("Receive failed");
    QString body = QStringLiteral("Transfer from %1 failed").arg(name);
    switch (update.status) {
      case NearbySharingApi::TransferStatus::kCancelled:
        title = QStringLiteral("Transfer cancelled");
        body = QStringLiteral("Cancelled transfer from %1").arg(name);
        break;
      case NearbySharingApi::TransferStatus::kRejected:
        title = QStringLiteral("Transfer declined");
        body = QStringLiteral("Declined transfer from %1").arg(name);
        break;
      case NearbySharingApi::TransferStatus::kTimedOut:
        title = QStringLiteral("Transfer timed out");
        body = QStringLiteral("Transfer from %1 timed out").arg(name);
        break;
      default:
        break;
    }
    emit requestTrayMessage(title, body);
    return;
  }

  const QString file_name =
      StringUtils::FromStdString(update.first_file_name).isEmpty()
          ? QStringLiteral("file")
          : StringUtils::FromStdString(update.first_file_name);

  // Check for received URL
  for (const auto& text : update.text_attachments) {
    if (text.type == NearbySharingApi::TextAttachmentType::kUrl) {
      const QString link = StringUtils::TrimmedFromStdString(text.text_body);
      if (!link.isEmpty()) {
        emit requestCopyLinkTrayMessage(QStringLiteral("Link received"),
                                        QStringLiteral("%1 from %2").arg(link, name),
                                        link);
        return;
      }
    }
  }

  // Check for received text
  if (!update.text_attachments.empty()) {
    const QString text_summary = [&]() {
      for (const auto& text : update.text_attachments) {
        const QString title = StringUtils::TrimmedFromStdString(text.text_title);
        if (!title.isEmpty()) return title;
        const QString body = StringUtils::TrimmedFromStdString(text.text_body);
        if (!body.isEmpty()) return body;
      }
      return QStringLiteral("Text");
    }();
    emit requestTrayMessage(QStringLiteral("Text received"),
                            QStringLiteral("%1 from %2").arg(text_summary, name));
    return;
  }

  emit requestTrayMessage(QStringLiteral("File received"),
                          QStringLiteral("%1 from %2").arg(file_name, name));
}

void FileShareTrayController::handleOutgoingTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update, const QString& name,
    bool success) {
  const QString file_name =
      StringUtils::FromStdString(update.first_file_name).isEmpty()
          ? QStringLiteral("file")
          : StringUtils::FromStdString(update.first_file_name);

  if (success) {
    emit requestTrayMessage(QStringLiteral("Send complete"),
                            QStringLiteral("%1 sent to %2").arg(file_name, name));
    return;
  }

  // As with receiving, name the cancel/decline/timeout cases instead of
  // blaming a failure.
  QString title = QStringLiteral("Send failed");
  QString body = QStringLiteral("%1 failed to send to %2").arg(file_name, name);
  switch (update.status) {
    case NearbySharingApi::TransferStatus::kCancelled:
      title = QStringLiteral("Send cancelled");
      body = QStringLiteral("Cancelled sending %1 to %2").arg(file_name, name);
      break;
    case NearbySharingApi::TransferStatus::kRejected:
      title = QStringLiteral("Send declined");
      body = QStringLiteral("%1 declined %2").arg(name, file_name);
      break;
    case NearbySharingApi::TransferStatus::kTimedOut:
      title = QStringLiteral("Send timed out");
      body = QStringLiteral("Sending %1 to %2 timed out").arg(file_name, name);
      break;
    default:
      break;
  }
  emit requestTrayMessage(title, body);
}

void FileShareTrayController::loadSettings() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));

  const QString stored_device_name =
      settings.value(QStringLiteral("deviceName"), state_.deviceName())
          .toString()
          .trimmed();
  if (!stored_device_name.isEmpty()) {
    state_.SetDeviceName(stored_device_name);
  }

  const bool stored_auto_accept =
      settings.value(QStringLiteral("autoAcceptIncoming"), false).toBool();
  state_.SetAutoAcceptIncoming(stored_auto_accept);

  const bool stored_enable_5ghz_hotspot =
      settings.value(QStringLiteral("enable5GhzHotspot"), true).toBool();
  state_.SetEnable5GhzHotspot(stored_enable_5ghz_hotspot);

  state_.SetHotspotBoost(
      settings.value(QStringLiteral("hotspotBoost"), false).toBool());

  const QString stored_log_path =
      settings.value(QStringLiteral("logPath"), DefaultLogPath())
          .toString()
          .trimmed();
  if (!stored_log_path.isEmpty()) {
    state_.SetLogPath(stored_log_path);
  }

  const QString stored_save_path =
      settings.value(QStringLiteral("savePath"), QString()).toString();
  state_.SetSavePath(resolveSavePath(stored_save_path));

  state_.SetDeveloperMode(
      settings.value(QStringLiteral("developerMode"), false).toBool());

  int stored_visibility =
      settings.value(QStringLiteral("visibility"), 0).toInt();
  // Migrate the old 3-value model. Old "Contacts" (1) advertised the self
  // identity (SELF_SHARE), which is now the dedicated "Your devices" (3); remap
  // it so existing users keep the exact behavior they had. The temporary
  // Everyone (4) is never persisted (saveSettings stores the base), so anything
  // out of the 0..3 range falls back to Everyone.
  if (stored_visibility == 1) {
    stored_visibility = 3;
  } else if (stored_visibility < 0 || stored_visibility > 3) {
    stored_visibility = 0;
  }
  visibility_ = stored_visibility;
  pre_temp_visibility_ = visibility_;
}

void FileShareTrayController::saveSettings() const {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  settings.setValue(QStringLiteral("deviceName"), state_.deviceName());
  settings.setValue(QStringLiteral("autoAcceptIncoming"), state_.autoAcceptIncoming());
  settings.setValue(QStringLiteral("enable5GhzHotspot"),
                    state_.enable5GhzHotspot());
  settings.setValue(QStringLiteral("hotspotBoost"), state_.hotspotBoost());
  settings.setValue(QStringLiteral("logPath"), state_.logPath());
  settings.setValue(QStringLiteral("savePath"), state_.savePath());
  settings.setValue(QStringLiteral("developerMode"), state_.developerMode());
  // Never persist the transient "Everyone (10 min)" (4) — store its base so a
  // restart mid-window lands on the safe prior visibility, not stuck on Everyone.
  settings.setValue(QStringLiteral("visibility"),
                    visibility_ == 4 ? pre_temp_visibility_ : visibility_);
}

void FileShareTrayController::setDeviceName(const QString& device_name) {
  const QString trimmed = device_name.trimmed();
  if (trimmed.isEmpty() || trimmed == state_.deviceName()) {
    return;
  }

  state_.SetDeviceName(trimmed);
  saveSettings();
  emit deviceNameChanged();

  if (state_.running()) {
    // Rebuild through the ordered path so the rename can't race the old engine's
    // shutdown against its destruction (same crash hardReset hit).
    rebuildService();
  }
}

void FileShareTrayController::setAutoAcceptIncoming(bool enabled) {
  if (enabled == state_.autoAcceptIncoming()) {
    return;
  }
  state_.SetAutoAcceptIncoming(enabled);
  saveSettings();
  emit autoAcceptIncomingChanged();
}

void FileShareTrayController::setEnable5GhzHotspot(bool enabled) {
  if (enabled == state_.enable5GhzHotspot()) {
    return;
  }
  state_.SetEnable5GhzHotspot(enabled);
  if (service_) {
    service_->Set5GhzHotspotEnabled(enabled);
  }
  saveSettings();
  emit enable5GhzHotspotChanged();
}

void FileShareTrayController::setHotspotBoost(bool enabled) {
  if (enabled == state_.hotspotBoost()) {
    return;
  }
  state_.SetHotspotBoost(enabled);
  if (service_) {
    service_->SetHotspotBoostEnabled(enabled);
  }
  saveSettings();
  emit hotspotBoostChanged();
}

void FileShareTrayController::setVisibility(int mode) {
  if (mode < 0 || mode > 4 || mode == visibility_) {
    return;
  }
  // Any explicit change cancels a pending "Everyone (10 min)" revert.
  if (temp_visibility_timer_) {
    temp_visibility_timer_->stop();
  }
  // Entering the temporary-Everyone mode: remember what to fall back to. At this
  // point visibility_ is never 4 (that would have been the early-return above).
  if (mode == 4) {
    pre_temp_visibility_ = visibility_;
  }
  visibility_ = mode;
  // Applies live if the service is receiving; otherwise takes effect on next
  // StartReceiveMode (see NearbySharingApi::SetVisibility). Mode 4 maps to
  // Everyone in the engine.
  if (service_) {
    service_->SetVisibility(mode);
  }
  saveSettings();
  emit visibilityChanged();
  if (mode == 4) {
    if (!temp_visibility_timer_) {
      temp_visibility_timer_ = new QTimer(this);
      temp_visibility_timer_->setSingleShot(true);
      connect(temp_visibility_timer_, &QTimer::timeout, this,
              [this] { setVisibility(pre_temp_visibility_); });
    }
    temp_visibility_timer_->start(kTempEveryoneMs);
  }
}

namespace {
constexpr char kMyDevicesService[] = "dev.scotty.MyDevices1";
constexpr char kMyDevicesPath[] = "/dev/scotty/MyDevices1";
constexpr char kMyDevicesIface[] = "dev.scotty.MyDevices1";
}  // namespace

void FileShareTrayController::setupMyDevicesBus() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  // React to the plugin appearing / disappearing (install, activation, exit).
  auto* watcher = new QDBusServiceWatcher(
      QLatin1String(kMyDevicesService), bus,
      QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
          &FileShareTrayController::onMyDevicesOwnerChanged);
  // AccountChanged(bool signed_in, string email) pushes account updates.
  bus.connect(QLatin1String(kMyDevicesService), QLatin1String(kMyDevicesPath),
              QLatin1String(kMyDevicesIface), QStringLiteral("AccountChanged"),
              this, SLOT(onMyDevicesAccountChanged(bool, QString)));

  refreshMyDevicesAvailability();
  if (mydevices_available_) {
    refreshMyDevicesAccount();
  }
}

void FileShareTrayController::refreshMyDevicesAvailability() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  bool available = false;
  if (auto* dbus = bus.interface()) {
    if (dbus->isServiceRegistered(QLatin1String(kMyDevicesService))) {
      available = true;
    } else {
      // Installed-but-not-running: activatable name (no activation triggered).
      QDBusReply<QStringList> names =
          dbus->call(QStringLiteral("ListActivatableNames"));
      if (names.isValid()) {
        available = names.value().contains(QLatin1String(kMyDevicesService));
      }
    }
  }
  if (available != mydevices_available_) {
    mydevices_available_ = available;
    emit mydevicesAvailableChanged();
  }
}

void FileShareTrayController::refreshMyDevicesAccount() {
  QDBusInterface plugin(QLatin1String(kMyDevicesService),
                        QLatin1String(kMyDevicesPath),
                        QLatin1String(kMyDevicesIface),
                        QDBusConnection::sessionBus());
  QDBusMessage reply = plugin.call(QStringLiteral("GetAccountInfo"));
  if (reply.type() != QDBusMessage::ReplyMessage) {
    return;  // plugin not answering; leave current state
  }
  const QList<QVariant> args = reply.arguments();
  const bool signed_in = !args.isEmpty() && args.at(0).toBool();
  setSignedInEmail(signed_in && args.size() > 1 ? args.at(1).toString()
                                                : QString());
  if (signed_in) {
    refreshMyDevicesProfile();
  }
}

void FileShareTrayController::refreshMyDevicesProfile() {
  auto* plugin = new QDBusInterface(
      QLatin1String(kMyDevicesService), QLatin1String(kMyDevicesPath),
      QLatin1String(kMyDevicesIface), QDBusConnection::sessionBus(), this);
  QDBusPendingCall pending = plugin->asyncCall(QStringLiteral("GetProfile"));
  auto* watcher = new QDBusPendingCallWatcher(pending, this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, plugin](QDBusPendingCallWatcher* w) {
            QDBusPendingReply<QString, QString> reply = *w;
            if (reply.isValid()) {
              // GetProfile also returns the photo *path*, but that path is in
              // the plugin's ~/.config and is unreadable from the flatpak
              // sandbox. Take only the name here; fetch the photo as bytes.
              const QString name = reply.argumentAt<0>();
              if (name != signed_in_name_) {
                signed_in_name_ = name; emit signedInNameChanged();
              }
            }
            w->deleteLater();
            plugin->deleteLater();
          });
  refreshMyDevicesPhoto();
}

void FileShareTrayController::refreshMyDevicesPhoto() {
  auto* plugin = new QDBusInterface(
      QLatin1String(kMyDevicesService), QLatin1String(kMyDevicesPath),
      QLatin1String(kMyDevicesIface), QDBusConnection::sessionBus(), this);
  QDBusPendingCall pending =
      plugin->asyncCall(QStringLiteral("GetProfilePhoto"));
  auto* watcher = new QDBusPendingCallWatcher(pending, this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
          [this, plugin](QDBusPendingCallWatcher* w) {
            QDBusPendingReply<QByteArray> reply = *w;
            if (reply.isValid()) {
              // Cache the bytes in our own config dir (a distinct filename from
              // the plugin's, so a shared-home .deb install never collides) and
              // point the UI at that local copy.
              const QByteArray bytes = reply.argumentAt<0>();
              const QString cache =
                  QStandardPaths::writableLocation(
                      QStandardPaths::GenericConfigLocation) +
                  QStringLiteral("/Scotty/mydevices-photo.jpg");
              QString path;
              if (!bytes.isEmpty()) {
                QDir().mkpath(QFileInfo(cache).absolutePath());
                QFile file(cache);
                if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                  file.write(bytes);
                  file.close();
                  path = cache;
                }
              } else {
                QFile::remove(cache);  // signed in, no photo
              }
              if (path != signed_in_photo_path_) {
                signed_in_photo_path_ = path;
                emit signedInPhotoPathChanged();
              }
            }
            w->deleteLater();
            plugin->deleteLater();
          });
}

void FileShareTrayController::requestMyDevicesSignIn() {
  // Fire-and-forget: the plugin runs the WebView flow and reports back via the
  // AccountChanged signal. Activates the plugin if installed but not running.
  QDBusInterface plugin(QLatin1String(kMyDevicesService),
                        QLatin1String(kMyDevicesPath),
                        QLatin1String(kMyDevicesIface),
                        QDBusConnection::sessionBus());
  plugin.asyncCall(QStringLiteral("StartSignIn"));
}

void FileShareTrayController::signOutMyDevices() {
  QDBusInterface plugin(QLatin1String(kMyDevicesService),
                        QLatin1String(kMyDevicesPath),
                        QLatin1String(kMyDevicesIface),
                        QDBusConnection::sessionBus());
  plugin.asyncCall(QStringLiteral("SignOut"));
}

void FileShareTrayController::onMyDevicesAccountChanged(bool signed_in,
                                                       const QString& email) {
  setSignedInEmail(signed_in ? email : QString());
  if (signed_in) {
    refreshMyDevicesProfile();
    return;
  }
  // Signed out. "Contacts" (1) and "Your devices" (3) both need the account to
  // mean anything — without it they cannot resolve who to be visible to. Leaving
  // the menu on one of those would keep advertising under a mode that no longer
  // works, so drop to "No one" (2). This mirrors the engine, which on logout
  // resets any non-Everyone visibility to hidden (see ResetAllSettings). Everyone
  // (0) / temporary Everyone (4) need no account, so they are left untouched.
  if (visibility_ == 1 || visibility_ == 3) {
    setVisibility(2);
  }
  // Send-targets discovered while signed in (e.g. own devices resolved via the
  // account's certificates) can no longer be rebuilt once the account is gone —
  // the engine drops the endpoint ("Failed to convert discovered advertisement")
  // so the cached entry is stale and a send to it errors out. Clear the list;
  // any still-valid Everyone-mode target re-appears on the next scan.
  state_.ClearTargets();
  emit discoveredTargetsChanged();
}

void FileShareTrayController::onMyDevicesOwnerChanged(const QString& /*service*/,
                                                     const QString& /*old_owner*/,
                                                     const QString& new_owner) {
  refreshMyDevicesAvailability();
  if (new_owner.isEmpty()) {
    setSignedInEmail(QString());  // plugin gone
  } else {
    refreshMyDevicesAccount();  // plugin appeared
  }
}

void FileShareTrayController::setSignedInEmail(const QString& email) {
  if (email == signed_in_email_) {
    return;
  }
  signed_in_email_ = email;
  emit signedInEmailChanged();
  if (email.isEmpty()) {
    if (!signed_in_name_.isEmpty()) { signed_in_name_.clear(); emit signedInNameChanged(); }
    if (!signed_in_photo_path_.isEmpty()) { signed_in_photo_path_.clear(); emit signedInPhotoPathChanged(); }
  }
}

static QString AutostartFilePath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         QStringLiteral("/autostart/dev.scotty.Scotty.desktop");
}

// There is no XDG key for "start minimized" (Hidden= means disabled), so the
// autostart entry passes --background and the app skips showing its window.
// Advertising visibility is unaffected.
static void WriteAutostartFile() {
  const QString path = AutostartFilePath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }
  QTextStream out(&file);
  out << "[Desktop Entry]\n"
      << "Type=Application\n"
      << "Name=Scotty\n"
      << "Exec=\"" << QCoreApplication::applicationFilePath()
      << "\" --background\n"
      << "Icon=dev.scotty.Scotty\n"
      << "Terminal=false\n"
      << "X-GNOME-Autostart-enabled=true\n";
}

bool FileShareTrayController::runAtStartup() const {
  return QFileInfo::exists(AutostartFilePath());
}

void FileShareTrayController::setRunAtStartup(bool enabled) {
  const QString path = AutostartFilePath();
  if (enabled == QFileInfo::exists(path)) {
    return;
  }
  if (enabled) {
    WriteAutostartFile();
  } else {
    QFile::remove(path);
  }
  emit runAtStartupChanged();
}

void FileShareTrayController::refreshAutostartFile() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  // Run at startup defaults ON: on the first launch, enable it once. The
  // autostart entry passes --background, so nothing pops on login. The guard
  // key makes this a one-time default — if the user later turns it off, it
  // stays off.
  if (!settings.value(QStringLiteral("runAtStartupInitialized"), false)
           .toBool()) {
    WriteAutostartFile();
    settings.setValue(QStringLiteral("runAtStartupInitialized"), true);
    return;
  }
  // Otherwise rewrite in place so entries written by an older build (no
  // --background, the earlier --hidden spelling, or an unquoted Exec path) pick
  // up the current format.
  if (runAtStartup()) {
    WriteAutostartFile();
  }
}

void FileShareTrayController::setReceiveForeground(bool foreground) {
  // Tracks the window: also decides whether an incoming request needs a
  // desktop prompt or is already answerable on screen.
  window_visible_ = foreground;
  if (!service_) {
    return;
  }
  service_->SetReceiveForeground(foreground,
                                 [](NearbySharingApi::StatusCode) {});
}

void FileShareTrayController::quitApplication() {
  // The QML window deliberately rejects ordinary close events to hide to the
  // tray. quit() can therefore be rejected too; exit(0) is Qt's documented
  // non-interruptible application exit. aboutToQuit still performs cleanup.
  QCoreApplication::exit(0);
}

void FileShareTrayController::setLogPath(const QString& path) {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty() || trimmed == state_.logPath()) {
    return;
  }
  state_.SetLogPath(trimmed);
  saveSettings();
  emit logPathChanged();
}

QString FileShareTrayController::defaultSavePath() {
  return QDir::homePath() + QStringLiteral("/Downloads/Scotty");
}

QString FileShareTrayController::resolveSavePath(const QString& raw) const {
  QString p = raw.trimmed();

  if (!p.isEmpty()) {
    // Expand a leading ~ to the home directory.
    if (p == QStringLiteral("~")) {
      p = QDir::homePath();
    } else if (p.startsWith(QStringLiteral("~/"))) {
      p = QDir::homePath() + p.mid(1);
    }
    // A relative path is interpreted under the home directory.
    if (!QDir::isAbsolutePath(p)) {
      p = QDir::homePath() + QLatin1Char('/') + p;
    }
    // Collapse //, resolve .., strip any trailing slash.
    p = QDir::cleanPath(p);
  }

  if (p.isEmpty()) {
    p = defaultSavePath();
  }

  // Create it and confirm it is a writable directory; otherwise fall back.
  QDir().mkpath(p);
  QFileInfo info(p);
  if (!info.exists() || !info.isDir() || !info.isWritable()) {
    const QString fallback = defaultSavePath();
    if (p != fallback) {
      QDir().mkpath(fallback);
      return fallback;
    }
  }
  return p;
}

void FileShareTrayController::setSavePath(const QString& path) {
  const QString resolved = resolveSavePath(path);
  if (resolved == state_.savePath()) {
    return;
  }
  state_.SetSavePath(resolved);
  if (service_) {
    service_->SetSavePath(resolved.toStdString());
  }
  saveSettings();
  emit savePathChanged();
}

void FileShareTrayController::setDeveloperMode(bool enabled) {
  if (enabled == state_.developerMode()) {
    return;
  }
  state_.SetDeveloperMode(enabled);
  saveSettings();
  emit developerModeChanged();
}

void FileShareTrayController::start() {
  if (state_.running()) {
    return;
  }

  state_.SetRunning(true);
  emit runningChanged();

}

void FileShareTrayController::stop() {
  if (!state_.running()) {
    return;
  }

  state_.SetRunning(false);
  emit runningChanged();

  readvertise_timer_->stop();
  service_->StopSendMode([](NearbySharingApi::StatusCode) {});
  service_->StopReceiveMode([](NearbySharingApi::StatusCode) {});

  state_.ClearAll();
  emit discoveredTargetsChanged();
  emit transfersChanged();

  setStatus(QStringLiteral("Stopped"));
}

void FileShareTrayController::startSendMode() {
  readvertise_timer_->stop();  // receive-advert self-heal only runs while receiving
  // StopReceiveMode is best-effort: whatever surface state the service is in,
  // we still want to (re)start send discovery. Gating StartSendMode on the stop
  // status left the service idle (and the UI stuck "looking") whenever the stop
  // returned an unexpected code, so always proceed to StartSendMode.
  service_->StopReceiveMode([this](NearbySharingApi::StatusCode /*status*/) {
    service_->StartSendMode([this](NearbySharingApi::StatusCode status) {
      QMetaObject::invokeMethod(
          this,
          [this, status]() {
            setStatus(QStringLiteral("StartSendMode: %1")
                          .arg(StatusMapper::ApiStatusToString(status)));
            if (status != NearbySharingApi::StatusCode::kOk) {
              state_.SetRunning(false);
              emit runningChanged();
            }
          },
          Qt::QueuedConnection);
    });
  });
}

void FileShareTrayController::rescanDevices() {
  // Force a fresh discovery cycle without changing the staged files. Only makes
  // sense in send mode; StartSendMode now unregisters+re-registers the send
  // surface, so this reliably restarts scanning when a target went stale.
  if (!state_.running() || state_.mode() != QStringLiteral("Send")) {
    return;
  }
  if (state_.HasActiveTransfers()) {
    return;  // never disturb an in-flight transfer
  }
  startSendMode();
}

void FileShareTrayController::hardReset() {
  // The nuclear "unstick": tear down and rebuild the whole engine. Replacing the
  // service object destroys the old one, which drops every active connection
  // (a wedged transfer included) and re-registers the radios/mediums from
  // scratch, then re-advertises with a fresh endpoint id and QR code. This is
  // the same teardown/rebuild a device-name change already performs. Unlike
  // rescanDevices it deliberately does NOT spare an in-flight transfer — a stuck
  // transfer is exactly what this button is for.
  setStatus(QStringLiteral("Resetting connection…"));
  rebuildService();
  setStatus(QStringLiteral("Connection reset — ready to receive."));
}

void FileShareTrayController::startDiscoveryWatchdog() {
  discovery_watchdog_empty_ticks_ = 0;
  if (discovery_watchdog_timer_ && !discovery_watchdog_timer_->isActive()) {
    discovery_watchdog_timer_->start();
  }
}

void FileShareTrayController::stopDiscoveryWatchdog() {
  if (discovery_watchdog_timer_) {
    discovery_watchdog_timer_->stop();
  }
}

void FileShareTrayController::onDiscoveryWatchdogTick() {
  // Self-heal only while actively looking for a device.
  if (!state_.running() || state_.mode() != QStringLiteral("Send")) {
    stopDiscoveryWatchdog();
    return;
  }
  if (state_.HasActiveTransfers()) {
    return;  // a transfer is running/starting — leave it alone, retry next tick
  }
  if (!state_.discoveredTargets().isEmpty()) {
    // Devices are already listed — discovery is healthy and the service's
    // found/lost events keep availability current. Don't re-cycle (it would
    // make the list flicker). Only self-heal the empty "still looking" case.
    discovery_watchdog_empty_ticks_ = 0;
    return;
  }
  // Nothing found yet. Re-cycle discovery a bounded number of times to recover a
  // quietly-dropped scan, then GIVE UP: if a peer simply isn't reachable (e.g.
  // it's backgrounded/out of range), re-cycling the whole stack every 10 s
  // forever just thrashes advertising/discovery and wedges the UI. Discovery is
  // still live after we stop — the service's own found events will surface a
  // peer if one appears; the watchdog only covers the transient stuck case.
  if (++discovery_watchdog_empty_ticks_ > kDiscoveryWatchdogMaxEmptyTicks) {
    stopDiscoveryWatchdog();
    return;
  }
  rescanDevices();
}

void FileShareTrayController::onReadvertiseTick() {
  // Only while actively receiving.
  if (!state_.running() || state_.mode() != QStringLiteral("Receive")) {
    readvertise_timer_->stop();
    return;
  }
  // "No one" (2) isn't discoverable by design — nothing to refresh.
  if (visibility_ == 2) {
    return;
  }
  // Never disrupt an in-flight transfer; refresh on the next tick instead.
  if (state_.HasActiveTransfers()) {
    return;
  }
  // Re-assert the advert. StopReceiveMode + StartReceiveMode re-registers the
  // BLE advert (and rotates the endpoint id), which kicks a chipset that
  // silently stopped emitting while the stack still thought it was advertising.
  // Visibility is unchanged, so this is invisible to the user beyond a brief
  // re-register. Fires every kReadvertiseIntervalMs.
  service_->StopReceiveMode([this](NearbySharingApi::StatusCode) {
    service_->StartReceiveMode([](NearbySharingApi::StatusCode) {});
  });
}

void FileShareTrayController::startReceiveMode() {
  // Keep the discovery advert fresh against chipsets that silently stop
  // emitting it (see onReadvertiseTick). Restart the interval on each entry.
  readvertise_timer_->start();
  service_->StopSendMode([this](NearbySharingApi::StatusCode status) {
    if (status == NearbySharingApi::StatusCode::kOk ||
        status == NearbySharingApi::StatusCode::kStatusAlreadyStopped) {
      service_->StartReceiveMode([this](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, status]() {
              if (status == NearbySharingApi::StatusCode::kOk) {
                setStatus(QStringLiteral("Ready to receive"));
              } else {
                setStatus(QStringLiteral("Couldn't start receiving"));
                state_.SetRunning(false);
                emit runningChanged();
              }
            },
            Qt::QueuedConnection);
      });
    }
  });
}

void FileShareTrayController::switchToReceiveMode() {
  if (state_.running() && state_.HasActiveTransfers()) {
    setStatus(QStringLiteral("Cannot switch mode while transfer is active"));
    emit requestTrayMessage(
        QStringLiteral("Transfer in progress"),
        QStringLiteral("Wait for the current transfer to complete."));
    return;
  }
  // A manual return pre-empts any pending post-send auto-return (harmless when
  // this call is the auto-return itself — the timer has already fired).
  cancelSendReturnToReceive();
  // Leaving the send sheet burns any QR that was on screen.
  if (qr_visible_) hideQrCode();
  // Drop the staged file so the UI (driven by pendingSendFilePath) returns
  // to the receive blob.
  state_.ClearPendingSendFile();
  emit pendingSendFilePathChanged();
  emit pendingSendFileNameChanged();

  stopDiscoveryWatchdog();
  if (state_.running())
  {
    startReceiveMode();
    state_.SetMode(QStringLiteral("Receive"));
    emit modeChanged();
  }
}

void FileShareTrayController::switchToSendModeWithFile(const QString& file_path) {
  switchToSendModeWithFiles(QStringList{file_path});
}

void FileShareTrayController::switchToSendModeWithText(const QString& text) {
  // A new send is a fresh intent (see beginSendWithFiles).
  cancelSendReturnToReceive();
  if (qr_visible_) hideQrCode();

  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    setStatus(QStringLiteral("Nothing to send"));
    emit requestTrayMessage(QStringLiteral("Send canceled"),
                            QStringLiteral("Enter some text or a link to send."));
    return;
  }

  state_.ClearFinishedTransfers();
  emit transfersChanged();

  state_.SetPendingSendText(trimmed);
  emit pendingSendFilePathChanged();
  emit pendingSendFileNameChanged();

  if (state_.running()) {
    startSendMode();
    state_.SetMode(QStringLiteral("Send"));
    emit modeChanged();
    startDiscoveryWatchdog();
  }
  setStatus(QStringLiteral("Discovery started. Choose a nearby device."));
  emit requestTrayMessage(
      QStringLiteral("Send mode"),
      QStringLiteral("Ready to send text/link. Choose a nearby device."));
}

// A sendable file: the transport needs a real, non-empty size, and a single
// zero-byte entry fails the *whole* batch (NearbySharingApi::SendFiles returns
// kInvalidArgument for the set), so they are dropped rather than passed on.
static bool IsSendableFile(const QFileInfo& info) {
  return info.exists() && info.isFile() && info.size() > 0;
}

void FileShareTrayController::switchToSendModeWithFiles(
    const QStringList& file_paths) {
  // Starting a fresh send cancels a pending post-send auto-return, and hides any
  // QR left over from a previous share (a new share starts with no QR shown).
  cancelSendReturnToReceive();
  if (qr_visible_) hideQrCode();
  QStringList paths;
  QStringList names;
  QStringList dirs;
  int skipped_empty = 0;
  for (const QString& raw : file_paths) {
    const QString trimmed_path = raw.trimmed();
    if (trimmed_path.isEmpty()) {
      continue;
    }
    const QFileInfo info(trimmed_path);
    if (!info.exists()) {
      continue;
    }

    if (info.isDir()) {
      // Quick Share has no folder attachment type; the desktop clients (Quick
      // Share for Windows/ChromeOS) compress a shared folder into one archive.
      // Collect directories and zip each below before entering send mode.
      dirs.append(info.absoluteFilePath());
      continue;
    }

    if (!IsSendableFile(info)) {
      if (info.isFile()) {
        ++skipped_empty;
      }
      continue;
    }
    paths.append(info.absoluteFilePath());
    names.append(info.fileName());
  }

  if (dirs.isEmpty()) {
    beginSendWithFiles(paths, names, skipped_empty);
    return;
  }

  // Compress each selected folder into a single .zip and send that. Zipping
  // runs as async QProcesses so the UI never freezes; when they all finish we
  // continue into send mode with the archives added to any plain files.
  setStatus(dirs.size() == 1 ? QStringLiteral("Compressing folder…")
                             : QStringLiteral("Compressing folders…"));

  auto batch = QSharedPointer<ZipBatch>::create();
  batch->ready_paths = paths;
  batch->ready_names = names;
  batch->skipped_empty = skipped_empty;
  batch->remaining = dirs.size();

  const QString temp_root =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
      QStringLiteral("/scotty-share-%1")
          .arg(QDateTime::currentMSecsSinceEpoch());

  auto finishOne = [this, batch]() {
    if (--batch->remaining == 0) {
      beginSendWithFiles(batch->ready_paths, batch->ready_names,
                         batch->skipped_empty);
    }
  };

  for (const QString& dir : dirs) {
    const QFileInfo dir_info(dir);
    const QString folder_name = dir_info.fileName().isEmpty()
                                    ? QStringLiteral("folder")
                                    : dir_info.fileName();
    // A fresh unique dir per folder: `zip` appends to an existing archive, so
    // the destination must not already exist.
    const QString out_dir =
        temp_root + QStringLiteral("/") + QString::number(batch->index++);
    QDir().mkpath(out_dir);
    const QString zip_path =
        out_dir + QStringLiteral("/") + folder_name + QStringLiteral(".zip");

    auto* proc = new QProcess(this);
    proc->setWorkingDirectory(dir_info.absolutePath());
    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, batch, proc, zip_path, folder_name, finishOne](
                int exit_code, QProcess::ExitStatus status) {
              if (status == QProcess::NormalExit && exit_code == 0 &&
                  QFileInfo::exists(zip_path)) {
                batch->ready_paths.append(zip_path);
                batch->ready_names.append(QFileInfo(zip_path).fileName());
              } else {
                emit requestTrayMessage(
                    QStringLiteral("Compression failed"),
                    QStringLiteral("Could not compress %1.").arg(folder_name));
              }
              proc->deleteLater();
              finishOne();
            });
    // -r recursive, -q quiet; run from the folder's parent so the archive
    // stores `folder_name/…` rather than absolute paths.
    proc->start(QStringLiteral("zip"),
                {QStringLiteral("-r"), QStringLiteral("-q"), zip_path,
                 folder_name});
    if (!proc->waitForStarted(2000)) {
      proc->disconnect();  // don't let a late finished() double-count
      proc->deleteLater();
      emit requestTrayMessage(
          QStringLiteral("Compression unavailable"),
          QStringLiteral("The 'zip' tool is not available to compress %1.")
              .arg(folder_name));
      finishOne();
    }
  }
}

void FileShareTrayController::beginSendWithFiles(const QStringList& paths,
                                                 const QStringList& names,
                                                 int skipped_empty) {
  if (paths.isEmpty()) {
    const QString detail =
        skipped_empty > 0
            ? QStringLiteral("Empty files can't be sent.")
            : QStringLiteral("Please choose a valid file or folder.");
    setStatus(QStringLiteral("Nothing to send"));
    emit requestTrayMessage(QStringLiteral("Send canceled"), detail);
    return;
  }

  // A new send is a fresh intent. Drop finished/stale transfer records so a
  // prior Complete/Failed (or a stalled outgoing transfer that never reached a
  // final state) can't block this send or clutter the device cards.
  state_.ClearFinishedTransfers();
  emit transfersChanged();

  state_.SetPendingSendFiles(paths, names, 0);
  emit pendingSendFilePathChanged();
  emit pendingSendFileNameChanged();

  // Always (re)start send mode. Restarting discovery tears down any stalled
  // prior connection, which is the desired behavior when the user drops a new
  // file to send.
  if (state_.running())
  {
    startSendMode();
    state_.SetMode(QStringLiteral("Send"));
    emit modeChanged();
    startDiscoveryWatchdog();
  }

  const QString summary = paths.size() == 1
      ? names.first()
      : QStringLiteral("%1 files").arg(paths.size());
  setStatus(QStringLiteral("Discovery started. Choose a nearby device."));
  emit requestTrayMessage(
      QStringLiteral("Send mode"),
      QStringLiteral("Selected %1. Choose a nearby device to send.")
          .arg(summary));
}

void FileShareTrayController::sendPendingFileToTarget(qlonglong share_target_id) {
  // A fresh, user-initiated send cancels any auto-retry still waiting on an
  // earlier pick.
  clearSendRetry();
  doSendToTarget(share_target_id, /*is_retry=*/false);
}

void FileShareTrayController::doSendToTarget(qlonglong share_target_id,
                                             bool is_retry) {
  if (share_target_id <= 0) {
    return;
  }
  // A new send in flight supersedes any pending post-send auto-return.
  cancelSendReturnToReceive();

  // Text/link payload: send as a text attachment, skipping file validation.
  const QString pending_text = state_.pendingSendText();
  if (!pending_text.isEmpty()) {
    const QString target_name = state_.GetTargetName(share_target_id);
    state_.SetPendingSendTargetId(share_target_id);
    state_.AddOrUpdateTransfer(share_target_id, target_name,
                               QStringLiteral("Queued"), 0.0, 0,
                               QStringLiteral("outgoing"), pending_text,
                               QString(), 0.0, 0, 1);
    if (transfer_sweep_timer_ && !transfer_sweep_timer_->isActive()) {
      transfer_sweep_timer_->start();
    }
    emit transfersChanged();

    service_->SendText(
        share_target_id, pending_text.toStdString(),
        [this, share_target_id](NearbySharingApi::StatusCode status) {
          QMetaObject::invokeMethod(
              this,
              [this, share_target_id, status]() {
                const QString target_name = state_.GetTargetName(share_target_id);
                if (status == NearbySharingApi::StatusCode::kOk) {
                  setStatus(QStringLiteral("Sending to %1").arg(target_name));
                } else {
                  setStatus(QStringLiteral("Send failed"));
                  emit requestTrayMessage(
                      QStringLiteral("Send failed"),
                      QStringLiteral("Couldn't send to %1 — tap Retry.")
                          .arg(target_name));
                }
              },
              Qt::QueuedConnection);
        });
    return;
  }

  const QStringList file_paths = state_.pendingSendFilePaths();
  std::vector<std::string> valid_paths;
  for (const QString& path : file_paths) {
    const QFileInfo info(path);
    if (!path.isEmpty() && IsSendableFile(info)) {
      valid_paths.push_back(info.absoluteFilePath().toStdString());
    }
  }

  if (valid_paths.empty()) {
    setStatus(QStringLiteral("Selected file is not available"));
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            QStringLiteral("Selected file is not available."));
    return;
  }

  const QString target_name = state_.GetTargetName(share_target_id);
  state_.SetPendingSendFiles(state_.pendingSendFilePaths(),
                             state_.pendingSendFileNames(), share_target_id);

  // One transfer row represents the whole set; label reflects the count.
  const QString row_name = file_paths.size() == 1
      ? state_.pendingSendFileName()
      : QStringLiteral("%1 +%2 more")
            .arg(state_.pendingSendFileName())
            .arg(file_paths.size() - 1);
  state_.AddOrUpdateTransfer(share_target_id, target_name, QStringLiteral("Queued"), 0.0, 0,
                             QStringLiteral("outgoing"), row_name,
                             state_.pendingSendFilePath(), 0.0, 0,
                             static_cast<int>(file_paths.size()));
  if (transfer_sweep_timer_ && !transfer_sweep_timer_->isActive()) {
    transfer_sweep_timer_->start();
  }
  emit transfersChanged();

  service_->SendFiles(
      share_target_id, valid_paths,
      [this, share_target_id](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, share_target_id, status]() {
              pending_retry_inflight_ = false;
              const QString target_name = state_.GetTargetName(share_target_id);
              if (status == NearbySharingApi::StatusCode::kOk) {
                clearSendRetry();
                setStatus(QStringLiteral("Sending %1 to %2")
                              .arg(state_.pendingSendFileName(), target_name));
                return;
              }

              // The peer flaps its receivability (a fresh share_target id each
              // time), so a failure here is usually "the target just went away",
              // not a real error. Hold the files and re-fire when the same
              // device re-appears able to receive, instead of forcing the user
              // to close the sheet, re-select, and resend by hand.
              armSendRetry(share_target_id);
            },
            Qt::QueuedConnection);
      });
}

void FileShareTrayController::armSendRetry(qlonglong failed_target_id) {
  const QString device_id = device_id_by_target_.value(failed_target_id);
  const qlonglong now = QDateTime::currentMSecsSinceEpoch();

  // No stable id to re-resolve against, or we've waited past the deadline:
  // surface a real failure.
  const bool expired =
      !pending_retry_device_id_.isEmpty() && now >= pending_retry_deadline_ms_;
  if (device_id.isEmpty() || expired) {
    onSendRetryTimeout();
    return;
  }

  if (pending_retry_device_id_.isEmpty()) {
    // First failure: capture the file set and open the retry window.
    pending_retry_device_id_ = device_id;
    pending_retry_target_name_ = state_.GetTargetName(failed_target_id);
    pending_retry_paths_ = state_.pendingSendFilePaths();
    pending_retry_names_ = state_.pendingSendFileNames();
    pending_retry_row_id_ = failed_target_id;
    pending_retry_deadline_ms_ = now + kSendRetryTimeoutMs;
    if (pending_retry_timer_ == nullptr) {
      pending_retry_timer_ = new QTimer(this);
      pending_retry_timer_->setSingleShot(true);
      connect(pending_retry_timer_, &QTimer::timeout, this,
              &FileShareTrayController::onSendRetryTimeout);
    }
    pending_retry_timer_->start(kSendRetryTimeoutMs);
  }
  // Keep the row visible as still-working rather than flashing "Failed".
  state_.AddOrUpdateTransfer(
      pending_retry_row_id_, pending_retry_target_name_,
      QStringLiteral("Connecting"), 0.0, 0, QStringLiteral("outgoing"),
      state_.pendingSendFileName(), state_.pendingSendFilePath(), 0.0, 0, 0);
  emit transfersChanged();
}

void FileShareTrayController::maybeFireSendRetry(
    const NearbySharingApi::ShareTargetInfo& info) {
  if (pending_retry_device_id_.isEmpty() || pending_retry_inflight_) return;
  if (info.is_incoming || info.receive_disabled) return;
  if (StringUtils::FromStdString(info.device_id) != pending_retry_device_id_) {
    return;
  }
  // The device is reachable again under a fresh id: move the pending files and
  // holding row onto it and re-fire.
  pending_retry_inflight_ = true;
  state_.SetPendingSendFiles(pending_retry_paths_, pending_retry_names_, info.id);
  if (pending_retry_row_id_ != info.id) {
    state_.RemoveTransfer(pending_retry_row_id_);
    pending_retry_row_id_ = info.id;
  }
  doSendToTarget(info.id, /*is_retry=*/true);
}

void FileShareTrayController::onSendRetryTimeout() {
  if (pending_retry_row_id_ > 0) {
    const QString target_name = pending_retry_target_name_;
    emit requestTrayMessage(
        QStringLiteral("Send failed"),
        QStringLiteral("Could not reach %1").arg(target_name));
    state_.AddOrUpdateTransfer(pending_retry_row_id_, target_name,
                               QStringLiteral("Failed"), 0.0, 0,
                               QStringLiteral("outgoing"),
                               state_.pendingSendFileName(),
                               state_.pendingSendFilePath(), 0.0, 0, 0);
    emit transfersChanged();
  }
  state_.SetPendingSendFile("", "", 0);
  clearSendRetry();
}

void FileShareTrayController::clearSendRetry() {
  if (pending_retry_timer_ != nullptr) pending_retry_timer_->stop();
  pending_retry_device_id_.clear();
  pending_retry_target_name_.clear();
  pending_retry_paths_.clear();
  pending_retry_names_.clear();
  pending_retry_row_id_ = 0;
  pending_retry_deadline_ms_ = 0;
  pending_retry_inflight_ = false;
}

void FileShareTrayController::copyTextToClipboard(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  QClipboard* clipboard = QGuiApplication::clipboard();
  if (clipboard == nullptr) {
    emit requestTrayMessage(QStringLiteral("Copy failed"),
                            QStringLiteral("Clipboard is not available."));
    return;
  }

  clipboard->setText(trimmed, QClipboard::Clipboard);
  setStatus(QStringLiteral("Connection URL copied to clipboard"));
  emit requestTrayMessage(QStringLiteral("URL copied"),
                          QStringLiteral("Link copied to clipboard."));
}

void FileShareTrayController::openFileLocation(const QString& file_path) {
  const QString trimmed = file_path.trimmed();
  if (trimmed.isEmpty()) {
    emit requestTrayMessage(QStringLiteral("Open location failed"),
                            QStringLiteral("No received file location is available."));
    return;
  }

  QFileInfo info(trimmed);
  QString target_path;
  if (info.exists() && info.isFile()) {
    // Open the containing folder so the file is visible in the user's file
    // manager regardless of the desktop environment.
    target_path = info.absolutePath();
  } else if (info.exists() && info.isDir()) {
    target_path = info.absoluteFilePath();
  } else {
    // Some transfer updates can outlive the exact file entry we saw earlier;
    // fall back to the parent directory when it still exists.
    const QFileInfo parent_info(info.absolutePath());
    if (parent_info.exists() && parent_info.isDir()) {
      target_path = parent_info.absoluteFilePath();
    }
  }

  if (target_path.isEmpty()) {
    emit requestTrayMessage(QStringLiteral("Open location failed"),
                            QStringLiteral("The file location is no longer available."));
    return;
  }

  const bool opened =
      QDesktopServices::openUrl(QUrl::fromLocalFile(target_path));
  if (!opened) {
    emit requestTrayMessage(QStringLiteral("Open location failed"),
                            QStringLiteral("Could not open the file location."));
  }
}

void FileShareTrayController::clearTransfers() {
  state_.ClearAll();
  emit discoveredTargetsChanged();
  emit transfersChanged();
}

void FileShareTrayController::hideToTray() {
  // This is handled by the main window, but can be extended here if needed
}

void FileShareTrayController::acceptTransfer(qlonglong share_target_id) {
  if (service_) {
    service_->Accept(share_target_id, [](NearbySharingApi::StatusCode) {});
  }
}

void FileShareTrayController::declineTransfer(qlonglong share_target_id) {
  if (service_) {
    service_->Reject(share_target_id, [](NearbySharingApi::StatusCode) {});
  }
}

void FileShareTrayController::cancelTransfer(qlonglong share_target_id) {
  if (service_) {
    service_->Cancel(share_target_id, [](NearbySharingApi::StatusCode) {});
  }
}

void FileShareTrayController::clearTransfer(qlonglong share_target_id) {
  state_.RemoveTransfer(share_target_id);
  emit transfersChanged();
  emit discoveredTargetsChanged();
}

void FileShareTrayController::setStatus(const QString& status) {
  if (status == state_.statusMessage()) {
    return;
  }
  state_.SetStatusMessage(status);
  emit statusMessageChanged();
}

void FileShareTrayController::notifyStateChange(const QString& property) {
  if (property == QStringLiteral("mode")) {
    emit modeChanged();
  } else if (property == QStringLiteral("deviceName")) {
    emit deviceNameChanged();
  } else if (property == QStringLiteral("statusMessage")) {
    emit statusMessageChanged();
  } else if (property == QStringLiteral("running")) {
    emit runningChanged();
  } else if (property == QStringLiteral("autoAcceptIncoming")) {
    emit autoAcceptIncomingChanged();
  } else if (property == QStringLiteral("enable5GhzHotspot")) {
    emit enable5GhzHotspotChanged();
  } else if (property == QStringLiteral("hotspotBoost")) {
    emit hotspotBoostChanged();
  } else if (property == QStringLiteral("discoveredTargets")) {
    emit discoveredTargetsChanged();
  } else if (property == QStringLiteral("transfers")) {
    emit transfersChanged();
  }
}
