#include "freedesktop_application_adaptor.h"

#include <QByteArray>
#include <QUrl>
#include <QtGlobal>

#include "quick_share_dbus.h"

namespace {

// A DBusActivatable launch carries the desktop environment's startup handshake
// in platform_data: on Wayland an xdg-activation token, on X11 a startup-id.
// GNOME shows the "launching" spinner cursor until that startup sequence is
// completed by the window it produced. Hand the token to Qt via the env vars
// it reads when it maps/activates a window, so showing the window finishes the
// handshake and the spinner stops instead of spinning to its timeout.
void ConsumeActivationToken(const QVariantMap& platform_data) {
  const auto wayland = platform_data.find(QStringLiteral("activation-token"));
  if (wayland != platform_data.end() && !wayland->toString().isEmpty()) {
    qputenv("XDG_ACTIVATION_TOKEN", wayland->toString().toUtf8());
  }
  const auto x11 = platform_data.find(QStringLiteral("desktop-startup-id"));
  if (x11 != platform_data.end() && !x11->toString().isEmpty()) {
    qputenv("DESKTOP_STARTUP_ID", x11->toString().toUtf8());
  }
}

}  // namespace

FreedesktopApplicationAdaptor::FreedesktopApplicationAdaptor(
    QuickShareDbus* application)
    : QDBusAbstractAdaptor(application), application_(application) {}

void FreedesktopApplicationAdaptor::Activate(
    const QVariantMap& platform_data) {
  ConsumeActivationToken(platform_data);
  application_->Show();
}

void FreedesktopApplicationAdaptor::Open(
    const QStringList& uris, const QVariantMap& platform_data) {
  ConsumeActivationToken(platform_data);
  QStringList paths;
  for (const QString& value : uris) {
    const QUrl url(value);
    if (url.isLocalFile()) {
      paths.push_back(url.toLocalFile());
    }
  }
  application_->OpenFiles(paths);
}

void FreedesktopApplicationAdaptor::ActivateAction(
    const QString& action_name, const QVariantList& /*parameter*/,
    const QVariantMap& platform_data) {
  ConsumeActivationToken(platform_data);
  if (action_name == QStringLiteral("quit")) {
    application_->Quit();
    return;
  }
  application_->Show();
}
