#include "quick_share_dbus.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QQuickWindow>

#include "file_share_tray_controller.h"

namespace {
constexpr char kServiceName[] = "io.github.ashpika40.QuickShare";
constexpr char kObjectPath[] = "/io/github/ashpika40/QuickShare";
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
