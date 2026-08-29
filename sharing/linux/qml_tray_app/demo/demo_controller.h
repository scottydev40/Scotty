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
#include <QTimer>
#include <QVariantList>
#include <QVector>
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
  QString qrCodeUrl() const { return QStringLiteral("https://quickshare.google/qr#scotty-demo"); }
  QStringList qrCodeRows() const { return qr_rows_; }
  int qrCodeSize() const { return qr_rows_.isEmpty() ? 0 : qr_rows_.size(); }
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
  Q_INVOKABLE void sendPendingFileToTarget(qlonglong id) {
    QString target_name = QStringLiteral("Device");
    for (const QVariant& t : targets_) {
      const QVariantMap m = t.toMap();
      if (m.value(QStringLiteral("id")).toLongLong() == id) {
        target_name = m.value(QStringLiteral("name")).toString();
        break;
      }
    }
    QVariantMap tr;
    tr[QStringLiteral("targetId")] = id;
    tr[QStringLiteral("targetName")] = target_name;
    tr[QStringLiteral("direction")] = QStringLiteral("outgoing");
    tr[QStringLiteral("status")] = QStringLiteral("Connecting");
    tr[QStringLiteral("progress")] = 0.0;
    tr[QStringLiteral("fileName")] = pending_name_;
    tr[QStringLiteral("filePath")] = pending_path_;
    tr[QStringLiteral("speed")] = 0;
    tr[QStringLiteral("currentFile")] = 1;
    tr[QStringLiteral("totalFiles")] = 1;
    transfers_ = {tr};
    emit transfersChanged();
    emit transferActiveChanged();

    if (!send_timer_) {
      send_timer_ = new QTimer(this);
      connect(send_timer_, &QTimer::timeout, this, &DemoController::tickSend);
    }
    send_progress_ = 0.0;
    send_timer_->start(120);
  }
  Q_INVOKABLE void tickSend() {
    if (transfers_.isEmpty()) { send_timer_->stop(); return; }
    QVariantMap tr = transfers_.first().toMap();
    send_progress_ += 0.025;
    if (send_progress_ >= 1.0) {
      send_progress_ = 1.0;
      tr[QStringLiteral("status")] = QStringLiteral("Complete");
      send_timer_->stop();
    } else {
      tr[QStringLiteral("status")] = QStringLiteral("InProgress");
      // ~14 MB/s, fabricated.
      tr[QStringLiteral("speed")] = qlonglong(14 * 1024 * 1024);
    }
    tr[QStringLiteral("progress")] = send_progress_;
    transfers_ = {tr};
    emit transfersChanged();
  }
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
  // trust: "own" (your cert), "contact" (contact cert), "stranger" (Everyone).
  static QVariantMap target(qlonglong id, const QString& name, int type,
                            const QString& trust) {
    QVariantMap m;
    m[QStringLiteral("id")] = id;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("deviceType")] = type;
    m[QStringLiteral("trust")] = trust;
    return m;
  }
  void makeTargets() {
    // Star Trek easter egg. Montgomery "Scotty" Scott would approve.
    targets_ = {
        target(1, QStringLiteral("Galaxy S26 Ultra"), 1, QStringLiteral("own")),
        target(2, QStringLiteral("office-pc"), 3, QStringLiteral("own")),
        target(3, QStringLiteral("Spock"), 1, QStringLiteral("contact")),
        target(4, QStringLiteral("Uhura's iPad"), 2, QStringLiteral("contact")),
        target(5, QStringLiteral("Pike's Laptop"), 3, QStringLiteral("stranger")),
        target(6, QStringLiteral("Kirk's Pixel 10"), 1, QStringLiteral("stranger")),
        target(7, QStringLiteral("Chromebook"), 2, QStringLiteral("stranger")),
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
      tr[QStringLiteral("status")] = QStringLiteral("InProgress");
      tr[QStringLiteral("progress")] = 0.68;
      tr[QStringLiteral("speed")] = qlonglong(14 * 1024 * 1024);
      tr[QStringLiteral("fileName")] = QStringLiteral("away_mission.mp4");
      tr[QStringLiteral("filePath")] = QString();
      tr[QStringLiteral("currentFile")] = 1;
      tr[QStringLiteral("totalFiles")] = 1;
      transfers_ = {tr};
      mode_ = QStringLiteral("Receive");
    } else {
      mode_ = QStringLiteral("Receive");  // home / idle
    }
    makeDummyQr();
  }
  // A fabricated 25x25 QR-looking matrix: three finder squares + a deterministic
  // pseudo-random data field. Not a real code; just for screenshots.
  void makeDummyQr() {
    const int n = 25;
    QVector<QVector<char>> g(n, QVector<char>(n, '0'));
    auto finder = [&](int r0, int c0) {
      for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 7; ++c) {
          bool ring = (r == 0 || r == 6 || c == 0 || c == 6);
          bool core = (r >= 2 && r <= 4 && c >= 2 && c <= 4);
          g[r0 + r][c0 + c] = (ring || core) ? '1' : '0';
        }
    };
    finder(0, 0);
    finder(0, n - 7);
    finder(n - 7, 0);
    // Timing patterns.
    for (int i = 8; i < n - 8; ++i) {
      g[6][i] = (i % 2 == 0) ? '1' : '0';
      g[i][6] = (i % 2 == 0) ? '1' : '0';
    }
    // Deterministic data fill outside the reserved zones.
    quint32 x = 0x5c077ea1u;
    for (int r = 0; r < n; ++r)
      for (int c = 0; c < n; ++c) {
        bool reserved = (r < 8 && c < 8) || (r < 8 && c >= n - 8) ||
                        (r >= n - 8 && c < 8) || r == 6 || c == 6;
        if (reserved) continue;
        x = x * 1103515245u + 12345u;
        g[r][c] = ((x >> 16) & 1) ? '1' : '0';
      }
    qr_rows_.clear();
    for (int r = 0; r < n; ++r) {
      QString row;
      for (int c = 0; c < n; ++c) row.append(QChar(g[r][c]));
      qr_rows_.append(row);
    }
  }

  QString mode_ = QStringLiteral("Receive");
  QString device_name_, status_, pending_name_, pending_path_, email_, name_, log_, save_;
  QVariantList targets_, transfers_;
  QStringList qr_rows_;
  QTimer* send_timer_ = nullptr;
  double send_progress_ = 0.0;
  int visibility_ = 0;
  bool auto_accept_ = false, hotspot5_ = false, boost_ = false, dev_ = false, run_startup_ = true;
};

#endif  // SCOTTY_LINUX_QML_TRAY_APP_DEMO_DEMO_CONTROLLER_H_
