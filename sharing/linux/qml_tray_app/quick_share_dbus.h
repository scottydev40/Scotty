#ifndef SHARING_LINUX_QML_TRAY_APP_QUICK_SHARE_DBUS_H_
#define SHARING_LINUX_QML_TRAY_APP_QUICK_SHARE_DBUS_H_

#include <QObject>
#include <QString>

class QDBusServiceWatcher;
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

  // True while an external tile (the GNOME Quick Settings extension) is live
  // and driving the app. Deliberately not persisted: it describes the current
  // session, and the default (false) makes the tray icon show for desktops
  // that have no tile at all.
  bool tileActive() const { return tile_active_; }

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
  // Announced by the Quick Settings extension: true when it enables (it owns
  // the UI, so the app hides its tray icon), false when it disables or the
  // shell goes away (the tray icon comes back so there is always a way out).
  Q_SCRIPTABLE void SetTileActive(bool active);
  Q_SCRIPTABLE bool GetTileActive() const;

 signals:
  Q_SCRIPTABLE void VisibilityChanged(int mode);
  Q_SCRIPTABLE void RunningChanged(bool running);
  Q_SCRIPTABLE void TileActiveChanged(bool active);

 private:
  FileShareTrayController* controller_;
  QQuickWindow* window_;
  // Only ever set by SetTileActive(), plus a forced reset when gnome-shell
  // drops off the bus without disabling the extension first (crash/restart).
  bool tile_active_ = false;
  QDBusServiceWatcher* shell_watcher_ = nullptr;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_QUICK_SHARE_DBUS_H_
