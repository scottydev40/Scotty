#ifndef SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_STATE_H_
#define SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_STATE_H_

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include "share_target_model.h"
#include "transfer_model.h"

class FileShareState {
 public:
  FileShareState();

  // Getters
  QString mode() const { return mode_; }
  QString deviceName() const { return device_name_; }
  QString statusMessage() const { return status_message_; }
  bool running() const { return running_; }
  bool autoAcceptIncoming() const { return auto_accept_incoming_; }
  bool enable5GhzHotspot() const { return enable_5ghz_hotspot_; }
  bool hotspotBoost() const { return hotspot_boost_; }

  QString pendingSendFileName() const { return pending_send_file_name_; }
  QString pendingSendFilePath() const { return pending_send_file_path_; }
  qlonglong pendingSendTargetId() const { return pending_send_target_id_; }

  // Full staged set for multi-file send. The singular getters above return the
  // first entry so existing single-file bindings keep working.
  QStringList pendingSendFilePaths() const { return pending_send_file_paths_; }
  QStringList pendingSendFileNames() const { return pending_send_file_names_; }
  int pendingSendFileCount() const { return pending_send_file_paths_.size(); }

  QVariantList discoveredTargets() const { return discovered_targets_; }
  QVariantList transfers() const { return transfers_; }

  QString qrCodeUrl() const { return qr_code_url_; }
  QStringList qrCodeRows() const { return qr_code_rows_; }
  int qrCodeSize() const { return qr_code_size_; }

  QString logPath() const { return log_path_; }
  QString savePath() const { return save_path_; }
  bool developerMode() const { return developer_mode_; }

  // Setters
  void SetMode(const QString& mode) { mode_ = mode; }
  void SetDeviceName(const QString& name) { device_name_ = name; }
  void SetStatusMessage(const QString& message) { status_message_ = message; }
  void SetRunning(bool running) { running_ = running; }
  void SetAutoAcceptIncoming(bool enabled) { auto_accept_incoming_ = enabled; }
  void SetEnable5GhzHotspot(bool enabled) { enable_5ghz_hotspot_ = enabled; }
  void SetHotspotBoost(bool enabled) { hotspot_boost_ = enabled; }

  void SetPendingSendFile(const QString& file_path, const QString& file_name,
                          qlonglong target_id) {
    pending_send_file_path_ = file_path;
    pending_send_file_name_ = file_name;
    pending_send_target_id_ = target_id;
    if (file_path.isEmpty()) {
      pending_send_file_paths_.clear();
      pending_send_file_names_.clear();
    } else {
      pending_send_file_paths_ = QStringList{file_path};
      pending_send_file_names_ = QStringList{file_name};
    }
  }

  void SetPendingSendFiles(const QStringList& file_paths,
                           const QStringList& file_names,
                           qlonglong target_id) {
    pending_send_file_paths_ = file_paths;
    pending_send_file_names_ = file_names;
    pending_send_file_path_ = file_paths.isEmpty() ? QString() : file_paths.first();
    pending_send_file_name_ = file_names.isEmpty() ? QString() : file_names.first();
    pending_send_target_id_ = target_id;
  }

  void ClearPendingSendFile() {
    pending_send_file_path_.clear();
    pending_send_file_name_.clear();
    pending_send_file_paths_.clear();
    pending_send_file_names_.clear();
    pending_send_target_id_ = 0;
  }

  // Keep the staged file, drop only the active target. Lets the user stay in
  // send mode after a transfer and pick another device.
  void ClearPendingSendTarget() { pending_send_target_id_ = 0; }

  void SetQrCodeData(const QString& url, const QStringList& rows, int size) {
    qr_code_url_ = url;
    qr_code_rows_ = rows;
    qr_code_size_ = size;
  }

  void SetLogPath(const QString& path) { log_path_ = path; }
  void SetSavePath(const QString& path) { save_path_ = path; }
  void SetDeveloperMode(bool enabled) { developer_mode_ = enabled; }

  // Target management
  // device_type: 0=unknown/other, and the Nearby ShareTargetType values (e.g.
  // phone/tablet/laptop). Pass -1 to leave an existing target's type unchanged
  // (used by transfer-update callers that don't carry a device type).
  void AddOrUpdateTarget(qlonglong id, const QString& name, bool is_incoming,
                         int device_type = -1);
  void RemoveTarget(qlonglong id);
  // Removes every discovered target (each via RemoveTarget, so a target with an
  // active transfer is deferred rather than yanked). Used when the discovery
  // context changes wholesale — e.g. sign-out invalidates certificate-resolved
  // targets that the engine can no longer rebuild.
  void ClearTargets();
  QString GetTargetName(qlonglong id) const;
  bool HasTarget(qlonglong id) const;

  // Transfer management
  void AddOrUpdateTransfer(qlonglong target_id, const QString& target_name,
                           const QString& status, double progress,
                           qulonglong transferred_bytes,
                           const QString& direction, const QString& file_name,
                           const QString& file_path, double speed_bytes_per_sec,
                           int current_file, int total_files);
  void RemoveTransfer(qlonglong target_id);
  bool HasActiveTransferForTarget(qlonglong target_id) const;
  bool HasActiveTransfers() const;
  void ClearFinishedTransfers();
  // Drops finished rows whose "endedAt" is older than ttl_ms. Returns true if
  // anything was removed (so the caller can emit transfersChanged). Active and
  // awaiting rows are always kept regardless of age.
  bool SweepExpiredTransfers(qlonglong now_ms, qlonglong ttl_ms);

  // Pending target removal management
  void AddPendingTargetRemoval(qlonglong id);
  void RemovePendingTargetRemoval(qlonglong id);
  bool IsPendingTargetRemoval(qlonglong id) const;

  void ClearAll();

 private:
  QString mode_ = QStringLiteral("Receive");
  QString device_name_ = QStringLiteral("NearbyLinux");
  QString status_message_ = QStringLiteral("Idle");
  bool running_ = false;
  bool auto_accept_incoming_ = true;
  bool enable_5ghz_hotspot_ = true;
  bool hotspot_boost_ = false;

  // QR Code
  QString qr_code_url_;
  QStringList qr_code_rows_;
  int qr_code_size_ = 0;
  QString log_path_ = QStringLiteral("/tmp/nearby_qml_file_tray.log");
  QString save_path_;
  bool developer_mode_ = false;

  // Pending send state
  QString pending_send_file_path_;
  QString pending_send_file_name_;
  QStringList pending_send_file_paths_;
  QStringList pending_send_file_names_;
  qlonglong pending_send_target_id_ = 0;

  // Discovered targets
  QVariantList discovered_targets_;
  QHash<qlonglong, int> discovered_row_by_target_;
  QHash<qlonglong, QString> target_names_;

  // Transfers
  QVariantList transfers_;
  QHash<qlonglong, int> transfer_row_by_target_;

  // Pending removals
  QSet<qlonglong> pending_target_removals_;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_STATE_H_
