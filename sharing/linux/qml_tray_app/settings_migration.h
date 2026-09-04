#ifndef SCOTTY_SETTINGS_MIGRATION_H_
#define SCOTTY_SETTINGS_MIGRATION_H_

#include <QSettings>

namespace scotty {

// Version the visibility model independently of application release tags.
// Existing unversioned settings are ambiguous: 1 was self-share in the old
// model and Contacts in the newer one. Preserve the old migration once, then
// persist the result and version together so future Contacts selections stick.
inline int LoadVisibility(QSettings& settings) {
  constexpr int kVisibilitySchema = 1;
  int visibility = settings.value(QStringLiteral("visibility"), 0).toInt();
  if (settings.value(QStringLiteral("visibilitySchemaVersion"), 0).toInt() <
      kVisibilitySchema) {
    if (visibility == 1) visibility = 3;
    if (visibility < 0 || visibility > 3) visibility = 0;
    settings.setValue(QStringLiteral("visibility"), visibility);
    settings.setValue(QStringLiteral("visibilitySchemaVersion"),
                      kVisibilitySchema);
  }
  return visibility >= 0 && visibility <= 3 ? visibility : 0;
}

}  // namespace scotty
#endif
