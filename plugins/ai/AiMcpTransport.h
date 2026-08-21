#pragma once

#include "AiMcpClient.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace slotdeck::plugins::ai {

// The process of a server, the lines it writes and their JSON live on their own thread, because a server answers with content of any size.
class AiMcpTransport final : public QObject {
    Q_OBJECT

  public:
    AiMcpTransport(QString command, QStringList arguments, QString workdir);

  public slots:
    void start();
    void send(const QJsonObject& message);
    void requestTermination();
    void shutdown();

  signals:
    void messageReceived(const QJsonObject& message);
    void failed(const QString& code, const QString& message);
    void exited(int exitCode);

  private:
    void readMessages();

    QString m_command;
    QStringList m_arguments;
    QString m_workdir;
    QProcess* m_process{nullptr};
    QByteArray m_buffer;
    bool m_stopping{false};
};

} // namespace slotdeck::plugins::ai
