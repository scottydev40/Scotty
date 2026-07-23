#include "theme_controller.h"

#include <QSettings>

namespace {

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

}  // namespace

ThemeController::ThemeController(QObject* parent) : QObject(parent) {
  load();
  rebuild();
}

void ThemeController::load() {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  accent_ = settings.value(QStringLiteral("themeAccent"), kGreen).toInt();
  if (accent_ < kGreen || accent_ > kUbuntu) accent_ = kGreen;
  dark_ = settings.value(QStringLiteral("themeDark"), false).toBool();
}

void ThemeController::save() const {
  QSettings settings(QStringLiteral("Nearby"), QStringLiteral("QmlFileTrayApp"));
  settings.setValue(QStringLiteral("themeAccent"), accent_);
  settings.setValue(QStringLiteral("themeDark"), dark_);
}

void ThemeController::setAccent(int accent) {
  if (accent < kGreen || accent > kUbuntu || accent == accent_) return;
  accent_ = accent;
  rebuild();
  save();
  emit changed();
}

void ThemeController::setDark(bool dark) {
  if (dark == dark_) return;
  dark_ = dark;
  rebuild();
  save();
  emit changed();
}

void ThemeController::rebuild() {
  const AccentTriple a = AccentFor(accent_);
  accent_color_ = a.base;
  accent_strong_ = a.strong;
  accent_deep_ = a.deep;
  on_accent_ = QColor("#ffffff");

  if (dark_) {
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
