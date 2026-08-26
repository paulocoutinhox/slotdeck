#pragma once

#include "terminal/platform/IPtyBackend.h"

#include <QByteArray>
#include <QSocketNotifier>
#include <QTimer>

#include <memory>

namespace slotdeck::terminalcore {

class PosixPtyBackend final : public IPtyBackend {
    Q_OBJECT

  public:
    explicit PosixPtyBackend(QObject* parent = nullptr);
    ~PosixPtyBackend() override;

    [[nodiscard]] utils::Result<void> start(const ShellProfile& profile, const QString& workingDirectory, const QString& historyFile, int columns, int rows) override;
    [[nodiscard]] utils::Result<void> write(const QByteArray& bytes) override;
    [[nodiscard]] utils::Result<void> resize(int columns, int rows, int cellWidth, int cellHeight) override;
    void setOutputPaused(bool paused) override;
    void terminate() override;
    [[nodiscard]] bool running() const override;

  private slots:
    void drainOutput();
    void flushInput();
    void checkProcess();
    void refreshWorkingDirectory();

  private:
    void closeDescriptor();
    void releaseNotifier(std::unique_ptr<QSocketNotifier>& notifier);
    void releaseNotifiers();
    void finishProcess(int status);

    int m_descriptor{-1};
    qint64 m_childProcessId{-1};
    QByteArray m_pendingInput;
    std::unique_ptr<QSocketNotifier> m_readNotifier;
    std::unique_ptr<QSocketNotifier> m_writeNotifier;
    QTimer m_exitTimer;
    QTimer m_directoryTimer;
    QString m_lastWorkingDirectory;
    bool m_outputPaused{false};
};

} // namespace slotdeck::terminalcore
