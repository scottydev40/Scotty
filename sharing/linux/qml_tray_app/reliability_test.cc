#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <chrono>
#include <functional>
#include <future>

#include "send_preparation.h"
#include "settings_migration.h"
#include "shutdown_barrier.h"
#ifndef Q_MOC_RUN
#include "internal/platform/implementation/linux/linux_flags.h"
#endif

using namespace std::chrono_literals;

class ReliabilityTest : public QObject {
  Q_OBJECT
 private:
  static bool WriteFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
  }

 private slots:
  void visibilityMigrationRunsOnce() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("settings.ini");
    {
      QSettings settings(path, QSettings::IniFormat);
      settings.setValue("visibility", 1);  // legacy self-share
      QCOMPARE(scotty::LoadVisibility(settings), 3);
      settings.setValue("visibility", 1);  // user selects modern Contacts
    }
    QSettings restarted(path, QSettings::IniFormat);
    QCOMPARE(scotty::LoadVisibility(restarted), 1);
    QCOMPARE(scotty::LoadVisibility(restarted), 1);
  }

  void visibilityDefaultsAndRoundTrips() {
    QTemporaryDir directory;
    QSettings settings(directory.filePath("settings.ini"), QSettings::IniFormat);
    QCOMPARE(scotty::LoadVisibility(settings), 0);
    for (int visibility : {0, 1, 2, 3}) {
      settings.setValue("visibility", visibility);
      QCOMPARE(scotty::LoadVisibility(settings), visibility);
    }
    for (int invalid : {-1, 4, 99}) {
      settings.setValue("visibility", invalid);
      QCOMPARE(scotty::LoadVisibility(settings), 0);
    }
  }

  void slowShutdownDoesNotReleaseOwner() {
    std::promise<std::function<void()>> callback_ready;
    std::promise<void> slow;
    auto callback = callback_ready.get_future();
    auto slow_signal = slow.get_future();
    auto shutdown = std::async(std::launch::async, [&] {
      scotty::AwaitShutdown(
          [&](auto complete) { callback_ready.set_value(std::move(complete)); },
          [&] { slow.set_value(); }, 1ms);
    });
    auto complete = callback.get();
    const bool warned = slow_signal.wait_for(1s) == std::future_status::ready;
    const bool still_waiting = shutdown.wait_for(0ms) == std::future_status::timeout;
    complete();  // release even if an assertion below fails
    shutdown.get();
    QVERIFY(warned);
    QVERIFY(still_waiting);
  }

  void synchronousShutdownCompletesWithoutWarning() {
    bool warned = false;
    scotty::AwaitShutdown([](auto complete) { complete(); },
                         [&] { warned = true; }, 0ms);
    QVERIFY(!warned);
  }

  void wifiNoticeIsInformationalAndCanUnsubscribe() {
    int count = 0;
    nearby::linux::SetWifiDisruptionCallback([&] {
      ++count;
      // A consumer may detach while handling the event; no lock is held.
      nearby::linux::SetWifiDisruptionCallback({});
    });
    nearby::linux::NotifyWifiDisruption();
    nearby::linux::NotifyWifiDisruption();
    QCOMPARE(count, 1);
  }

  void newerSelectionSupersedesCompression() {
    QTemporaryDir directory;
    QVERIFY(QDir().mkpath(directory.filePath("old")));
    QVERIFY(WriteFile(directory.filePath("old/data"), "old data"));
    QVERIFY(WriteFile(directory.filePath("new.txt"), "new data"));
    // No event loop is run between starts: even a queued startup failure from
    // the old batch must never overwrite the new selection or show an error.
    SendPreparation preparation(nullptr, directory.filePath("missing-zip"));
    QList<PreparedSend> sends;
    connect(&preparation, &SendPreparation::prepared, this,
            [&](const PreparedSend& send) { sends.append(send); });
    QSignalSpy errors(&preparation, &SendPreparation::error);
    preparation.Prepare({directory.filePath("old")});
    preparation.Prepare({directory.filePath("new.txt")});
    QTest::qWait(100);
    QCOMPARE(sends.size(), 1);
    QCOMPARE(sends.first().paths, QStringList{directory.filePath("new.txt")});
    QCOMPARE(errors.count(), 0);
  }

  void cancelledPreparationDoesNotStageFiles() {
    QTemporaryDir directory;
    QVERIFY(QDir().mkpath(directory.filePath("folder")));
    SendPreparation preparation(nullptr, directory.filePath("missing-zip"));
    int prepared = 0;
    connect(&preparation, &SendPreparation::prepared, this,
            [&](const PreparedSend&) { ++prepared; });
    QSignalSpy errors(&preparation, &SendPreparation::error);
    preparation.Prepare({directory.filePath("folder")});
    preparation.Cancel();
    QTest::qWait(100);
    QCOMPARE(prepared, 0);
    QCOMPARE(errors.count(), 0);
  }

  void missingZipSettlesBatchOnce() {
    QTemporaryDir directory;
    QVERIFY(QDir().mkpath(directory.filePath("folder")));
    SendPreparation preparation(nullptr, directory.filePath("missing-zip"));
    int prepared = 0;
    connect(&preparation, &SendPreparation::prepared, this,
            [&](const PreparedSend& send) {
              QVERIFY(send.paths.isEmpty());
              ++prepared;
            });
    QSignalSpy errors(&preparation, &SendPreparation::error);
    preparation.Prepare({directory.filePath("folder")});
    QTRY_COMPARE(prepared, 1);
    QCOMPARE(errors.count(), 1);
  }

  void archiveSurvivesReselectionAndIsCleanedUp() {
    const QString zip = QStandardPaths::findExecutable("zip");
    QVERIFY2(!zip.isEmpty(), "Install zip to run the compression regression test");
    QTemporaryDir directory;
    QVERIFY(QDir().mkpath(directory.filePath("-folder")));
    QVERIFY(WriteFile(directory.filePath("-folder/data"), "file contents"));
    QString archive;
    {
      SendPreparation preparation(nullptr, zip);
      connect(&preparation, &SendPreparation::prepared, this,
              [&](const PreparedSend& send) {
                if (!send.paths.isEmpty()) archive = send.paths.first();
              });
      preparation.Prepare({directory.filePath("-folder")});
      QTRY_VERIFY(!archive.isEmpty());
      QVERIFY(QFileInfo::exists(archive));
      preparation.Prepare({});
      QVERIFY(QFileInfo::exists(archive));  // an engine may still be reading it
    }
    QVERIFY(!QFileInfo::exists(archive));
  }

  void mixedBatchKeepsFilesAndWaitsForEveryFolder() {
    const QString zip = QStandardPaths::findExecutable("zip");
    QVERIFY(!zip.isEmpty());
    QTemporaryDir directory;
    QVERIFY(QDir().mkpath(directory.filePath("one")));
    QVERIFY(QDir().mkpath(directory.filePath("two")));
    QVERIFY(WriteFile(directory.filePath("one/data"), "one"));
    QVERIFY(WriteFile(directory.filePath("two/data"), "two"));
    QVERIFY(WriteFile(directory.filePath("plain.txt"), "plain"));
    QVERIFY(WriteFile(directory.filePath("empty.txt"), ""));
    SendPreparation preparation(nullptr, zip);
    QList<PreparedSend> sends;
    connect(&preparation, &SendPreparation::prepared, this,
            [&](const PreparedSend& send) { sends.append(send); });
    preparation.Prepare({directory.filePath("one"), directory.filePath("two"),
                         directory.filePath("plain.txt"), directory.filePath("empty.txt")});
    QTRY_COMPARE(sends.size(), 1);
    QCOMPARE(sends.first().paths.size(), 3);
    QCOMPARE(sends.first().skipped_empty, 1);
    QVERIFY(sends.first().names.contains("one.zip"));
    QVERIFY(sends.first().names.contains("two.zip"));
    QVERIFY(sends.first().names.contains("plain.txt"));
  }
};

QTEST_GUILESS_MAIN(ReliabilityTest)
#include "reliability_test.moc"
