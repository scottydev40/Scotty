#ifndef SHARING_LINUX_QML_TRAY_APP_FREEDESKTOP_APPLICATION_ADAPTOR_H_
#define SHARING_LINUX_QML_TRAY_APP_FREEDESKTOP_APPLICATION_ADAPTOR_H_

#include <QDBusAbstractAdaptor>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QuickShareDbus;

// Standard org.freedesktop.Application surface used by DBusActivatable
// desktop entries. The Scotty-specific API remains on dev.scotty.Scotty at
// the same object path; this adaptor only handles desktop activation/open.
class FreedesktopApplicationAdaptor final : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

 public:
  explicit FreedesktopApplicationAdaptor(QuickShareDbus* application);

 public slots:
  Q_SCRIPTABLE void Activate(const QVariantMap& platform_data);
  Q_SCRIPTABLE void Open(const QStringList& uris,
                         const QVariantMap& platform_data);
  Q_SCRIPTABLE void ActivateAction(const QString& action_name,
                                   const QVariantList& parameter,
                                   const QVariantMap& platform_data);

 private:
  QuickShareDbus* application_;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_FREEDESKTOP_APPLICATION_ADAPTOR_H_
