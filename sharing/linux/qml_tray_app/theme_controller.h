#ifndef SHARING_LINUX_QML_TRAY_APP_THEME_CONTROLLER_H_
#define SHARING_LINUX_QML_TRAY_APP_THEME_CONTROLLER_H_

#include <QColor>
#include <QDBusVariant>
#include <QObject>
#include <QString>

// Central color theme, exposed to QML as the `Theme` context property.
// Every color token is derived from two inputs: an accent choice and
// light/dark mode. QML references these tokens (Theme.accent, Theme.surface, …)
// instead of hardcoding hexes, so a single setting recolors the whole app.
//
// By default both inputs follow the desktop (GNOME's accent colour and
// light/dark preference) via the XDG desktop portal, live. Picking an accent
// swatch or flipping the dark switch in Settings turns that off and pins the
// manual choice, which is what gets persisted.
class ThemeController : public QObject {
  Q_OBJECT
  // Inputs.
  Q_PROPERTY(int accent READ accent WRITE setAccent NOTIFY changed)
  Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)
  Q_PROPERTY(bool followSystem READ followSystem WRITE setFollowSystem NOTIFY changed)
  // True when the desktop actually reports an accent colour (GNOME 47+).
  // Lets the UI explain why "follow system" only affects light/dark.
  Q_PROPERTY(bool systemAccentAvailable READ systemAccentAvailable NOTIFY changed)

  // Derived tokens (all recomputed on any input change → single NOTIFY).
  Q_PROPERTY(QColor accentColor READ accentColor NOTIFY changed)
  Q_PROPERTY(QColor accentStrong READ accentStrong NOTIFY changed)
  Q_PROPERTY(QColor accentDeep READ accentDeep NOTIFY changed)
  Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)
  Q_PROPERTY(QColor windowBg READ windowBg NOTIFY changed)
  Q_PROPERTY(QColor surface READ surface NOTIFY changed)
  Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed)
  Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY changed)
  Q_PROPERTY(QColor textMuted READ textMuted NOTIFY changed)
  Q_PROPERTY(QColor border READ border NOTIFY changed)
  Q_PROPERTY(QColor rowFill READ rowFill NOTIFY changed)
  Q_PROPERTY(QColor rowFillHover READ rowFillHover NOTIFY changed)
  Q_PROPERTY(QColor danger READ danger NOTIFY changed)
  Q_PROPERTY(QColor dangerSoft READ dangerSoft NOTIFY changed)

 public:
  // Accent choices persisted as an int so the settings plumbing stays simple.
  enum Accent { kGreen = 0, kBlue = 1, kUbuntu = 2 };

  explicit ThemeController(QObject* parent = nullptr);

  int accent() const { return accent_; }
  // Effective light/dark: the desktop's preference while following it, the
  // pinned choice otherwise.
  bool dark() const { return follow_system_ ? system_dark_ : dark_; }
  bool followSystem() const { return follow_system_; }
  bool systemAccentAvailable() const { return system_accent_.isValid(); }

  QColor accentColor() const { return accent_color_; }
  QColor accentStrong() const { return accent_strong_; }
  QColor accentDeep() const { return accent_deep_; }
  QColor onAccent() const { return on_accent_; }
  QColor windowBg() const { return window_bg_; }
  QColor surface() const { return surface_; }
  QColor surfaceAlt() const { return surface_alt_; }
  QColor textPrimary() const { return text_primary_; }
  QColor textMuted() const { return text_muted_; }
  QColor border() const { return border_; }
  QColor rowFill() const { return row_fill_; }
  QColor rowFillHover() const { return row_fill_hover_; }
  QColor danger() const { return danger_; }
  QColor dangerSoft() const { return danger_soft_; }

  void setAccent(int accent);
  void setDark(bool dark);
  void setFollowSystem(bool follow);

 signals:
  void changed();

 private slots:
  // org.freedesktop.portal.Settings.SettingChanged
  void onPortalSettingChanged(const QString& name_space, const QString& key,
                              const QDBusVariant& value);

 private:
  void load();
  void save() const;
  void rebuild();  // recompute every derived token from the active inputs

  // Initial portal read + subscription to live changes.
  void initSystemAppearance();
  void applyPortalValue(const QString& key, const QVariant& value);

  int accent_ = kGreen;
  bool dark_ = false;
  bool follow_system_ = true;

  // Last values reported by the desktop. system_accent_ stays invalid on
  // desktops that do not publish one.
  QColor system_accent_;
  bool system_dark_ = false;

  QColor accent_color_, accent_strong_, accent_deep_, on_accent_;
  QColor window_bg_, surface_, surface_alt_;
  QColor text_primary_, text_muted_, border_;
  QColor row_fill_, row_fill_hover_;
  QColor danger_, danger_soft_;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_THEME_CONTROLLER_H_
