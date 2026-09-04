#ifndef SCOTTY_SEND_PREPARATION_H_
#define SCOTTY_SEND_PREPARATION_H_

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSharedPointer>
#include <QStringList>
#include <QTemporaryDir>

struct PreparedSend {
  QStringList paths;
  QStringList names;
  int skipped_empty = 0;
};

// Owns folder compression independently of discovery and transfers. A new
// selection or Cancel invalidates earlier work, including queued completions.
class SendPreparation : public QObject {
  Q_OBJECT
 public:
  explicit SendPreparation(QObject* parent = nullptr,
                           QString zip_program = QStringLiteral("zip"));
  ~SendPreparation() override;
  void Prepare(const QStringList& paths);
  void Cancel();

 signals:
  void prepared(const PreparedSend& send);
  void compressing(int count);
  void error(const QString& message);

 private:
  quint64 generation_ = 0;
  QString zip_program_;
  QList<QPointer<QProcess>> processes_;
  // Keep successful archives available for re-sending and active engine reads.
  // Clean them up when the controller is destroyed, after engine shutdown.
  QList<QSharedPointer<QTemporaryDir>> archives_;
};

#endif
