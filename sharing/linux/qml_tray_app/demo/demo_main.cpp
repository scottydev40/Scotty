// Copyright 2026 The Scotty Authors
// Licensed under the Apache License, Version 2.0.
//
// Demo launcher: loads the real Scotty QML UI against a fabricated controller so
// each screen can be screenshotted without the Nearby engine, an account, or a
// real hostname. Pick the screen with SCOTTY_DEMO=home|send|receive.
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "demo_controller.h"
#include "theme_controller.h"
#include "update_checker.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Scotty"));

  QQmlApplicationEngine engine;
  DemoController controller;
  ThemeController theme;
  UpdateChecker update_checker;

  QQmlContext* ctx = engine.rootContext();
  ctx->setContextProperty(QStringLiteral("fileShareController"), &controller);
  ctx->setContextProperty(QStringLiteral("Theme"), &theme);
  ctx->setContextProperty(QStringLiteral("updateChecker"), &update_checker);
  ctx->setContextProperty(QStringLiteral("startInBackground"), false);

  engine.load(QUrl(QStringLiteral("qrc:/qml/FileShareTray.qml")));
  if (engine.rootObjects().isEmpty()) return 1;
  return app.exec();
}
