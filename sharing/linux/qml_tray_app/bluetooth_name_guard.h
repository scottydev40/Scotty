#ifndef SHARING_LINUX_QML_TRAY_APP_BLUETOOTH_NAME_GUARD_H_
#define SHARING_LINUX_QML_TRAY_APP_BLUETOOTH_NAME_GUARD_H_

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Puts the Bluetooth adapter name back after the Nearby stack borrows it.
//
// Bluetooth Classic discovery carries endpoint metadata in the adapter's name,
// so the platform layer overwrites BlueZ's Alias with a base64 advertisement
// blob while advertising (see internal/platform/implementation/linux/
// bluetooth_adapter.cc, whose SetName ignores its `persist` argument and never
// restores). The visible effect is a Bluetooth device name turning into
// gibberish and staying that way.
//
// arm() records the current alias of every adapter; restore() writes it back.
// The record is persisted, so a run that is killed before restoring is cleaned
// up by the next arm() instead of leaving the name mangled forever.
// QObject only so it can serve as a connection context; it declares no
// signals, slots or properties, so it needs no meta-object.
class BluetoothNameGuard : public QObject {
 public:
  explicit BluetoothNameGuard(QObject* parent = nullptr);

  // Restores anything a previous run left behind, then remembers the current
  // (now clean) aliases. Call before the sharing service starts.
  void arm();

  // Writes the remembered aliases back. Safe to call more than once.
  void restore();

 private:
  QStringList adapterPaths() const;
  QString readAlias(const QString& adapter_path) const;
  void writeAlias(const QString& adapter_path, const QString& alias) const;

  void loadStash();
  void saveStash() const;

  // Adapter object path -> the alias it had before we touched it.
  QVariantMap stash_;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_BLUETOOTH_NAME_GUARD_H_
