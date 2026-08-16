#ifndef SHARING_LINUX_QML_TRAY_APP_NOTIFICATION_MANAGER_H_
#define SHARING_LINUX_QML_TRAY_APP_NOTIFICATION_MANAGER_H_

#include <QObject>

#include <QHash>
#include <QString>

class QSystemTrayIcon;

class NotificationManager : public QObject {
  Q_OBJECT

 public:
  explicit NotificationManager(QSystemTrayIcon* tray_icon,
                               QObject* parent = nullptr);

  void ShowNotification(const QString& title, const QString& body);
  void ShowCopyableNotification(const QString& title, const QString& body,
                                const QString& text_to_copy,
                                const QString& action_label);
  // Asks the user to accept or decline, with buttons on the notification
  // itself, so it works with the window hidden and no tray icon.
  void ShowIncomingRequest(qlonglong share_target_id,
                           const QString& device_name,
                           const QString& file_name);
  // Takes the request notification away once the transfer no longer needs an
  // answer — accepted from the window, cancelled, or finished.
  void DismissIncomingRequest(qlonglong share_target_id);

 signals:
  void acceptRequested(qlonglong share_target_id);
  void declineRequested(qlonglong share_target_id);

 private slots:
  void OnActionInvoked(uint notification_id, const QString& action_key);
  void OnNotificationClosed(uint notification_id, uint reason);

 private:
  struct CopyActionState {
    QString text_to_copy;
  };

  void CopyTextToClipboard(const QString& text_to_copy,
                           const QString& confirmation_title,
                           const QString& confirmation_body) const;
  void ShowFallbackDialog(const QString& title, const QString& body,
                          const QString& informative_text,
                          const QString& text_to_copy,
                          const QString& action_label);

  // Posts via org.freedesktop.Notifications. Returns 0 if that is unavailable,
  // in which case the caller should fall back. Unlike the tray balloon this
  // does not need a visible tray icon.
  uint PostNotification(const QString& title, const QString& body,
                        const QStringList& actions, int timeout_ms,
                        bool resident);
  // Resident notifications survive their action being clicked, so they have to
  // be closed explicitly or they sit there forever.
  void CloseNotification(uint notification_id);

  bool supports_actions_ = false;
  bool notifications_available_ = false;
  QHash<uint, CopyActionState> copy_actions_;
  // Notification id -> the transfer it is asking about.
  QHash<uint, qlonglong> pending_decisions_;
  QSystemTrayIcon* tray_icon_ = nullptr;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_NOTIFICATION_MANAGER_H_
