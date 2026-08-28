// Copyright 2026 The Scotty Authors
// Licensed under the Apache License, Version 2.0.
//
// A stand-in for FileShareTrayController that feeds the real QML UI fabricated,
// PII-free data so the app can be screenshotted in each state without a running
// Nearby engine, a real account, or a real hostname. The screen is chosen by the
// SCOTTY_DEMO env var: "home" (default), "send", or "receive".
#ifndef SCOTTY_LINUX_QML_TRAY_APP_DEMO_DEMO_CONTROLLER_H_
#define SCOTTY_LINUX_QML_TRAY_APP_DEMO_DEMO_CONTROLLER_H_

#include <QCoreApplication>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class DemoController : public QObject {
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
  Q_PROPERTY(int visibility READ visibility WRITE setVisibility NOTIFY visibilityChanged)
  Q_PROPERTY(QString signedInEmail READ signedInEmail NOTIFY signedInEmailChanged)
  Q_PROPERTY(QString signedInName READ signedInName NOTIFY signedInNameChanged)
  Q_PROPERTY(QString signedInPhotoPath READ signedInPhotoPath NOTIFY signedInPhotoPathChanged)
  Q_PROPERTY(bool mydevicesAvailable READ mydevicesAvailable NOTIFY mydevicesAvailableChanged)
  Q_PROPERTY(bool runAtStartup READ runAtStartup WRITE setRunAtStartup NOTIFY runAtStartupChanged)

 public:
  explicit DemoController(QObject* parent = nullptr) : QObject(parent) { seed(); }

  QString mode() const { return mode_; }
  QString deviceName() const { return device_name_; }
  void setDeviceName(const QString& v) { device_name_ = v; emit deviceNameChanged(); }
  QString statusMessage() const { return status_; }
  bool running() const { return true; }
  QString pendingSendFileName() const { return pending_name_; }
  QString pendingSendFilePath() const { return pending_path_; }
  QStringList pendingSendFileNames() const {
    return pending_path_.isEmpty() ? QStringList() : QStringList{pending_name_};
  }
  int pendingSendFileCount() const { return pending_path_.isEmpty() ? 0 : 1; }
  QVariantList discoveredTargets() const { return targets_; }
  QVariantList transfers() const { return transfers_; }
  bool transferActive() const { return !transfers_.isEmpty(); }
  bool autoAcceptIncoming() const { return auto_accept_; }
  void setAutoAcceptIncoming(bool v) { auto_accept_ = v; emit autoAcceptIncomingChanged(); }
  bool enable5GhzHotspot() const { return hotspot5_; }
  void setEnable5GhzHotspot(bool v) { hotspot5_ = v; emit enable5GhzHotspotChanged(); }
  bool hotspotBoost() const { return boost_; }
  void setHotspotBoost(bool v) { boost_ = v; emit hotspotBoostChanged(); }
  QString qrCodeUrl() const { return QString(); }
  QStringList qrCodeRows() const { return {}; }
  int qrCodeSize() const { return 0; }
  QString logPath() const { return QStringLiteral("~/.local/state/scotty/scotty.log"); }
  void setLogPath(const QString& v) { log_ = v; emit logPathChanged(); }
  QString savePath() const { return QStringLiteral("~/Downloads"); }
  void setSavePath(const QString& v) { save_ = v; emit savePathChanged(); }
  bool developerMode() const { return dev_; }
  void setDeveloperMode(bool v) { dev_ = v; emit developerModeChanged(); }
  int visibility() const { return visibility_; }
  void setVisibility(int v) { visibility_ = v; emit visibilityChanged(); }
  QString signedInEmail() const { return email_; }
  QString signedInName() const { return name_; }
  QString signedInPhotoPath() const { return QString(); }  // letter-fallback avatar, no face
  bool mydevicesAvailable() const { return true; }
  bool runAtStartup() const { return run_startup_; }
  void setRunAtStartup(bool v) { run_startup_ = v; emit runAtStartupChanged(); }

  Q_INVOKABLE void start() {}
  Q_INVOKABLE void stop() {}
  Q_INVOKABLE void quitApplication() { qApp->quit(); }
  Q_INVOKABLE void switchToReceiveMode() {
    pending_path_.clear(); pending_name_.clear(); mode_ = QStringLiteral("Receive");
    emit pendingSendFilePathChanged(); emit modeChanged();
  }
  Q_INVOKABLE void switchToSendModeWithFile(const QString&) { enterSend(); }
  Q_INVOKABLE void switchToSendModeWithFiles(const QStringList&) { enterSend(); }
  Q_INVOKABLE void sendPendingFileToTarget(qlonglong) {}
  Q_INVOKABLE void requestMyDevicesSignIn() {}
  Q_INVOKABLE void signOutMyDevices() {
    email_.clear(); name_.clear(); emit signedInEmailChanged(); emit signedInNameChanged();
  }
  Q_INVOKABLE void rescanDevices() {}
  Q_INVOKABLE void hardReset() {}
  Q_INVOKABLE void copyTextToClipboard(const QString&) {}
  Q_INVOKABLE void openFileLocation(const QString&) {}
  Q_INVOKABLE void clearTransfers() {
    transfers_.clear(); emit transfersChanged(); emit transferActiveChanged();
  }
  Q_INVOKABLE void hideToTray() {}
  Q_INVOKABLE void setReceiveForeground(bool) {}
  Q_INVOKABLE void acceptTransfer(qlonglong) {}
  Q_INVOKABLE void declineTransfer(qlonglong) {}
  Q_INVOKABLE void cancelTransfer(qlonglong) {}
  Q_INVOKABLE void clearTransfer(qlonglong) {}

 signals:
  void modeChanged();
  void deviceNameChanged();
  void statusMessageChanged();
  void runningChanged();
  void pendingSendFileNameChanged();
  void pendingSendFilePathChanged();
  void discoveredTargetsChanged();
  void transfersChanged();
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
  void signedInEmailChanged();
  void signedInNameChanged();
  void signedInPhotoPathChanged();
  void mydevicesAvailableChanged();
  void runAtStartupChanged();

 private:
  // deviceType: 1/5 phone, 2 tablet, 3 laptop, else generic (see DeviceRow.qml).
  static QVariantMap target(qlonglong id, const QString& name, int type) {
    QVariantMap m;
    m[QStringLiteral("id")] = id;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("deviceType")] = type;
    return m;
  }
  void makeTargets() {
    // Star Trek easter egg. Montgomery "Scotty" Scott would approve.
    targets_ = {
        target(1, QStringLiteral("Pike's Laptop"), 3),
        target(2, QStringLiteral("Kirk's Pixel 10"), 1),
        target(3, QStringLiteral("Chromebook"), 2),
    };
  }
  void enterSend() {
    pending_path_ = QStringLiteral("/home/demo/away_mission.mp4");
    pending_name_ = QStringLiteral("away_mission.mp4");
    mode_ = QStringLiteral("Send");
    makeTargets();
    emit pendingSendFilePathChanged();
    emit modeChanged();
    emit discoveredTargetsChanged();
  }
  void seed() {
    device_name_ = QStringLiteral("linux-pc");
    email_ = QStringLiteral("scotty@enterprise");  // drives a letter-"S" avatar, no photo
    name_ = QStringLiteral("Scotty");
    visibility_ = 0;  // Everyone
    const QByteArray screen = qgetenv("SCOTTY_DEMO");
    if (screen == "send") {
      enterSend();
    } else if (screen == "receive") {
      QVariantMap tr;
      tr[QStringLiteral("targetId")] = qlonglong(2);
      tr[QStringLiteral("targetName")] = QStringLiteral("Kirk's Pixel 10");
      tr[QStringLiteral("direction")] = QStringLiteral("incoming");
      tr[QStringLiteral("status")] = QStringLiteral("Transferring");
      tr[QStringLiteral("progress")] = 0.68;
      tr[QStringLiteral("fileName")] = QStringLiteral("away_mission.mp4");
      tr[QStringLiteral("filePath")] = QString();
      tr[QStringLiteral("speed")] = 0;
      tr[QStringLiteral("currentFile")] = 1;
      tr[QStringLiteral("totalFiles")] = 1;
      transfers_ = {tr};
      mode_ = QStringLiteral("Receive");
    } else {
      mode_ = QStringLiteral("Receive");  // home / idle
    }
  }

  QString mode_ = QStringLiteral("Receive");
  QString device_name_, status_, pending_name_, pending_path_, email_, name_, log_, save_;
  QVariantList targets_, transfers_;
  int visibility_ = 0;
  bool auto_accept_ = false, hotspot5_ = false, boost_ = false, dev_ = false, run_startup_ = true;
};

#endif  // SCOTTY_LINUX_QML_TRAY_APP_DEMO_DEMO_CONTROLLER_H_
