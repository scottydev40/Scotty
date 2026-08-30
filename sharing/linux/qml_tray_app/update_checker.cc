#include "update_checker.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#include <QCoreApplication>

#include <cstdio>  // std::rename

#include "scotty_version.h"
#include "version_compare.h"

namespace {

constexpr char kSettingsBetaKey[] = "updates/betaChannel";

QString AppImagePath() {
  return qEnvironmentVariable("APPIMAGE");  // empty unless running as AppImage
}

bool InFlatpak() {
  return QFileInfo::exists(QStringLiteral("/.flatpak-info"));
}

// A release tag is "beta" if the API flags it prerelease OR the tag name says
// so (the repo does not reliably set the prerelease flag).
bool TagIsBeta(const QString& tag, bool api_prerelease) {
  return api_prerelease || tag.contains("beta", Qt::CaseInsensitive);
}

}  // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent),
      nam_(new QNetworkAccessManager(this)),
      current_version_(QString::fromUtf8(SCOTTY_VERSION_TAG)) {
  QSettings settings(QStringLiteral("Nearby"),
                     QStringLiteral("QmlFileTrayApp"));
  beta_channel_ = settings.value(QString::fromLatin1(kSettingsBetaKey),
                                 /*default=*/false)
                      .toBool();
}

UpdateChecker::~UpdateChecker() = default;

bool UpdateChecker::canSelfUpdate() const {
  return !AppImagePath().isEmpty() || InFlatpak();
}

void UpdateChecker::setBetaChannel(bool on) {
  if (beta_channel_ == on) return;
  beta_channel_ = on;
  QSettings settings(QStringLiteral("Nearby"),
                     QStringLiteral("QmlFileTrayApp"));
  settings.setValue(QString::fromLatin1(kSettingsBetaKey), on);
  emit betaChannelChanged();
}

void UpdateChecker::setStatus(Status status, const QString& text) {
  status_ = status;
  status_text_ = text;
  emit statusChanged();
}

void UpdateChecker::checkForUpdates() {
  if (status_ == Status::Checking || status_ == Status::Downloading) return;
  setStatus(Status::Checking, tr("Checking for updates…"));

  QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=30")
               .arg(QString::fromUtf8(SCOTTY_UPDATE_REPO)));
  QNetworkRequest req(url);
  req.setRawHeader("Accept", "application/vnd.github+json");
  // GitHub rejects requests without a User-Agent.
  req.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("Scotty-UpdateChecker/%1").arg(current_version_));
  QNetworkReply* reply = nam_->get(req);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply] { onReleasesReply(reply); });
}

void UpdateChecker::onReleasesReply(QNetworkReply* reply) {
  reply->deleteLater();
  if (reply->error() != QNetworkReply::NoError) {
    setStatus(Status::Failed,
              tr("Update check failed: %1").arg(reply->errorString()));
    return;
  }

  QJsonParseError perr;
  const QJsonDocument doc =
      QJsonDocument::fromJson(reply->readAll(), &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
    setStatus(Status::Failed, tr("Update check failed: bad response"));
    return;
  }

  const std::string current = current_version_.toStdString();
  QString best_tag;
  QJsonObject best_release;

  for (const QJsonValue& v : doc.array()) {
    const QJsonObject rel = v.toObject();
    if (rel.value("draft").toBool()) continue;
    const QString tag = rel.value("tag_name").toString();
    const bool is_beta =
        TagIsBeta(tag, rel.value("prerelease").toBool());
    // Channel filter: stable channel ignores betas.
    if (is_beta && !beta_channel_) continue;
    if (!scotty::ParseTag(tag.toStdString()).valid) continue;

    if (best_tag.isEmpty() ||
        scotty::CompareVersions(scotty::ParseTag(tag.toStdString()),
                                scotty::ParseTag(best_tag.toStdString())) > 0) {
      best_tag = tag;
      best_release = rel;
    }
  }

  if (best_tag.isEmpty() || !scotty::IsNewer(best_tag.toStdString(), current)) {
    available_version_.clear();
    release_notes_.clear();
    release_url_.clear();
    asset_url_.clear();
    asset_sha256_.clear();
    setStatus(Status::UpToDate,
              tr("You're up to date (%1).").arg(current_version_));
    return;
  }

  // Found a newer release. Record notes + page, and look for a verifiable
  // AppImage asset.
  available_version_ = best_tag;
  release_notes_ = best_release.value("body").toString();
  release_url_ = best_release.value("html_url").toString();
  asset_url_.clear();
  asset_sha256_.clear();

  for (const QJsonValue& av : best_release.value("assets").toArray()) {
    const QJsonObject asset = av.toObject();
    if (asset.value("name").toString() !=
        QString::fromUtf8(SCOTTY_APPIMAGE_ASSET)) {
      continue;
    }
    asset_url_ = asset.value("browser_download_url").toString();
    // GitHub returns "sha256:<hex>" on the asset when available.
    const QString digest = asset.value("digest").toString();
    if (digest.startsWith("sha256:")) {
      asset_sha256_ = digest.mid(7).toLower();
    }
    break;
  }

  // Flatpak updates through OSTree, so it needs no GitHub AppImage asset; the
  // AppImage path still requires a verifiable download.
  const bool can_install =
      InFlatpak() ||
      (canSelfUpdate() && !asset_url_.isEmpty() && !asset_sha256_.isEmpty());
  if (can_install) {
    setStatus(Status::UpdateAvailable,
              tr("Update available: %1").arg(best_tag));
  } else {
    // Newer version exists but we can't safely self-install it here.
    setStatus(Status::UpdateAvailable,
              tr("Update available: %1 — open the release page to install.")
                  .arg(best_tag));
  }
}

void UpdateChecker::downloadAndInstall() {
  if (status_ != Status::UpdateAvailable) return;

  // Flatpak: hand off to `flatpak update`; OSTree/GPG verify, no GitHub asset.
  if (InFlatpak()) {
    installViaFlatpak();
    return;
  }

  if (AppImagePath().isEmpty() || asset_url_.isEmpty() || asset_sha256_.isEmpty()) {
    // No verifiable self-update path; send them to the page instead.
    openReleasePage();
    return;
  }

  download_progress_ = 0;
  emit downloadProgressChanged();
  setStatus(Status::Downloading, tr("Downloading %1…").arg(available_version_));

  QNetworkRequest req{QUrl(asset_url_)};
  req.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("Scotty-UpdateChecker/%1").arg(current_version_));
  // QNetworkAccessManager follows redirects safely by default in Qt6.
  QNetworkReply* reply = nam_->get(req);
  connect(reply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            download_progress_ =
                (total > 0) ? static_cast<int>(received * 100 / total) : 0;
            emit downloadProgressChanged();
          });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply] { onDownloadFinished(reply); });
}

void UpdateChecker::onDownloadFinished(QNetworkReply* reply) {
  reply->deleteLater();
  download_progress_ = -1;
  emit downloadProgressChanged();

  if (reply->error() != QNetworkReply::NoError) {
    setStatus(Status::Failed,
              tr("Download failed: %1").arg(reply->errorString()));
    return;
  }

  const QByteArray body = reply->readAll();

  // Verify the sha256 digest before we ever touch the running file.
  const QByteArray got =
      QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex();
  if (QString::fromLatin1(got).toLower() != asset_sha256_) {
    setStatus(Status::Failed,
              tr("Update rejected: checksum mismatch. The running app was not "
                 "changed."));
    return;
  }

  // Write to a temp file in the SAME directory as the AppImage so the final
  // rename is atomic (same filesystem).
  const QString appimage = AppImagePath();
  const QFileInfo info(appimage);
  const QString tmp_path =
      info.absolutePath() + QStringLiteral("/.scotty-update-") +
      available_version_ + QStringLiteral(".AppImage.part");

  QFile tmp(tmp_path);
  if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    setStatus(Status::Failed,
              tr("Update failed: cannot write to %1").arg(info.absolutePath()));
    return;
  }
  if (tmp.write(body) != body.size()) {
    tmp.close();
    tmp.remove();
    setStatus(Status::Failed, tr("Update failed: short write"));
    return;
  }
  tmp.close();
  finishInstall(tmp_path);
}

void UpdateChecker::finishInstall(const QString& downloaded_path) {
  const QString appimage = AppImagePath();

  // Make it executable (rwxr-xr-x).
  QFile::setPermissions(downloaded_path,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                            QFileDevice::ExeGroup | QFileDevice::ReadOther |
                            QFileDevice::ExeOther);

  // Atomically replace the running AppImage. POSIX rename() overwrites the
  // destination in one step (Linux allows renaming over a file that is
  // currently executing), so there is no window with no app in place.
  if (std::rename(downloaded_path.toLocal8Bit().constData(),
                  appimage.toLocal8Bit().constData()) != 0) {
    QFile::remove(downloaded_path);
    setStatus(Status::Failed,
              tr("Update failed while replacing the app. Your current version "
                 "still works."));
    return;
  }

  setStatus(Status::ReadyToRelaunch,
            tr("Updated to %1 — restarting…").arg(available_version_));
  // Launch the new AppImage detached, then quit this one.
  QProcess::startDetached(appimage, {});
  QCoreApplication::quit();
}

void UpdateChecker::installViaFlatpak() {
  setStatus(Status::Downloading, tr("Updating via Flatpak…"));
  download_progress_ = -1;  // indeterminate: flatpak prints its own progress
  emit downloadProgressChanged();

  auto* proc = new QProcess(this);
  proc->setProcessChannelMode(QProcess::MergedChannels);
  connect(
      proc,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
      [this, proc](int exit_code, QProcess::ExitStatus exit_status) {
        const QString output = QString::fromUtf8(proc->readAll());
        proc->deleteLater();
        if (exit_status != QProcess::NormalExit || exit_code != 0) {
          setStatus(Status::Failed,
                    tr("Flatpak update failed. Your current version still "
                       "works."));
          return;
        }
        // "flatpak update" is a no-op when already current; the deploy only
        // takes effect on a fresh launch, so relaunch through the host.
        setStatus(Status::ReadyToRelaunch,
                  tr("Updated to %1 — restarting…").arg(available_version_));
        QProcess::startDetached(
            QStringLiteral("flatpak-spawn"),
            {QStringLiteral("--host"), QStringLiteral("flatpak"),
             QStringLiteral("run"), QStringLiteral("dev.scotty.Scotty")});
        QCoreApplication::quit();
      });

  // Run on the host: the sandbox can neither see nor update its own deployment.
  proc->start(QStringLiteral("flatpak-spawn"),
              {QStringLiteral("--host"), QStringLiteral("flatpak"),
               QStringLiteral("update"), QStringLiteral("-y"),
               QStringLiteral("dev.scotty.Scotty")});
}

void UpdateChecker::openReleasePage() {
  const QString url =
      release_url_.isEmpty()
          ? QStringLiteral("https://github.com/%1/releases/latest")
                .arg(QString::fromUtf8(SCOTTY_UPDATE_REPO))
          : release_url_;
  QDesktopServices::openUrl(QUrl(url));
}
