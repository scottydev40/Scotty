#ifndef SHARING_LINUX_QML_TRAY_APP_THEME_CONTROLLER_H_
#define SHARING_LINUX_QML_TRAY_APP_THEME_CONTROLLER_H_

#include <QColor>
#include <QObject>

// Central color theme, exposed to QML as the `Theme` context property.
// Every color token is derived from two persisted inputs: an accent choice
// (green / blue / Ubuntu-orange) and light/dark mode. QML references these
// tokens (Theme.accent, Theme.surface, …) instead of hardcoding hexes, so a
// single setting recolors the whole app.
class ThemeController : public QObject {
  Q_OBJECT
  // Inputs.
  Q_PROPERTY(int accent READ accent WRITE setAccent NOTIFY changed)
  Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)

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
  bool dark() const { return dark_; }

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

 signals:
  void changed();

 private:
  void load();
  void save() const;
  void rebuild();  // recompute every derived token from accent_ + dark_

  int accent_ = kGreen;
  bool dark_ = false;

  QColor accent_color_, accent_strong_, accent_deep_, on_accent_;
  QColor window_bg_, surface_, surface_alt_;
  QColor text_primary_, text_muted_, border_;
  QColor row_fill_, row_fill_hover_;
  QColor danger_, danger_soft_;
};

#endif  // SHARING_LINUX_QML_TRAY_APP_THEME_CONTROLLER_H_
