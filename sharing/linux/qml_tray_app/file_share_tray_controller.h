#ifndef SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_
#define SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_

#include <QObject>
#include <QHash>
#include <memory>

#include "file_share_state.h"

class QTimer;
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
  Q_PROPERTY(bool transferActive READ transferActive NOTIFY transferActiveChanged)
  Q_PROPERTY(bool autoAcceptIncoming READ autoAcceptIncoming WRITE setAutoAcceptIncoming NOTIFY autoAcceptIncomingChanged)
  Q_PROPERTY(bool enable5GhzHotspot READ enable5GhzHotspot WRITE setEnable5GhzHotspot NOTIFY enable5GhzHotspotChanged)
  Q_PROPERTY(bool hotspotBoost READ hotspotBoost WRITE setHotspotBoost NOTIFY hotspotBoostChanged)
  Q_PROPERTY(QString qrCodeUrl READ qrCodeUrl NOTIFY qrCodeUrlChanged)
  Q_PROPERTY(QStringList qrCodeRows READ qrCodeRows NOTIFY qrCodeChanged)
  Q_PROPERTY(int qrCodeSize READ qrCodeSize NOTIFY qrCodeChanged)
  Q_PROPERTY(QString logPath READ logPath WRITE setLogPath NOTIFY logPathChanged)
  Q_PROPERTY(QString savePath READ savePath WRITE setSavePath NOTIFY savePathChanged)
  Q_PROPERTY(bool developerMode READ developerMode WRITE setDeveloperMode NOTIFY developerModeChanged)
  // Advertising visibility for receive mode: 0 = Everyone, 1 = Contacts, 2 = Hidden.
  Q_PROPERTY(int visibility READ visibility WRITE setVisibility NOTIFY visibilityChanged)
  Q_PROPERTY(QString signedInEmail READ signedInEmail NOTIFY signedInEmailChanged)
  Q_PROPERTY(QString signedInName READ signedInName NOTIFY signedInNameChanged)
  Q_PROPERTY(QString signedInPhotoPath READ signedInPhotoPath NOTIFY signedInPhotoPathChanged)
  // Whether the opt-in My-Devices plugin (dev.scotty.MyDevices1) is present
  // (installed/activatable). The core links no grey code; this only gates the
  // Contacts / Your-devices UI and the Sign-in affordance.
  Q_PROPERTY(bool mydevicesAvailable READ mydevicesAvailable NOTIFY mydevicesAvailableChanged)
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
  // True while any row is in a non-terminal (in-progress) state.
  bool transferActive() const { return state_.HasActiveTransfers(); }
  bool autoAcceptIncoming() const { return state_.autoAcceptIncoming(); }
  bool enable5GhzHotspot() const { return state_.enable5GhzHotspot(); }
  bool hotspotBoost() const { return state_.hotspotBoost(); }
  QString qrCodeUrl() const { return state_.qrCodeUrl(); }
  QStringList qrCodeRows() const { return state_.qrCodeRows(); }
  int qrCodeSize() const { return state_.qrCodeSize(); }
  QString logPath() const { return state_.logPath(); }
  QString savePath() const { return state_.savePath(); }
  bool developerMode() const { return state_.developerMode(); }
  int visibility() const { return visibility_; }
  QString signedInEmail() const { return signed_in_email_; }
  QString signedInName() const { return signed_in_name_; }
  QString signedInPhotoPath() const { return signed_in_photo_path_; }
  bool mydevicesAvailable() const { return mydevices_available_; }
  bool runAtStartup() const;

  // Public methods
  void setDeviceName(const QString& device_name);
  void setAutoAcceptIncoming(bool enabled);
  void setEnable5GhzHotspot(bool enabled);
  void setHotspotBoost(bool enabled);
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
  // Triggers the opt-in plugin's sign-in flow over D-Bus (StartSignIn). The
  // plugin owns the WebView / token / RPC; the core never touches grey code.
  Q_INVOKABLE void requestMyDevicesSignIn();
  // Signs out via the plugin (SignOut) and reverts to Everyone / No-one.
  Q_INVOKABLE void signOutMyDevices();
  // Force a fresh discovery cycle in send mode (re-registers the send surface)
  // when a target went stale and the list stopped updating.
  Q_INVOKABLE void rescanDevices();
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
  // Edge-triggered: fires only when transferActive() flips, not on every row
  // update. Feeds the D-Bus TransferActiveChanged signal / panel indicator.
  void transferActiveChanged();
  void autoAcceptIncomingChanged();
  void enable5GhzHotspotChanged();
  void hotspotBoostChanged();
  void qrCodeUrlChanged();
  void qrCodeChanged();
  void logPathChanged();
  void savePathChanged();
  void developerModeChanged();
  void visibilityChanged();
  void runAtStartupChanged();
  void signedInEmailChanged();
  void signedInNameChanged();
  void signedInPhotoPathChanged();
  void mydevicesAvailableChanged();

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

  // My-Devices plugin (dev.scotty.MyDevices1) over session D-Bus. The core
  // holds no grey code; it only reads account state and triggers sign-in.
  void setupMyDevicesBus();
  void refreshMyDevicesAvailability();
  void refreshMyDevicesAccount();
  void refreshMyDevicesProfile();
  void setSignedInEmail(const QString& email);

 private slots:
  void onMyDevicesAccountChanged(bool signed_in, const QString& email);
  void onMyDevicesOwnerChanged(const QString& service, const QString& old_owner,
                               const QString& new_owner);

 private:
  std::unique_ptr<NearbySharingApi> service_;
  QString signed_in_email_;
  QString signed_in_name_;
  QString signed_in_photo_path_;
  bool mydevices_available_ = false;
  FileShareState state_;
  // Per-target throughput tracking: last observed byte count + timestamp, and
  // the smoothed rate we report. Keyed by share_target_id.
  struct SpeedSample {
    qulonglong bytes = 0;
    qlonglong ms = 0;
    double bps = 0.0;
  };
  QHash<qlonglong, SpeedSample> speed_samples_;
  // Ticks while transfers exist, expiring finished rows a few seconds after
  // they end so completed entries fade instead of piling up.
  QTimer* transfer_sweep_timer_ = nullptr;
  // Self-heals send-mode discovery: while looking for a device (send mode, no
  // active transfer), re-cycles the send surface every ~10 s so the list
  // reflects current availability and recovers if the service quietly dropped
  // discovery. Runs only in send mode; never fires during a transfer.
  QTimer* discovery_watchdog_timer_ = nullptr;
  // Consecutive empty (no-target) watchdog ticks; capped so the self-heal can't
  // thrash the stack forever when a peer simply isn't reachable.
  int discovery_watchdog_empty_ticks_ = 0;
  static constexpr int kDiscoveryWatchdogMaxEmptyTicks = 6;  // ~60 s, then stop
  void startDiscoveryWatchdog();
  void stopDiscoveryWatchdog();
  void onDiscoveryWatchdogTick();
  // Advertising visibility: 0 = Everyone, 1 = Contacts, 2 = Hidden.
  int visibility_ = 0;
  // Whether the main window is on screen; kept in sync by setReceiveForeground.
  bool window_visible_ = true;
  // Last emitted value of transferActive(), so transferActiveChanged only fires
  // on a real edge rather than on every transfersChanged.
  bool last_transfer_active_ = false;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_FILE_SHARE_TRAY_CONTROLLER_H_
