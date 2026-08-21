#pragma once

#include "utils/Result.h"

#include <QObject>
#include <QProcess>
#include <QTimer>

namespace slotdeck::plugins::ai {

// A shell writes colors, cursor moves and progress rewrites, so the readable text is what reaches the execution record.
[[nodiscard]] QString plainCommandOutput(QString& pending, const QString& chunk);

class AiCommandRunner final : public QObject {
    Q_OBJECT

  public:
    explicit AiCommandRunner(QObject* parent = nullptr);
    ~AiCommandRunner() override;

    void start(const QString& command, const QString& workdir, int timeoutSeconds);
    void cancel();
    [[nodiscard]] bool running() const;

  signals:
    void outputReceived(const QString& text);
    void finished(int exitCode, const QString& output);
    void failed(const utils::Error& error);

  private:
    void readOutput();
    void stopProcess();
    void completeProcess(int exitCode, QProcess::ExitStatus status);
    void reportFailure(const utils::Error& error);
    void release();

    QProcess* m_process{nullptr};
    QTimer m_timeout;
    QString m_output;
    QString m_pendingControl;
    bool m_completed{false};
    bool m_timedOut{false};
};

} // namespace slotdeck::plugins::ai
