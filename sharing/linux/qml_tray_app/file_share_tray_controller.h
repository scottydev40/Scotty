#ifndef SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_
#define SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_

#include <QObject>
#include <memory>

#include "file_share_state.h"
#include <sharing/linux/nearby_sharing_api.h>

using NearbySharingApi = nearby::sharing::NearbySharingApi;

class FileShareTrayController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
  Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(QString pendingSendFileName READ pendingSendFileName NOTIFY pendingSendFileNameChanged)
  Q_PROPERTY(QString pendingSendFilePath READ pendingSendFilePath NOTIFY pendingSendFilePathChanged)
  Q_PROPERTY(QStringList pendingSendFileNames READ pendingSendFileNames NOTIFY pendingSendFilePathChanged)
  Q_PROPERTY(int pendingSendFileCount READ pendingSendFileCount NOTIFY pendingSendFilePathChanged)
  Q_PROPERTY(QVariantList discoveredTargets READ discoveredTargets NOTIFY discoveredTargetsChanged)
  Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
  Q_PROPERTY(bool autoAcceptIncoming READ autoAcceptIncoming WRITE setAutoAcceptIncoming NOTIFY autoAcceptIncomingChanged)
  Q_PROPERTY(bool enable5GhzHotspot READ enable5GhzHotspot WRITE setEnable5GhzHotspot NOTIFY enable5GhzHotspotChanged)
  Q_PROPERTY(QString qrCodeUrl READ qrCodeUrl NOTIFY qrCodeUrlChanged)
  Q_PROPERTY(QStringList qrCodeRows READ qrCodeRows NOTIFY qrCodeChanged)
  Q_PROPERTY(int qrCodeSize READ qrCodeSize NOTIFY qrCodeChanged)
  Q_PROPERTY(QString logPath READ logPath WRITE setLogPath NOTIFY logPathChanged)
  Q_PROPERTY(QString savePath READ savePath WRITE setSavePath NOTIFY savePathChanged)
  Q_PROPERTY(bool developerMode READ developerMode WRITE setDeveloperMode NOTIFY developerModeChanged)
  // Advertising visibility for receive mode: 0 = Everyone, 1 = Contacts, 2 = Hidden.
  Q_PROPERTY(int visibility READ visibility WRITE setVisibility NOTIFY visibilityChanged)
  // Launch the app on login (managed via an XDG autostart .desktop entry).
  Q_PROPERTY(bool runAtStartup READ runAtStartup WRITE setRunAtStartup NOTIFY runAtStartupChanged)

 public:
  explicit FileShareTrayController(QObject* parent = nullptr);
  ~FileShareTrayController() override;

  // Property accessors
  QString mode() const { return state_.mode(); }
  QString deviceName() const { return state_.deviceName(); }
  QString statusMessage() const { return state_.statusMessage(); }
  bool running() const { return state_.running(); }
  QString pendingSendFileName() const { return state_.pendingSendFileName(); }
  QString pendingSendFilePath() const { return state_.pendingSendFilePath(); }
  QStringList pendingSendFileNames() const { return state_.pendingSendFileNames(); }
  int pendingSendFileCount() const { return state_.pendingSendFileCount(); }
  QVariantList discoveredTargets() const { return state_.discoveredTargets(); }
  QVariantList transfers() const { return state_.transfers(); }
  bool autoAcceptIncoming() const { return state_.autoAcceptIncoming(); }
  bool enable5GhzHotspot() const { return state_.enable5GhzHotspot(); }
  QString qrCodeUrl() const { return state_.qrCodeUrl(); }
  QStringList qrCodeRows() const { return state_.qrCodeRows(); }
  int qrCodeSize() const { return state_.qrCodeSize(); }
  QString logPath() const { return state_.logPath(); }
  QString savePath() const { return state_.savePath(); }
  bool developerMode() const { return state_.developerMode(); }
  int visibility() const { return visibility_; }
  bool runAtStartup() const;

  // Public methods
  void setDeviceName(const QString& device_name);
  void setAutoAcceptIncoming(bool enabled);
  void setEnable5GhzHotspot(bool enabled);
  void setLogPath(const QString& path);
  void setSavePath(const QString& path);
  void setDeveloperMode(bool enabled);
  void setVisibility(int mode);
  void setRunAtStartup(bool enabled);

  Q_INVOKABLE void start();
  Q_INVOKABLE void stop();
  // Quit the whole application (ordered shutdown runs via aboutToQuit->stop()).
  Q_INVOKABLE void quitApplication();
  Q_INVOKABLE void switchToReceiveMode();
  Q_INVOKABLE void switchToSendModeWithFile(const QString& file_path);
  Q_INVOKABLE void switchToSendModeWithFiles(const QStringList& file_paths);
  Q_INVOKABLE void sendPendingFileToTarget(qlonglong share_target_id);
  Q_INVOKABLE void copyTextToClipboard(const QString& text);
  Q_INVOKABLE void openFileLocation(const QString& file_path);
  Q_INVOKABLE void clearTransfers();
  Q_INVOKABLE void hideToTray();
  // Follows the window: a visible window advertises at high power (which drags
  // in Bluetooth Classic and renames the adapter), hidden drops to BLE/Wi-Fi
  // only. Still discoverable either way — visibility governs that.
  Q_INVOKABLE void setReceiveForeground(bool foreground);
  // Incoming-transfer decisions / cancel (used by DeviceRow when auto-accept is off
  // or a transfer is in progress).
  Q_INVOKABLE void acceptTransfer(qlonglong share_target_id);
  Q_INVOKABLE void declineTransfer(qlonglong share_target_id);
  Q_INVOKABLE void cancelTransfer(qlonglong share_target_id);
  // Dismiss a single finished/failed transfer without wiping the whole list.
  Q_INVOKABLE void clearTransfer(qlonglong share_target_id);

 signals:
  void modeChanged();
  void deviceNameChanged();
  void statusMessageChanged();
  void runningChanged();
  void pendingSendFileNameChanged();
  void pendingSendFilePathChanged();
  void discoveredTargetsChanged();
  void transfersChanged();
  void autoAcceptIncomingChanged();
  void enable5GhzHotspotChanged();
  void qrCodeUrlChanged();
  void qrCodeChanged();
  void logPathChanged();
  void savePathChanged();
  void developerModeChanged();
  void visibilityChanged();
  void runAtStartupChanged();

  // An incoming transfer needs an answer and auto-accept is off.
  void requestIncomingDecision(qlonglong share_target_id,
                               const QString& device_name,
                               const QString& file_name);
  // That request has been settled; drop any prompt still showing.
  void dismissIncomingDecision(qlonglong share_target_id);

  void requestTrayMessage(const QString& title, const QString& body);
  void requestCopyLinkTrayMessage(const QString& title, const QString& body,
                                   const QString& link);

 private:
  void initializeService();
  // Keeps an existing autostart entry in sync with the current format.
  void refreshAutostartFile();
  void attachServiceListeners();
  void loadSettings();
  void saveSettings() const;
  void updateQrCodeData();

  void startSendMode();
  void startReceiveMode();

  // Default location for received files when none is configured.
  static QString defaultSavePath();
  // Normalize (~ expansion, absolute, cleaned), create, and verify the folder
  // is writable. Falls back to defaultSavePath() if raw is empty/invalid.
  QString resolveSavePath(const QString& raw) const;
  
  void updateTargetFromInfo(const NearbySharingApi::ShareTargetInfo& info);
  void handleTransferUpdate(const NearbySharingApi::TransferUpdateInfo& update);
  void handleTransferComplete(const NearbySharingApi::TransferUpdateInfo& update);
  void handleIncomingTransferComplete(const NearbySharingApi::TransferUpdateInfo& update,
                                      const QString& name, bool success);
  void handleOutgoingTransferComplete(const NearbySharingApi::TransferUpdateInfo& update,
                                      const QString& name, bool success);
  
  void setStatus(const QString& status);
  void notifyStateChange(const QString& property);

  std::unique_ptr<NearbySharingApi> service_;
  FileShareState state_;
  // Advertising visibility: 0 = Everyone, 1 = Contacts, 2 = Hidden.
  int visibility_ = 0;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_
