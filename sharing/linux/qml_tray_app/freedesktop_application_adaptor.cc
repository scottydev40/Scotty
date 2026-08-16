#include "freedesktop_application_adaptor.h"

#include <QUrl>

#include "quick_share_dbus.h"

FreedesktopApplicationAdaptor::FreedesktopApplicationAdaptor(
    QuickShareDbus* application)
    : QDBusAbstractAdaptor(application), application_(application) {}

void FreedesktopApplicationAdaptor::Activate(
    const QVariantMap& /*platform_data*/) {
  application_->Show();
}

void FreedesktopApplicationAdaptor::Open(
    const QStringList& uris, const QVariantMap& /*platform_data*/) {
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
    const QVariantMap& /*platform_data*/) {
  if (action_name == QStringLiteral("quit")) {
    application_->Quit();
    return;
  }
  application_->Show();
}
