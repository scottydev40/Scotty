#include "quick_share_dbus.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QQuickWindow>

#include "file_share_tray_controller.h"

namespace {
constexpr char kServiceName[] = "dev.scotty.Scotty";
constexpr char kObjectPath[] = "/dev/scotty/Scotty";
// The only process that can host the Quick Settings tile.
constexpr char kShellService[] = "org.gnome.Shell";
}  // namespace

QuickShareDbus::QuickShareDbus(FileShareTrayController* controller,
                               QQuickWindow* window, QObject* parent)
    : QObject(parent), controller_(controller), window_(window) {
  // Re-broadcast controller state changes onto the bus so the extension stays
  // in sync without polling.
  connect(controller_, &FileShareTrayController::visibilityChanged, this,
          [this]() { emit VisibilityChanged(controller_->visibility()); });
  connect(controller_, &FileShareTrayController::runningChanged, this,
          [this]() { emit RunningChanged(controller_->running()); });
  connect(controller_, &FileShareTrayController::transferActiveChanged, this,
          [this]() { emit TransferActiveChanged(controller_->transferActive()); });

  // If gnome-shell dies, its extension never gets to call SetTileActive(false),
  // so clear the flag here — otherwise the tray icon would stay hidden with no
  // tile left to replace it.
  shell_watcher_ = new QDBusServiceWatcher(
      QString::fromLatin1(kShellService), QDBusConnection::sessionBus(),
      QDBusServiceWatcher::WatchForUnregistration, this);
  connect(shell_watcher_, &QDBusServiceWatcher::serviceUnregistered, this,
          [this]() { SetTileActive(false); });
}

bool QuickShareDbus::registerOnBus() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.registerObject(QString::fromLatin1(kObjectPath), this,
                          QDBusConnection::ExportScriptableContents)) {
    return false;
  }
  return bus.registerService(QString::fromLatin1(kServiceName));
}

int QuickShareDbus::GetVisibility() const { return controller_->visibility(); }

void QuickShareDbus::SetVisibility(int mode) {
  controller_->setVisibility(mode);
}

bool QuickShareDbus::GetRunning() const { return controller_->running(); }

bool QuickShareDbus::GetTransferActive() const {
  return controller_->transferActive();
}

QString QuickShareDbus::GetDeviceName() const {
  return controller_->deviceName();
}

void QuickShareDbus::Show() {
  if (window_ == nullptr) {
    return;
  }
  window_->show();
  window_->raise();
  window_->requestActivate();
}

void QuickShareDbus::Quit() {
  controller_->stop();
  QCoreApplication::quit();
}

void QuickShareDbus::SetTileActive(bool active) {
  if (active == tile_active_) {
    return;
  }
  tile_active_ = active;
  emit TileActiveChanged(tile_active_);
}

bool QuickShareDbus::GetTileActive() const { return tile_active_; }
