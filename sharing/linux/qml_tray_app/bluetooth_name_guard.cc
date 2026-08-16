#include "bluetooth_name_guard.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QRegularExpression>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>
#include <QVariant>

namespace {

constexpr char kBluezService[] = "org.bluez";
constexpr char kAdapterInterface[] = "org.bluez.Adapter1";
constexpr char kPropsInterface[] = "org.freedesktop.DBus.Properties";
constexpr char kStashKey[] = "bluetoothAliasStash";

// A Nearby advertisement blob: long, base64/url-safe charset, no spaces. The
// platform layer writes one of these into the adapter Alias while advertising
// (base64 endpoint metadata). A genuine adapter name ("laptop", "Harsha's
// Laptop") is short or contains spaces, so this only matches the gibberish.
bool LooksLikeNearbyBlob(const QString& name) {
  if (name.size() < 20) {
    return false;
  }
  static const QRegularExpression kBlob(QStringLiteral("^[A-Za-z0-9+/=_-]+$"));
  return kBlob.match(name).hasMatch();
}

// The value to remember as an adapter's "original" alias. A blob is never the
// user's real name — remembering it would make the guard heal the adapter to
// gibberish forever (the poisoning bug) — so fall back to the host name, which
// is exactly what BlueZ shows by default. Empty and genuine names pass through
// unchanged so they still round-trip.
QString SanitizeOriginal(const QString& current) {
  if (LooksLikeNearbyBlob(current)) {
    return QSysInfo::machineHostName();
  }
  return current;
}

// Skips a property dictionary (a{sv}) we don't care about.
void SkipProperties(const QDBusArgument& arg) {
  arg.beginMap();
  while (!arg.atEnd()) {
    arg.beginMapEntry();
    QString key;
    QDBusVariant value;
    arg >> key >> value;
    arg.endMapEntry();
  }
  arg.endMap();
}

}  // namespace

BluetoothNameGuard::BluetoothNameGuard(QObject* parent) : QObject(parent) {}

QStringList BluetoothNameGuard::adapterPaths() const {
  QStringList paths;
  QDBusConnection bus = QDBusConnection::systemBus();
  if (!bus.isConnected()) {
    return paths;
  }

  QDBusMessage call = QDBusMessage::createMethodCall(
      QString::fromLatin1(kBluezService), QStringLiteral("/"),
      QStringLiteral("org.freedesktop.DBus.ObjectManager"),
      QStringLiteral("GetManagedObjects"));
  const QDBusMessage reply = bus.call(call, QDBus::Block, 1000);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
    return paths;  // BlueZ not running; nothing to protect.
  }

  // a{oa{sa{sv}}}: object path -> interface -> properties.
  const QDBusArgument arg =
      reply.arguments().constFirst().value<QDBusArgument>();
  arg.beginMap();
  while (!arg.atEnd()) {
    arg.beginMapEntry();
    QDBusObjectPath path;
    arg >> path;

    bool is_adapter = false;
    arg.beginMap();
    while (!arg.atEnd()) {
      arg.beginMapEntry();
      QString interface;
      arg >> interface;
      SkipProperties(arg);
      arg.endMapEntry();
      if (interface == QLatin1String(kAdapterInterface)) {
        is_adapter = true;
      }
    }
    arg.endMap();
    arg.endMapEntry();

    if (is_adapter) {
      paths.append(path.path());
    }
  }
  arg.endMap();
  return paths;
}

QString BluetoothNameGuard::readAlias(const QString& adapter_path) const {
  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusMessage call = QDBusMessage::createMethodCall(
      QString::fromLatin1(kBluezService), adapter_path,
      QString::fromLatin1(kPropsInterface), QStringLiteral("Get"));
  call << QString::fromLatin1(kAdapterInterface) << QStringLiteral("Alias");

  const QDBusMessage reply = bus.call(call, QDBus::Block, 1000);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
    return QString();
  }
  return reply.arguments().constFirst().value<QDBusVariant>().variant().toString();
}

void BluetoothNameGuard::writeAlias(const QString& adapter_path,
                                    const QString& alias) const {
  QDBusConnection bus = QDBusConnection::systemBus();
  QDBusMessage call = QDBusMessage::createMethodCall(
      QString::fromLatin1(kBluezService), adapter_path,
      QString::fromLatin1(kPropsInterface), QStringLiteral("Set"));
  call << QString::fromLatin1(kAdapterInterface) << QStringLiteral("Alias")
       << QVariant::fromValue(QDBusVariant(alias));
  bus.call(call, QDBus::Block, 1000);
}

void BluetoothNameGuard::loadStash() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  stash_ = settings.value(QString::fromLatin1(kStashKey)).toMap();
  // Heal a stash poisoned by an earlier buggy run that recorded the advertising
  // blob as the "original": replace any blob with the host name so restore/heal
  // put back the real name instead of gibberish.
  for (auto it = stash_.begin(); it != stash_.end(); ++it) {
    const QString original = it.value().toString();
    const QString clean = SanitizeOriginal(original);
    if (clean != original) {
      it.value() = clean;
    }
  }
}

void BluetoothNameGuard::saveStash() const {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  if (stash_.isEmpty()) {
    settings.remove(QString::fromLatin1(kStashKey));
  } else {
    settings.setValue(QString::fromLatin1(kStashKey), stash_);
  }
}

void BluetoothNameGuard::arm() {
  loadStash();
  if (!stash_.isEmpty()) {
    // A stash that outlived its process means the last run was killed before
    // it could restore, so the adapter is still showing the mangled name.
    // Those stashed values are the originals: write them back and keep them as
    // the record. Re-reading here would be wrong — BlueZ applies alias writes
    // asynchronously, so the read can still return the mangled name and we
    // would stash *that* as the original.
    const QVariantMap originals = stash_;
    restore();
    stash_ = originals;
    saveStash();
  } else {
    // Clean start: whatever the adapters show now is what to put back.
    const QStringList paths = adapterPaths();
    for (const QString& path : paths) {
      // An empty alias is BlueZ's "use the auto-generated name" state, and
      // writing an empty string back restores exactly that — so it round-trips.
      // Sanitize first: if the adapter is already showing an advertising blob
      // (arm() lost the race with the platform layer), record the host name
      // instead of adopting the gibberish as the original.
      stash_.insert(path, SanitizeOriginal(readAlias(path)));
    }
    saveStash();
  }

  // Self-heal: a BT-Classic fallback transfer can overwrite the alias while the
  // app is running; put it back periodically instead of only on quit.
  if (heal_timer_ == nullptr) {
    heal_timer_ = new QTimer(this);
    heal_timer_->setInterval(45000);
    connect(heal_timer_, &QTimer::timeout, this, [this] { heal(); });
  }
  heal_timer_->start();
}

void BluetoothNameGuard::heal() {
  for (auto it = stash_.constBegin(); it != stash_.constEnd(); ++it) {
    const QString original = it.value().toString();
    if (readAlias(it.key()) != original) {
      writeAlias(it.key(), original);
    }
  }
}

void BluetoothNameGuard::restore() {
  if (stash_.isEmpty()) {
    return;
  }
  for (auto it = stash_.constBegin(); it != stash_.constEnd(); ++it) {
    writeAlias(it.key(), it.value().toString());
  }
  stash_.clear();
  saveStash();
}
