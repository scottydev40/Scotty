#ifndef SHARING_LINUX_QML_TRAY_APP_QUICK_SHARE_DBUS_H_
#define SHARING_LINUX_QML_TRAY_APP_QUICK_SHARE_DBUS_H_

#include <QObject>

class FileShareTrayController;
class QQuickWindow;

// Session-bus service that lets an external UI (e.g. a GNOME Shell Quick
// Settings extension) read and drive the app: query/set advertising
// visibility, surface the window, or quit. Registered on the session bus as
// io.github.ashpika40.QuickShare at /io/github/ashpika40/QuickShare.
//
// Methods and signals are Q_SCRIPTABLE and exported via
// ExportScriptableContents; state changes are pushed as signals so the
// extension can stay in sync without polling.
class QuickShareDbus : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "io.github.ashpika40.QuickShare")

 public:
  QuickShareDbus(FileShareTrayController* controller, QQuickWindow* window,
                 QObject* parent = nullptr);

  // Registers the service + object on the session bus. Returns false if the
  // name/object could not be claimed (e.g. already owned).
  bool registerOnBus();

 public slots:
  // Visibility: 0 = Everyone, 1 = Contacts, 2 = Hidden.
  Q_SCRIPTABLE int GetVisibility() const;
  Q_SCRIPTABLE void SetVisibility(int mode);
  Q_SCRIPTABLE bool GetRunning() const;
  Q_SCRIPTABLE QString GetDeviceName() const;
  // Surface (show + raise + focus) the main window.
  Q_SCRIPTABLE void Show();
  // Ordered shutdown + quit the process.
  Q_SCRIPTABLE void Quit();

 signals:
  Q_SCRIPTABLE void VisibilityChanged(int mode);
  Q_SCRIPTABLE void RunningChanged(bool running);

 private:
  FileShareTrayController* controller_;
  QQuickWindow* window_;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_QUICK_SHARE_DBUS_H_
