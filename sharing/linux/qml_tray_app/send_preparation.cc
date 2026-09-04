#include "send_preparation.h"

#include <QDir>
#include <QFileInfo>
#include <utility>

SendPreparation::SendPreparation(QObject* parent, QString zip_program)
    : QObject(parent), zip_program_(std::move(zip_program)) {}

SendPreparation::~SendPreparation() {
  Cancel();
  // Stop children before the archive directory owners are destroyed.
  const auto processes = processes_;
  for (const auto& process : processes) {
    if (process) delete process.data();
  }
}

void SendPreparation::Cancel() {
  ++generation_;
  const auto processes = processes_;
  for (const auto& process : processes) {
    if (process && process->state() != QProcess::NotRunning) process->kill();
  }
}

void SendPreparation::Prepare(const QStringList& inputs) {
  Cancel();
  const quint64 generation = generation_;
  struct Batch {
    PreparedSend send;
    int remaining = 0;
    QSharedPointer<QTemporaryDir> directory;
  };
  auto batch = QSharedPointer<Batch>::create();
  QStringList folders;
  for (const QString& input : inputs) {
    if (input.trimmed().isEmpty()) continue;
    const QFileInfo info(input);
    if (info.isDir()) {
      folders.append(info.absoluteFilePath());
    } else if (info.isFile() && info.size() > 0) {
      batch->send.paths.append(info.absoluteFilePath());
      batch->send.names.append(info.fileName());
    } else if (info.isFile()) {
      ++batch->send.skipped_empty;
    }
  }
  if (folders.isEmpty()) {
    emit prepared(batch->send);
    return;
  }

  batch->directory = QSharedPointer<QTemporaryDir>::create(
      QDir::tempPath() + QStringLiteral("/scotty-share-XXXXXX"));
  if (!batch->directory->isValid()) {
    emit error(QStringLiteral("Could not create a temporary folder for compression."));
    return;
  }
  emit compressing(folders.size());
  batch->remaining = folders.size();
  for (int index = 0; index < folders.size(); ++index) {
    const QFileInfo folder(folders[index]);
    const QString name = folder.fileName().isEmpty()
                             ? QStringLiteral("folder") : folder.fileName();
    const QString output_dir = batch->directory->path() + QLatin1Char('/') +
                               QString::number(index);
    QDir().mkpath(output_dir);
    const QString archive = output_dir + QLatin1Char('/') + name + QStringLiteral(".zip");
    auto* process = new QProcess(this);
    processes_.append(process);
    process->setWorkingDirectory(folder.absolutePath());
    // QProcess may emit errorOccurred and finished for one failure. Settle a
    // folder once, and let obsolete batches finish without touching the UI.
    auto settled = QSharedPointer<bool>::create(false);
    auto finish = [this, process, batch, settled, generation, archive, name](bool ok) {
      if (*settled) return;
      *settled = true;
      processes_.removeAll(process);
      process->deleteLater();
      if (generation != generation_) return;
      if (ok && QFileInfo::exists(archive)) {
        batch->send.paths.append(archive);
        batch->send.names.append(QFileInfo(archive).fileName());
      } else {
        emit error(QStringLiteral("Could not compress %1. Check that zip is installed and the folder is readable.").arg(name));
      }
      if (--batch->remaining == 0) {
        archives_.append(batch->directory);
        emit prepared(batch->send);
      }
    };
    connect(process, &QProcess::finished, this,
            [finish](int code, QProcess::ExitStatus status) {
              finish(status == QProcess::NormalExit && code == 0);
            });
    connect(process, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError error) {
              if (error == QProcess::FailedToStart) finish(false);
            });
    // Prefix the relative folder with ./ so names beginning with '-' are data.
    process->start(zip_program_, {QStringLiteral("-r"), QStringLiteral("-q"),
                                  archive, QStringLiteral("./") + folder.fileName()});
  }
}
