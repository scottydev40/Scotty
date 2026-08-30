#ifndef SCOTTY_LINUX_QML_TRAY_APP_UPDATE_CHECKER_H_
#define SCOTTY_LINUX_QML_TRAY_APP_UPDATE_CHECKER_H_

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Checks github.com/scottydev40/Scotty for a newer release and, when the build
// is a self-contained AppImage, downloads + verifies + swaps it in place.
//
// Channels: the "beta" toggle (persisted in QSettings) decides whether beta
// tags are considered. Stable-vs-beta is read from the tag name, because the
// releases are not reliably flagged `prerelease` on the API.
//
// Self-update is only offered when running as an AppImage ($APPIMAGE set) AND
// the release asset carries a sha256 `digest` we can verify. A .deb install, or
// a release without a digest, falls back to "open the release page".
//
// Exposed to QML as the `updateChecker` context property.
class UpdateChecker : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
  Q_PROPERTY(bool betaChannel READ betaChannel WRITE setBetaChannel NOTIFY
                 betaChannelChanged)
  // True when this build can update itself in place: an AppImage (download +
  // swap) or a flatpak (`flatpak update` on the host).
  Q_PROPERTY(bool canSelfUpdate READ canSelfUpdate CONSTANT)
  Q_PROPERTY(Status status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QString availableVersion READ availableVersion NOTIFY statusChanged)
  Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY statusChanged)
  Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY statusChanged)
  // 0..100 during a download, else -1.
  Q_PROPERTY(int downloadProgress READ downloadProgress NOTIFY
                 downloadProgressChanged)

 public:
  enum class Status {
    Idle,
    Checking,
    UpToDate,
    UpdateAvailable,
    Downloading,
    ReadyToRelaunch,
    Failed,
  };
  Q_ENUM(Status)

  explicit UpdateChecker(QObject* parent = nullptr);
  ~UpdateChecker() override;

  QString currentVersion() const { return current_version_; }
  bool betaChannel() const { return beta_channel_; }
  void setBetaChannel(bool on);
  bool canSelfUpdate() const;
  Status status() const { return status_; }
  QString statusText() const { return status_text_; }
  QString availableVersion() const { return available_version_; }
  QString releaseNotes() const { return release_notes_; }
  QString releaseUrl() const { return release_url_; }
  int downloadProgress() const { return download_progress_; }

 public slots:
  // Queries the releases API and updates status. No-op while already busy.
  void checkForUpdates();
  // Downloads the verified AppImage and swaps it in; only valid when
  // status()==UpdateAvailable and canSelfUpdate() and a digest is known.
  void downloadAndInstall();
  // Opens the release page in the user's browser (the .deb / no-digest path).
  void openReleasePage();

 signals:
  void betaChannelChanged();
  void statusChanged();
  void downloadProgressChanged();

 private:
  void onReleasesReply(QNetworkReply* reply);
  void onDownloadFinished(QNetworkReply* reply);
  void setStatus(Status status, const QString& text);
  void finishInstall(const QString& downloaded_path);
  // Flatpak self-update: `flatpak update` on the host (via flatpak-spawn), then
  // relaunch. OSTree/GPG handles integrity, so no digest/download here.
  void installViaFlatpak();

  QNetworkAccessManager* nam_;
  QString current_version_;
  bool beta_channel_ = false;

  Status status_ = Status::Idle;
  QString status_text_;
  QString available_version_;
  QString release_notes_;
  QString release_url_;
  // Chosen AppImage asset for self-update (empty if none verifiable).
  QString asset_url_;
  QString asset_sha256_;  // lowercase hex, no "sha256:" prefix
  int download_progress_ = -1;
};

#endif  // SCOTTY_LINUX_QML_TRAY_APP_UPDATE_CHECKER_H_
