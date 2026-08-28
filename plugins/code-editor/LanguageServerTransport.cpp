#include "LanguageServerTransport.h"

#include <QJsonDocument>
#include <QThread>

#include <utility>

namespace slotdeck::plugins::codeeditor {

constexpr qsizetype transportMaximumMessageSize = 16 * 1024 * 1024;
constexpr qsizetype transportMaximumHeaderSize = 8 * 1024;

LanguageServerTransport::LanguageServerTransport(ResolvedLanguageServer server, QString rootPath) : m_server(std::move(server)), m_rootPath(std::move(rootPath)) {}

// A server started again after it died reuses its process object, so the buffer and the failure it reported belong to the run that ended.
void LanguageServerTransport::start() {
    if (m_process != nullptr) {
        if (m_process->state() == QProcess::NotRunning) {
            m_inputBuffer.clear();
            m_aborted = false;
            m_startFailureReported = false;
            m_process->start();
        }
        return;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setProgram(m_server.executablePath);
    m_process->setArguments(m_server.arguments);
    m_process->setWorkingDirectory(m_rootPath);
    // clang-format off
    connect(m_process, &QProcess::started, this, [this]() { emit started(); });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() { readOutput(); });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() { const QString text = QString(m_diagnosticDecoder(m_process->readAllStandardError())).trimmed(); if (!text.isEmpty() && !m_aborted) { emit diagnosticText(text); } });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) { if (m_aborted) { return; } if (error != QProcess::FailedToStart) { emit diagnosticText(m_process->errorString()); return; } if (!m_startFailureReported) { m_startFailureReported = true; emit startFailed(m_process->errorString()); } });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) { emit exited(exitCode); });
    // clang-format on
    m_process->start();
}

// The message is serialized here as well, because a document of any size would otherwise be encoded where the interface runs.
void LanguageServerTransport::send(const QJsonObject& message) {
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        return;
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process->write("Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
}

void LanguageServerTransport::requestTermination() {
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
    }
}

void LanguageServerTransport::kill() {
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
}

// The process is detached and killed rather than waited on, so no caller of this ever blocks on a child exiting.
void LanguageServerTransport::releaseProcess() {
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        return;
    }

    QProcess* released = m_process;
    m_process = nullptr;
    // A read already queued is still delivered, so the handlers leave with the process they were reading.
    released->disconnect(this);
    released->setParent(nullptr);
    connect(released, &QProcess::finished, released, &QObject::deleteLater);
    released->kill();
}

// A server never outlives the transport that owns it, so Qt never has to clean up a process that is still running.
LanguageServerTransport::~LanguageServerTransport() {
    releaseProcess();
}

void LanguageServerTransport::shutdown() {
    releaseProcess();

    if (QThread* owning = thread(); owning != nullptr) {
        owning->quit();
    }
}

void LanguageServerTransport::readOutput() {
    m_inputBuffer.append(m_process->readAllStandardOutput());

    if (m_inputBuffer.size() > transportMaximumMessageSize + transportMaximumHeaderSize) {
        abort(QStringLiteral("The language server response exceeded the permitted size"));
        return;
    }

    parseMessages();
}

bool LanguageServerTransport::readHeader(qsizetype headerEnd, qsizetype& contentLength) {
    bool found = false;

    for (const auto& line : m_inputBuffer.left(headerEnd).split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.toLower().startsWith("content-length:")) {
            continue;
        }
        if (found) {
            abort(QStringLiteral("The language server returned duplicate content lengths"));
            return false;
        }
        found = true;
        bool valid = false;
        contentLength = trimmed.mid(15).trimmed().toLongLong(&valid);
        if (!valid || contentLength < 0 || contentLength > transportMaximumMessageSize) {
            abort(QStringLiteral("The language server returned an invalid content length"));
            return false;
        }
    }

    if (!found) {
        abort(QStringLiteral("The language server response omitted its content length"));
        return false;
    }

    return true;
}

void LanguageServerTransport::parseMessages() {
    while (!m_aborted) {
        const qsizetype headerEnd = m_inputBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (m_inputBuffer.size() > transportMaximumHeaderSize) {
                abort(QStringLiteral("The language server response header exceeded the permitted size"));
            }
            return;
        }
        if (headerEnd > transportMaximumHeaderSize) {
            abort(QStringLiteral("The language server response header exceeded the permitted size"));
            return;
        }

        qsizetype contentLength = -1;
        if (!readHeader(headerEnd, contentLength)) {
            return;
        }
        if (m_inputBuffer.size() < headerEnd + 4 + contentLength) {
            return;
        }

        const QByteArray payload = m_inputBuffer.mid(headerEnd + 4, contentLength);
        m_inputBuffer.remove(0, headerEnd + 4 + contentLength);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            abort(QStringLiteral("The language server returned invalid JSON"));
            return;
        }
        if (document.object().value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
            abort(QStringLiteral("The language server returned an invalid JSON-RPC version"));
            return;
        }
        emit messageReceived(document.object());
    }
}

void LanguageServerTransport::abort(const QString& message) {
    m_aborted = true;
    m_startFailureReported = true;
    emit protocolFailed(message);
    kill();
}

} // namespace slotdeck::plugins::codeeditor
