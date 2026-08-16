#include "notification_manager.h"

#include <QAbstractButton>
#include <QClipboard>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QVariantMap>

namespace {

constexpr char kNotificationsService[] = "org.freedesktop.Notifications";
constexpr char kNotificationsPath[] = "/org/freedesktop/Notifications";
constexpr char kNotificationsInterface[] = "org.freedesktop.Notifications";
constexpr char kCopyActionId[] = "copy_value";
constexpr char kAcceptActionId[] = "accept_transfer";
constexpr char kDeclineActionId[] = "decline_transfer";
constexpr char kDesktopEntryId[] = "dev.scotty.Scotty";

}  // namespace

NotificationManager::NotificationManager(QSystemTrayIcon* tray_icon,
                                         QObject* parent)
    : QObject(parent), tray_icon_(tray_icon) {
  QDBusConnection session_bus = QDBusConnection::sessionBus();
  if (!session_bus.isConnected()) {
    return;
  }

  session_bus.connect(QString::fromLatin1(kNotificationsService),
                      QString::fromLatin1(kNotificationsPath),
                      QString::fromLatin1(kNotificationsInterface),
                      QStringLiteral("ActionInvoked"), this,
                      SLOT(OnActionInvoked(uint,QString)));
  session_bus.connect(QString::fromLatin1(kNotificationsService),
                      QString::fromLatin1(kNotificationsPath),
                      QString::fromLatin1(kNotificationsInterface),
                      QStringLiteral("NotificationClosed"), this,
                      SLOT(OnNotificationClosed(uint,uint)));

  QDBusInterface notification_interface(
      QString::fromLatin1(kNotificationsService),
      QString::fromLatin1(kNotificationsPath),
      QString::fromLatin1(kNotificationsInterface), session_bus);
  QDBusReply<QStringList> capabilities_reply =
      notification_interface.call(QStringLiteral("GetCapabilities"));
  if (capabilities_reply.isValid()) {
    notifications_available_ = true;
    supports_actions_ =
        capabilities_reply.value().contains(QStringLiteral("actions"));
  }
}

uint NotificationManager::PostNotification(const QString& title,
                                           const QString& body,
                                           const QStringList& actions,
                                           int timeout_ms, bool resident) {
  if (!notifications_available_) {
    return 0;
  }

  QDBusInterface notification_interface(
      QString::fromLatin1(kNotificationsService),
      QString::fromLatin1(kNotificationsPath),
      QString::fromLatin1(kNotificationsInterface),
      QDBusConnection::sessionBus());

  // Let the notification server draw the app's own icon (full-colour, correctly
  // sized and padded) from the desktop entry — the way Firefox and other apps
  // do — instead of hand-rendering the monochrome tray glyph into the slot.
  QVariantMap hints{
      {QStringLiteral("desktop-entry"), QString::fromLatin1(kDesktopEntryId)}};
  if (resident) {
    // Keep it on screen until answered rather than fading out with the sender
    // still waiting.
    hints.insert(QStringLiteral("resident"), true);
    hints.insert(QStringLiteral("urgency"), static_cast<uchar>(2));
  }

  QDBusReply<uint> reply = notification_interface.call(
      QStringLiteral("Notify"), QCoreApplication::applicationName(),
      static_cast<uint>(0), QString::fromLatin1(kDesktopEntryId),
      title, body, actions, hints, timeout_ms);
  return reply.isValid() ? reply.value() : 0;
}

void NotificationManager::ShowNotification(const QString& title,
                                           const QString& body) {
  // Prefer the desktop notification service: the tray balloon below only
  // renders while the tray icon is visible, and it is hidden whenever the
  // GNOME Quick Settings tile is driving the app.
  if (PostNotification(title, body, QStringList{}, 4000, /*resident=*/false) !=
      0) {
    return;
  }
  if (tray_icon_ != nullptr) {
    tray_icon_->showMessage(title, body, QSystemTrayIcon::Information, 4000);
  }
}

void NotificationManager::ShowIncomingRequest(qlonglong share_target_id,
                                              const QString& device_name,
                                              const QString& file_name) {
  const QString title = QStringLiteral("Incoming file");
  const QString body =
      file_name.isEmpty()
          ? QStringLiteral("%1 wants to send you a file.").arg(device_name)
          : QStringLiteral("%1 wants to send %2.").arg(device_name, file_name);

  if (supports_actions_) {
    const uint id = PostNotification(
        title, body,
        QStringList{QString::fromLatin1(kAcceptActionId),
                    QStringLiteral("Accept"),
                    QString::fromLatin1(kDeclineActionId),
                    QStringLiteral("Decline")},
        /*timeout_ms=*/0, /*resident=*/true);
    if (id != 0) {
      pending_decisions_.insert(id, share_target_id);
      return;
    }
  }

  // No action support: at least say something, so the user knows to open the
  // window and answer there.
  ShowNotification(title, body);
}

void NotificationManager::ShowCopyableNotification(
    const QString& title, const QString& body, const QString& text_to_copy,
    const QString& action_label) {
  const QString trimmed_text = text_to_copy.trimmed();
  const QString trimmed_action_label = action_label.trimmed().isEmpty()
                                           ? QStringLiteral("Copy")
                                           : action_label.trimmed();
  if (trimmed_text.isEmpty()) {
    ShowNotification(title, body);
    return;
  }

  if (supports_actions_) {
    QDBusInterface notification_interface(
        QString::fromLatin1(kNotificationsService),
        QString::fromLatin1(kNotificationsPath),
        QString::fromLatin1(kNotificationsInterface),
        QDBusConnection::sessionBus());
    const QString application_name = QCoreApplication::applicationName();
    QVariantMap hints{{QStringLiteral("desktop-entry"),
                       QString::fromLatin1(kDesktopEntryId)}};
    QDBusReply<uint> reply = notification_interface.call(
        QStringLiteral("Notify"), application_name,
        static_cast<uint>(0), QString::fromLatin1(kDesktopEntryId),
        title, body,
        QStringList{QString::fromLatin1(kCopyActionId), trimmed_action_label},
        hints, 8000);
    if (reply.isValid()) {
      copy_actions_.insert(reply.value(), CopyActionState{trimmed_text});
      return;
    }
  }

  ShowFallbackDialog(title, body, trimmed_text, trimmed_text,
                     trimmed_action_label);
}

void NotificationManager::CloseNotification(uint notification_id) {
  if (!notifications_available_ || notification_id == 0) {
    return;
  }
  QDBusInterface notification_interface(
      QString::fromLatin1(kNotificationsService),
      QString::fromLatin1(kNotificationsPath),
      QString::fromLatin1(kNotificationsInterface),
      QDBusConnection::sessionBus());
  notification_interface.call(QStringLiteral("CloseNotification"),
                              notification_id);
}

void NotificationManager::DismissIncomingRequest(qlonglong share_target_id) {
  for (auto it = pending_decisions_.begin(); it != pending_decisions_.end();) {
    if (it.value() == share_target_id) {
      CloseNotification(it.key());
      it = pending_decisions_.erase(it);
    } else {
      ++it;
    }
  }
}

void NotificationManager::OnActionInvoked(uint notification_id,
                                          const QString& action_key) {
  auto decision = pending_decisions_.find(notification_id);
  if (decision != pending_decisions_.end()) {
    const qlonglong share_target_id = decision.value();
    pending_decisions_.erase(decision);
    // Posted resident, so clicking a button does not dismiss it for us.
    CloseNotification(notification_id);
    if (action_key == QString::fromLatin1(kAcceptActionId)) {
      emit acceptRequested(share_target_id);
    } else if (action_key == QString::fromLatin1(kDeclineActionId)) {
      emit declineRequested(share_target_id);
    }
    return;
  }

  if (action_key != QString::fromLatin1(kCopyActionId)) {
    return;
  }

  auto it = copy_actions_.find(notification_id);
  if (it == copy_actions_.end()) {
    return;
  }

  CopyTextToClipboard(it->text_to_copy, QStringLiteral("Copied"),
                      QStringLiteral("Copied to clipboard."));
  copy_actions_.erase(it);
}

void NotificationManager::OnNotificationClosed(uint notification_id,
                                               uint reason) {
  Q_UNUSED(reason);
  copy_actions_.remove(notification_id);
  // Dismissing without choosing is not an answer: leave the transfer pending
  // so it can still be handled from the window.
  pending_decisions_.remove(notification_id);
}

void NotificationManager::CopyTextToClipboard(
    const QString& text_to_copy, const QString& confirmation_title,
    const QString& confirmation_body) const {
  QClipboard* clipboard = QGuiApplication::clipboard();
  if (clipboard != nullptr) {
    clipboard->setText(text_to_copy);
  }

  if (tray_icon_ != nullptr) {
    tray_icon_->showMessage(confirmation_title, confirmation_body,
                            QSystemTrayIcon::Information, 2500);
  }
}

void NotificationManager::ShowFallbackDialog(const QString& title,
                                             const QString& body,
                                             const QString& informative_text,
                                             const QString& text_to_copy,
                                             const QString& action_label) {
  auto* message_box =
      new QMessageBox(QMessageBox::Information, title, body, QMessageBox::NoButton);
  message_box->setAttribute(Qt::WA_DeleteOnClose);
  message_box->setTextFormat(Qt::PlainText);
  message_box->setInformativeText(informative_text);
  message_box->setWindowFlag(Qt::WindowStaysOnTopHint);
  if (tray_icon_ != nullptr && !tray_icon_->icon().isNull()) {
    message_box->setWindowIcon(tray_icon_->icon());
  }

  QAbstractButton* copy_button =
      message_box->addButton(action_label, QMessageBox::ActionRole);
  message_box->addButton(QMessageBox::Close);

  QObject::connect(message_box, &QMessageBox::buttonClicked, message_box,
                   [this, message_box, copy_button, text_to_copy](
                       QAbstractButton* button) {
                     if (button == copy_button) {
                       CopyTextToClipboard(text_to_copy, QStringLiteral("Copied"),
                                           QStringLiteral("Copied to clipboard."));
                     }
                     message_box->close();
                   });

  message_box->show();
  message_box->raise();
  message_box->activateWindow();
}
