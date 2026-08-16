#include "app_paths.h"

#include <QDir>

QString DefaultLogPath() {
  QString state_home = qEnvironmentVariable("XDG_STATE_HOME").trimmed();
  if (state_home.isEmpty()) {
    state_home = QDir::homePath() + QStringLiteral("/.local/state");
  }
  return QDir(state_home).filePath(QStringLiteral("scotty/scotty.log"));
}
