#include "file_share_tray_controller.h"

#include <chrono>
#include <future>
#include <iostream>
#include <QClipboard>
#include <QDesktopServices>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QCoreApplication>
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
  refreshAutostartFile();
  initializeService();
}

FileShareTrayController::~FileShareTrayController() {
  stop();
  if (service_) {
    // Shutdown() is asynchronous: it posts a task to the service thread that
    // still dereferences the platform/context (e.g. StopFastInitiationAdvertising
    // -> GetFastInitiationManager). If we let service_ (and the platform it owns)
    // destruct before that task runs, the task makes a pure-virtual call on the
    // half-destroyed platform and aborts. Block until the shutdown callback fires
    // (bounded) so teardown is ordered.
    std::promise<void> shutdown_done;
    std::future<void> done = shutdown_done.get_future();
    service_->Shutdown([&shutdown_done](NearbySharingApi::StatusCode) {
      shutdown_done.set_value();
    });
    done.wait_for(std::chrono::seconds(5));
  }
}

void FileShareTrayController::initializeService() {
  service_ = std::make_unique<NearbySharingApi>(state_.deviceName().toStdString());
  service_->Set5GhzHotspotEnabled(state_.enable5GhzHotspot());
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
  state_.AddOrUpdateTarget(info.id, name, info.is_incoming, info.device_type);
  emit discoveredTargetsChanged();
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

  state_.AddOrUpdateTransfer(update.share_target_id, name, status, update.progress,
                             update.transferred_bytes, direction, file_name,
                             StringUtils::FromStdString(update.first_file_path));
  emit transfersChanged();

  setStatus(QStringLiteral("%1 (%2)").arg(status, name));

  // Auto-accept incoming transfers if enabled
  if (update.status == NearbySharingApi::TransferStatus::kAwaitingLocalConfirmation &&
      state_.autoAcceptIncoming()) {
    service_->Accept(update.share_target_id, [](NearbySharingApi::StatusCode) {});
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
}

void FileShareTrayController::handleIncomingTransferComplete(
    const NearbySharingApi::TransferUpdateInfo& update, const QString& name,
    bool success) {
  if (!success) {
    emit requestTrayMessage(
        QStringLiteral("Receive failed"),
        QStringLiteral("Transfer from %1 failed").arg(name));
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
  } else {
    emit requestTrayMessage(QStringLiteral("Send failed"),
                            QStringLiteral("%1 failed to send to %2").arg(file_name, name));
  }
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
      settings.value(QStringLiteral("autoAcceptIncoming"), true).toBool();
  state_.SetAutoAcceptIncoming(stored_auto_accept);

  const bool stored_enable_5ghz_hotspot =
      settings.value(QStringLiteral("enable5GhzHotspot"), true).toBool();
  state_.SetEnable5GhzHotspot(stored_enable_5ghz_hotspot);

  const QString stored_log_path =
      settings.value(QStringLiteral("logPath"), QStringLiteral("/tmp/nearby_qml_file_tray.log"))
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

  const int stored_visibility =
      settings.value(QStringLiteral("visibility"), 0).toInt();
  visibility_ = (stored_visibility >= 0 && stored_visibility <= 2)
                    ? stored_visibility
                    : 0;
}

void FileShareTrayController::saveSettings() const {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  settings.setValue(QStringLiteral("deviceName"), state_.deviceName());
  settings.setValue(QStringLiteral("autoAcceptIncoming"), state_.autoAcceptIncoming());
  settings.setValue(QStringLiteral("enable5GhzHotspot"),
                    state_.enable5GhzHotspot());
  settings.setValue(QStringLiteral("logPath"), state_.logPath());
  settings.setValue(QStringLiteral("savePath"), state_.savePath());
  settings.setValue(QStringLiteral("developerMode"), state_.developerMode());
  settings.setValue(QStringLiteral("visibility"), visibility_);
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
    stop();
    initializeService();
    start();
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

void FileShareTrayController::setVisibility(int mode) {
  if (mode < 0 || mode > 2 || mode == visibility_) {
    return;
  }
  visibility_ = mode;
  // Applies live if the service is receiving; otherwise takes effect on next
  // StartReceiveMode (see NearbySharingApi::SetVisibility).
  if (service_) {
    service_->SetVisibility(mode);
  }
  saveSettings();
  emit visibilityChanged();
}

static QString AutostartFilePath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
         QStringLiteral("/autostart/nearby-file-share.desktop");
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
      << "Name=Nearby File Share\n"
      << "Exec=\"" << QCoreApplication::applicationFilePath()
      << "\" --background\n"
      << "Icon=nearby-file-share\n"
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
  // Rewrite in place so entries written by an older build (no --background, or
  // the earlier --hidden spelling, or an unquoted Exec path) pick up the
  // current format.
  if (runAtStartup()) {
    WriteAutostartFile();
  }
}

void FileShareTrayController::setReceiveForeground(bool foreground) {
  if (!service_) {
    return;
  }
  service_->SetReceiveForeground(foreground,
                                 [](NearbySharingApi::StatusCode) {});
}

void FileShareTrayController::quitApplication() {
  // aboutToQuit is wired to stop() in main() for an ordered teardown.
  QCoreApplication::quit();
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
  return QDir::homePath() + QStringLiteral("/Downloads/QuickShare");
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

  service_->StopSendMode([](NearbySharingApi::StatusCode) {});
  service_->StopReceiveMode([](NearbySharingApi::StatusCode) {});

  state_.ClearAll();
  emit discoveredTargetsChanged();
  emit transfersChanged();

  setStatus(QStringLiteral("Stopped"));
}

void FileShareTrayController::startSendMode() {
  service_->StopReceiveMode([this](NearbySharingApi::StatusCode status) {
    if (status == NearbySharingApi::StatusCode::kOk ||
        status == NearbySharingApi::StatusCode::kStatusAlreadyStopped) {
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
    }
  });
}

void FileShareTrayController::startReceiveMode() {
  service_->StopSendMode([this](NearbySharingApi::StatusCode status) {
    if (status == NearbySharingApi::StatusCode::kOk ||
        status == NearbySharingApi::StatusCode::kStatusAlreadyStopped) {
      service_->StartReceiveMode([this](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, status]() {
              setStatus(QStringLiteral("StartReceiveMode: %1")
                            .arg(StatusMapper::ApiStatusToString(status)));
              if (status != NearbySharingApi::StatusCode::kOk) {
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
  // Drop the staged file so the UI (driven by pendingSendFilePath) returns
  // to the receive blob.
  state_.ClearPendingSendFile();
  emit pendingSendFilePathChanged();
  emit pendingSendFileNameChanged();

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

// A sendable file: the transport needs a real, non-empty size, and a single
// zero-byte entry fails the *whole* batch (NearbySharingApi::SendFiles returns
// kInvalidArgument for the set), so they are dropped rather than passed on.
static bool IsSendableFile(const QFileInfo& info) {
  return info.exists() && info.isFile() && info.size() > 0;
}

// Desktop cruft nobody means to share. Dotfiles and hidden directories are
// already excluded by the iterator's filters; these are the ones that are not
// hidden on Linux (Windows marks them hidden by attribute instead) plus editor
// backups. Only applied when expanding a folder — explicitly selecting one of
// these files still sends it.
static bool IsJunkFile(const QString& name) {
  static const QStringList kJunk = {
      QStringLiteral("thumbs.db"),    QStringLiteral("ehthumbs.db"),
      QStringLiteral("desktop.ini"),  QStringLiteral(".ds_store"),
      QStringLiteral(".directory"),
  };
  return name.endsWith(QLatin1Char('~')) || kJunk.contains(name.toLower());
}

// Quick Share has no folder attachment type, so a selected folder contributes
// the files inside it — the same thing Android does when you share a folder.
// Directory structure is not preserved; the receiver gets a flat set.
static void CollectSendableFiles(const QString& root, QStringList* out) {
  // No QDir::Hidden: dotfiles are skipped, and the iterator will not descend
  // into hidden directories either, so .git/.cache never get walked.
  // Symlinked directories are not followed — they can form cycles.
  QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (IsSendableFile(it.fileInfo()) && !IsJunkFile(it.fileName())) {
      out->append(it.fileInfo().absoluteFilePath());
    }
  }
}

void FileShareTrayController::switchToSendModeWithFiles(
    const QStringList& file_paths) {
  QStringList paths;
  QStringList names;
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
      QStringList expanded;
      CollectSendableFiles(info.absoluteFilePath(), &expanded);
      for (const QString& path : expanded) {
        paths.append(path);
        names.append(QFileInfo(path).fileName());
      }
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
  if (share_target_id <= 0) {
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
                             state_.pendingSendFilePath());
  emit transfersChanged();

  service_->SendFiles(
      share_target_id, valid_paths,
      [this, share_target_id](NearbySharingApi::StatusCode status) {
        QMetaObject::invokeMethod(
            this,
            [this, share_target_id, status]() {
              const QString target_name = state_.GetTargetName(share_target_id);
              if (status == NearbySharingApi::StatusCode::kOk) {
                setStatus(QStringLiteral("Sending %1 to %2")
                              .arg(state_.pendingSendFileName(), target_name));
                return;
              }

              emit requestTrayMessage(
                  QStringLiteral("Send failed"),
                  QStringLiteral("Could not send to %1").arg(target_name));

              state_.AddOrUpdateTransfer(share_target_id, target_name,
                                         QStringLiteral("Failed"), 0.0, 0,
                                         QStringLiteral("outgoing"),
                                         state_.pendingSendFileName(),
                                         state_.pendingSendFilePath());
              emit transfersChanged();
              state_.SetPendingSendFile("", "", 0);
            },
            Qt::QueuedConnection);
      });
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
  } else if (property == QStringLiteral("discoveredTargets")) {
    emit discoveredTargetsChanged();
  } else if (property == QStringLiteral("transfers")) {
    emit transfersChanged();
  }
}
