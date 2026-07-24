#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QUrl>

#include <fcntl.h>
#include <unistd.h>

#include "bluetooth_name_guard.h"
#include "file_share_tray_controller.h"
#include "notification_manager.h"
#include "quick_share_dbus.h"
#include "theme_controller.h"

namespace {

constexpr char kDefaultLogPath[] = "/tmp/nearby_qml_file_tray.log";
// Per-user single-instance key (QLocalServer socket name).
constexpr char kInstanceKey[] = "nearby_qml_file_tray_app.instance";

// Wire format for the single-instance socket: a verb line, then one path per
// line. "SHOW" alone just surfaces the window; "SEND" is followed by the paths
// to share.
//
// Returns true if another instance took the message, meaning this process
// should exit immediately.
bool ForwardToRunningInstance(const QByteArray& payload) {
  QLocalSocket probe;
  probe.connectToServer(QString::fromLatin1(kInstanceKey));
  if (!probe.waitForConnected(200)) {
    return false;
  }
  probe.write(payload);
  probe.flush();
  probe.waitForBytesWritten(500);
  probe.disconnectFromServer();
  if (probe.state() != QLocalSocket::UnconnectedState) {
    probe.waitForDisconnected(200);
  }
  return true;
}

// File managers pass plain paths for %F, but accept file:// URIs too so the
// entry also works with %U.
QString NormalizePathArgument(const QString& argument) {
  if (argument.startsWith(QStringLiteral("file://"))) {
    return QUrl(argument).toLocalFile();
  }
  return argument;
}

// Everything following --send, up to the next flag.
QStringList ParseSendPaths(const QStringList& arguments) {
  QStringList paths;
  const int flag = arguments.indexOf(QStringLiteral("--send"));
  if (flag < 0) {
    return paths;
  }
  for (int i = flag + 1; i < arguments.size(); ++i) {
    if (arguments.at(i).startsWith(QStringLiteral("--"))) {
      break;
    }
    const QString path = NormalizePathArgument(arguments.at(i));
    if (!path.isEmpty()) {
      paths.append(path);
    }
  }
  return paths;
}

bool EnsureLogDirectory(const QString& file_path) {
  const QFileInfo file_info(file_path);
  QDir directory = file_info.absoluteDir();
  if (directory.exists()) {
    return true;
  }
  return directory.mkpath(QStringLiteral("."));
}

bool RedirectStdStreamsToFile(const QString& file_path) {
  const QByteArray encoded_path = QFile::encodeName(file_path);
  const int fd = ::open(encoded_path.constData(), O_CREAT | O_APPEND | O_WRONLY, 0644);
  if (fd < 0) {
    return false;
  }

  const bool redirected_stdout = ::dup2(fd, STDOUT_FILENO) >= 0;
  const bool redirected_stderr = ::dup2(fd, STDERR_FILENO) >= 0;
  ::close(fd);
  return redirected_stdout && redirected_stderr;
}

QString ResolveConfiguredLogPath() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  const QString configured_path =
      settings.value(QStringLiteral("logPath"),
                     QString::fromLatin1(kDefaultLogPath))
          .toString()
          .trimmed();
  if (configured_path.isEmpty()) {
    return QString::fromLatin1(kDefaultLogPath);
  }
  return configured_path;
}

void RedirectProcessLogsToConfiguredPath() {
  QString log_path = ResolveConfiguredLogPath();
  if (EnsureLogDirectory(log_path) && RedirectStdStreamsToFile(log_path)) {
    return;
  }

  const QString fallback_path = QString::fromLatin1(kDefaultLogPath);
  if (log_path == fallback_path) {
    return;
  }
  if (!EnsureLogDirectory(fallback_path)) {
    return;
  }
  RedirectStdStreamsToFile(fallback_path);
}

QIcon BuildTintedSymbolicIcon(const QString& source, const QColor& color) {
  QIcon source_icon(source);
  if (source_icon.isNull()) {
    return QIcon();
  }

  QIcon tinted_icon;
  for (int size : {16, 18, 20, 22, 24, 32, 40, 48, 64}) {
    QPixmap pixmap = source_icon.pixmap(size, size);
    if (pixmap.isNull()) {
      continue;
    }

    QPixmap tinted(pixmap.size());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();

    tinted_icon.addPixmap(tinted);
  }

  return tinted_icon;
}

}  // namespace

int main(int argc, char* argv[]) {
  RedirectProcessLogsToConfiguredPath();

  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  app.setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon.svg")));

  // Single instance: hand any request to the running copy and bail. A --send
  // launch from a file manager is the common case — the paths go across so the
  // existing window switches to send mode instead of a second app starting.
  const QStringList send_paths = ParseSendPaths(app.arguments());
  const QByteArray instance_payload =
      send_paths.isEmpty()
          ? QByteArrayLiteral("SHOW\n")
          : QByteArray("SEND\n") + send_paths.join(QLatin1Char('\n')).toUtf8() +
                QByteArrayLiteral("\n");
  if (ForwardToRunningInstance(instance_payload)) {
    return 0;
  }
  // Clear any stale socket left by a previous crash, then claim the name.
  QLocalServer::removeServer(QString::fromLatin1(kInstanceKey));
  QLocalServer instance_server;
  instance_server.listen(QString::fromLatin1(kInstanceKey));
  // Wayland/GNOME matches a window to its .desktop entry (and thus its taskbar
  // icon) via the desktop file name / app_id, not setWindowIcon. Without this
  // the shell falls back to a generic icon.
  QGuiApplication::setDesktopFileName(QStringLiteral("nearby-file-share"));

  // Armed before anything touches the radio: cleans up after a previous run
  // that was killed, and records the adapter name to put back on the way out.
  BluetoothNameGuard bt_name_guard;
  bt_name_guard.arm();

  FileShareTrayController controller;
  ThemeController theme;

  // Start with no window. Advertising visibility is untouched — this is only
  // about the window, so avoid the word "hidden", which already names a
  // visibility state. --hidden stays accepted for autostart entries written
  // before the rename.
  const QStringList args = app.arguments();
  const bool start_in_background =
      args.contains(QStringLiteral("--background")) ||
      args.contains(QStringLiteral("--hidden"));

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("fileShareController", &controller);
  engine.rootContext()->setContextProperty("Theme", &theme);
  engine.rootContext()->setContextProperty("startInBackground",
                                           start_in_background);
  engine.load(QUrl(QStringLiteral("qrc:/qml/FileShareTray.qml")));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }


  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (window == nullptr) {
    return 1;
  }

  // Expose a session-bus service so an external UI (GNOME Quick Settings
  // extension) can read/drive visibility and surface the window.
  QuickShareDbus dbus_service(&controller, window);
  dbus_service.registerOnBus();

  // A second launch pings the socket → surface the existing window, and switch
  // to send mode if it handed us paths.
  QObject::connect(
      &instance_server, &QLocalServer::newConnection, window,
      [&instance_server, &controller, window]() {
        QLocalSocket* conn = instance_server.nextPendingConnection();
        if (conn == nullptr) {
          return;
        }
        // The payload is a few hundred bytes at most, so read it inline rather
        // than tracking partial-read state per connection.
        QByteArray data;
        while (conn->waitForReadyRead(300)) {
          data += conn->readAll();
        }
        data += conn->readAll();
        conn->disconnectFromServer();
        conn->deleteLater();

        QStringList lines =
            QString::fromUtf8(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!lines.isEmpty() && lines.constFirst() == QStringLiteral("SEND")) {
          lines.removeFirst();
          if (!lines.isEmpty()) {
            controller.switchToSendModeWithFiles(lines);
          }
        }

        window->show();
        window->raise();
        window->requestActivate();
      });

  const auto resolve_tray_icon = [&app]() {
    const QColor white = "white";
    QIcon tray_icon = BuildTintedSymbolicIcon(
        QStringLiteral(":/icons/app_swap.svg"), white);
    if (tray_icon.isNull()) {
      tray_icon = QIcon::fromTheme(QStringLiteral("network-wireless-symbolic"));
    }
    if (tray_icon.isNull()) {
      tray_icon = QIcon(QStringLiteral(":/icons/tray_icon.png"));
    }
    if (tray_icon.isNull()) {
      tray_icon = app.windowIcon();
    }
    return tray_icon;
  };

  QSystemTrayIcon tray(resolve_tray_icon());
  tray.setToolTip(QStringLiteral("Nearby File Tray"));
  NotificationManager notification_manager(&tray, &app);

  QMenu tray_menu;
  QAction* send_action = tray_menu.addAction(QStringLiteral("Send"));
  QAction* receive_action = tray_menu.addAction(QStringLiteral("Receive"));
  tray_menu.addSeparator();
  QAction* show_action = tray_menu.addAction(QStringLiteral("Show"));
  QAction* hide_action = tray_menu.addAction(QStringLiteral("Hide"));
  tray_menu.addSeparator();
  QAction* quit_action = tray_menu.addAction(QStringLiteral("Quit"));

  QObject::connect(send_action, &QAction::triggered, window,
                   [&controller, window]() {
                     const QString file = QFileDialog::getOpenFileName(
                         nullptr, QStringLiteral("Select file to send"));
                     if (file.isEmpty()) {
                       return;
                     }
                     controller.switchToSendModeWithFile(file);
                     window->show();
                     window->raise();
                     window->requestActivate();
                   });

  QObject::connect(receive_action, &QAction::triggered,
                   [&controller, window]() {
                     controller.switchToReceiveMode();
                     window->show();
                     window->raise();
                     window->requestActivate();
                   });

  QObject::connect(show_action, &QAction::triggered, window, [window]() {
    window->show();
    window->raise();
    window->requestActivate();
  });

  QObject::connect(hide_action, &QAction::triggered, window, [window]() {
    window->hide();
  });

  QObject::connect(quit_action, &QAction::triggered, &app,
                   [&controller, &app]() {
                     controller.stop();
                     app.quit();
                   });

  QObject::connect(&tray, &QSystemTrayIcon::activated, window,
                   [window](QSystemTrayIcon::ActivationReason reason) {
                     if (reason != QSystemTrayIcon::Trigger &&
                         reason != QSystemTrayIcon::DoubleClick) {
                       return;
                     }
                     if (window->isVisible()) {
                       window->hide();
                     } else {
                       window->show();
                       window->raise();
                       window->requestActivate();
                     }
                   });

  QObject::connect(&controller, &FileShareTrayController::requestTrayMessage,
                   &notification_manager, &NotificationManager::ShowNotification);
  QObject::connect(&controller,
                   &FileShareTrayController::requestCopyLinkTrayMessage,
                   &notification_manager,
                   [&notification_manager](const QString& title,
                                           const QString& body,
                                           const QString& link) {
                     notification_manager.ShowCopyableNotification(
                         title, body, link, QStringLiteral("Copy link"));
                   });

  QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                   [&controller]() { controller.stop(); });
  // Connected after the stop() above so the service has given up the radio
  // before the adapter name goes back.
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &bt_name_guard,
                   [&bt_name_guard]() { bt_name_guard.restore(); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &tray,
                   [&tray, &resolve_tray_icon](Qt::ColorScheme) {
                     tray.setIcon(resolve_tray_icon());
                   });
#endif

  tray.setContextMenu(&tray_menu);
  // No user setting for this: the tray icon is the fallback UI. It hides only
  // while the GNOME Quick Settings tile announces itself over D-Bus, and comes
  // straight back if that tile goes away — so there is always a way to open or
  // quit the app.
  tray.setVisible(!dbus_service.tileActive());
  QObject::connect(&dbus_service, &QuickShareDbus::TileActiveChanged, &tray,
                   [&tray](bool active) { tray.setVisible(!active); });

  // Set before the first registration: a --background launch has no window, so
  // onVisibleChanged never fires to tell us.
  controller.setReceiveForeground(!start_in_background);

  controller.start();
  if (send_paths.isEmpty()) {
    controller.switchToReceiveMode();
  } else {
    controller.switchToSendModeWithFiles(send_paths);
  }

  return app.exec();
}
