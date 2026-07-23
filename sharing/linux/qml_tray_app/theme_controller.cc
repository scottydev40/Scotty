#include "theme_controller.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QSettings>
#include <QVariant>

namespace {

constexpr char kPortalService[] = "org.freedesktop.portal.Desktop";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr char kPortalSettings[] = "org.freedesktop.portal.Settings";
constexpr char kAppearance[] = "org.freedesktop.appearance";


// Alpha-blend `over` onto opaque `base` (base*(1-a) + over*a).
QColor Blend(const QColor& base, const QColor& over, double a) {
  const double inv = 1.0 - a;
  return QColor::fromRgbF(base.redF() * inv + over.redF() * a,
                          base.greenF() * inv + over.greenF() * a,
                          base.blueF() * inv + over.blueF() * a);
}

struct AccentTriple {
  QColor base, strong, deep;
};

AccentTriple AccentFor(int accent) {
  switch (accent) {
    case ThemeController::kBlue:
      return {QColor("#2563eb"), QColor("#1d4ed8"), QColor("#1e40af")};
    case ThemeController::kUbuntu:
      return {QColor("#e95420"), QColor("#d0451a"), QColor("#a83815")};
    case ThemeController::kGreen:
    default:
      return {QColor("#16a34a"), QColor("#059669"), QColor("#047857")};
  }
}

// The portal publishes the accent as an (ddd) struct in 0..1, and uses
// (-1,-1,-1) to mean "no accent configured".
QColor AccentFromPortal(const QVariant& value) {
  if (value.userType() != qMetaTypeId<QDBusArgument>()) return QColor();
  const QDBusArgument arg = value.value<QDBusArgument>();
  if (arg.currentType() != QDBusArgument::StructureType) return QColor();

  double r = -1.0, g = -1.0, b = -1.0;
  arg.beginStructure();
  arg >> r >> g >> b;
  arg.endStructure();
  if (r < 0.0 || g < 0.0 || b < 0.0) return QColor();
  return QColor::fromRgbF(qBound(0.0, r, 1.0), qBound(0.0, g, 1.0),
                          qBound(0.0, b, 1.0));
}

// Perceived brightness, used to pick readable text on top of an accent we did
// not choose ourselves.
double Luminance(const QColor& c) {
  return 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
}

}  // namespace

ThemeController::ThemeController(QObject* parent) : QObject(parent) {
  load();
  initSystemAppearance();
  rebuild();
}

void ThemeController::load() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  accent_ = settings.value(QStringLiteral("themeAccent"), kGreen).toInt();
  if (accent_ < kGreen || accent_ > kUbuntu) accent_ = kGreen;
  dark_ = settings.value(QStringLiteral("themeDark"), false).toBool();
  follow_system_ =
      settings.value(QStringLiteral("themeFollowSystem"), true).toBool();
}

void ThemeController::save() const {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  settings.setValue(QStringLiteral("themeAccent"), accent_);
  settings.setValue(QStringLiteral("themeDark"), dark_);
  settings.setValue(QStringLiteral("themeFollowSystem"), follow_system_);
}

void ThemeController::initSystemAppearance() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) return;

  bus.connect(QString::fromLatin1(kPortalService),
              QString::fromLatin1(kPortalPath),
              QString::fromLatin1(kPortalSettings),
              QStringLiteral("SettingChanged"), this,
              SLOT(onPortalSettingChanged(QString, QString, QDBusVariant)));

  // Short blocking reads: the portal is a local process, and this runs once
  // before the first frame so the window never flashes the wrong theme.
  for (const char* key : {"color-scheme", "accent-color"}) {
    for (const char* method : {"ReadOne", "Read"}) {
      QDBusMessage call = QDBusMessage::createMethodCall(
          QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
          QString::fromLatin1(kPortalSettings), QString::fromLatin1(method));
      call << QString::fromLatin1(kAppearance) << QString::fromLatin1(key);

      const QDBusMessage reply = bus.call(call, QDBus::Block, 300);
      if (reply.type() != QDBusMessage::ReplyMessage ||
          reply.arguments().isEmpty()) {
        continue;  // no ReadOne on older portals — fall through to Read
      }
      applyPortalValue(QString::fromLatin1(key), reply.arguments().constFirst());
      break;
    }
  }
}

void ThemeController::applyPortalValue(const QString& key,
                                       const QVariant& value) {
  // ReadOne wraps the value once, the legacy Read wraps it twice.
  QVariant unwrapped = value;
  while (unwrapped.userType() == qMetaTypeId<QDBusVariant>()) {
    unwrapped = unwrapped.value<QDBusVariant>().variant();
  }

  if (key == QLatin1String("color-scheme")) {
    system_dark_ = unwrapped.toUInt() == 1;  // 0 no preference, 1 dark, 2 light
  } else if (key == QLatin1String("accent-color")) {
    system_accent_ = AccentFromPortal(unwrapped);
  }
}

void ThemeController::onPortalSettingChanged(const QString& name_space,
                                             const QString& key,
                                             const QDBusVariant& value) {
  if (name_space != QLatin1String(kAppearance)) return;
  applyPortalValue(key, value.variant());
  if (!follow_system_) return;
  rebuild();
  emit changed();
}

void ThemeController::setAccent(int accent) {
  if (accent < kGreen || accent > kUbuntu) return;
  if (accent == accent_ && !follow_system_) return;
  accent_ = accent;
  follow_system_ = false;  // an explicit pick pins the theme
  rebuild();
  save();
  emit changed();
}

void ThemeController::setDark(bool dark) {
  if (dark == dark_ && !follow_system_) return;
  dark_ = dark;
  follow_system_ = false;
  rebuild();
  save();
  emit changed();
}

void ThemeController::setFollowSystem(bool follow) {
  if (follow == follow_system_) return;
  follow_system_ = follow;
  // Unpinning shouldn't visibly jump: keep the light/dark state we're showing.
  if (!follow) dark_ = system_dark_;
  rebuild();
  save();
  emit changed();
}

void ThemeController::rebuild() {
  const bool dark = this->dark();

  // A desktop-provided accent has no hand-tuned hover/pressed shades, so
  // derive them by darkening.
  const AccentTriple a =
      (follow_system_ && system_accent_.isValid())
          ? AccentTriple{system_accent_, system_accent_.darker(115),
                         system_accent_.darker(135)}
          : AccentFor(accent_);

  accent_color_ = a.base;
  accent_strong_ = a.strong;
  accent_deep_ = a.deep;
  // System accents can be light (yellow, teal); keep label text readable.
  on_accent_ = Luminance(a.base) > 0.62 ? QColor("#101418") : QColor("#ffffff");

  if (dark) {
    surface_ = QColor("#1b1f26");
    surface_alt_ = QColor("#242932");
    window_bg_ = QColor("#12151b");
    text_primary_ = QColor("#f3f4f6");
    text_muted_ = QColor("#9aa3af");
    border_ = QColor("#2d333d");
    danger_ = QColor("#f87171");
    row_fill_ = Blend(surface_, a.base, 0.22);
    row_fill_hover_ = Blend(surface_, a.base, 0.32);
    danger_soft_ = Blend(surface_, danger_, 0.18);
  } else {
    surface_ = QColor("#ffffff");
    surface_alt_ = QColor("#f7f8fa");
    window_bg_ = Blend(QColor("#ffffff"), a.base, 0.06);
    text_primary_ = QColor("#111827");
    text_muted_ = QColor("#6b7280");
    border_ = QColor("#e5e7eb");
    danger_ = QColor("#ef4444");
    row_fill_ = Blend(QColor("#ffffff"), a.base, 0.14);
    row_fill_hover_ = Blend(QColor("#ffffff"), a.base, 0.22);
    danger_soft_ = Blend(QColor("#ffffff"), danger_, 0.12);
  }
}
