#include "AiMcpClient.h"

#include "AiMcpTransport.h"
#include "AiNetwork.h"
#include "AiProviderCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QUrl>
#include <QtConcurrent>

#include <memory>
#include <utility>

namespace slotdeck::plugins::ai {

constexpr int methodNotFound = -32601;
constexpr int internalError = -32603;
constexpr int transportDrainTimeoutMs = 5000;

class AiMcpClientHelper final {
  public:
    static QJsonObject clientInformation();
    static QMutex& transportGuard();
    static QSet<QThread*>& liveTransports();
};

// Every transport thread is registered while it runs, because the process must not end while one of them is still inside the code it was given.
QMutex& AiMcpClientHelper::transportGuard() {
    static QMutex guard;
    return guard;
}

QSet<QThread*>& AiMcpClientHelper::liveTransports() {
    static QSet<QThread*> threads;
    return threads;
}

void AiMcpClient::drainTransports() {
    QSet<QThread*> pending;
    {
        const QMutexLocker locked(&AiMcpClientHelper::transportGuard());
        pending = AiMcpClientHelper::liveTransports();
    }

    for (auto* thread : pending) {
        thread->quit();
        thread->wait(transportDrainTimeoutMs);
    }
}

QJsonObject AiMcpClientHelper::clientInformation() {
    return {{QStringLiteral("name"), QStringLiteral("SlotDeck")}, {QStringLiteral("version"), QCoreApplication::applicationVersion()}};
}

AiMcpClient::AiMcpClient(McpServerDescriptor descriptor, QObject* parent) : QObject(parent), m_descriptor(std::move(descriptor)) {
    m_startTimeout.setSingleShot(true);
    // clang-format off
    connect(&m_startTimeout, &QTimer::timeout, this, [this]() { if (!m_ready) { reportFailure({"ai_mcp_start_timeout", "The MCP server did not complete initialization", m_descriptor.id}); stop(); } });
    // clang-format on
}

AiMcpClient::~AiMcpClient() {
    releaseTransport();
}

// Sampling is declared only when the server is explicitly allowed to spend the configured model.
QJsonObject AiMcpClient::capabilities() const {
    QJsonObject declared{{QStringLiteral("roots"), QJsonObject{{QStringLiteral("listChanged"), true}}}};

    if (m_descriptor.samplingEnabled && m_samplingHandler) {
        declared.insert(QStringLiteral("sampling"), QJsonObject{});
    }

    return declared;
}

void AiMcpClient::setSamplingHandler(McpSamplingHandler handler) {
    m_samplingHandler = std::move(handler);
}

bool AiMcpClient::ready() const {
    return m_ready;
}

const QString& AiMcpClient::serverId() const {
    return m_descriptor.id;
}

const QVector<McpToolDescriptor>& AiMcpClient::tools() const {
    return m_tools;
}

const QJsonObject& AiMcpClient::serverCapabilities() const {
    return m_capabilities;
}

void AiMcpClient::start() {
    if (m_running) {
        reportFailure({"ai_mcp_busy", "The MCP server is already running", m_descriptor.id});
        return;
    }

    if (m_descriptor.transport == McpTransport::Stdio && m_descriptor.command.trimmed().isEmpty()) {
        reportFailure({"ai_mcp_invalid", "The MCP server command is required", m_descriptor.id});
        return;
    }

    if (m_descriptor.transport == McpTransport::Http) {
        const QUrl endpoint(m_descriptor.url);
        if (!endpoint.isValid() || endpoint.host().isEmpty() || (endpoint.scheme() != QStringLiteral("http") && endpoint.scheme() != QStringLiteral("https"))) {
            reportFailure({"ai_mcp_invalid", "The MCP server address is invalid", m_descriptor.url});
            return;
        }
    }

    m_stopping = false;
    m_ready = false;
    m_sessionId.clear();

    if (m_descriptor.transport == McpTransport::Http) {
        startHttp();
        performInitialize();
        return;
    }

    startStdio();
    performInitialize();
}

void AiMcpClient::startStdio() {
    auto* transportThread = new QThread;
    auto* transport = new AiMcpTransport(m_descriptor.command, m_descriptor.arguments, m_descriptor.workdir);
    m_transport = transport;
    transport->moveToThread(transportThread);
    {
        const QMutexLocker locked(&AiMcpClientHelper::transportGuard());
        AiMcpClientHelper::liveTransports().insert(transportThread);
    }
    // clang-format off
    connect(transportThread, &QThread::finished, transportThread, [transportThread]() { const QMutexLocker locked(&AiMcpClientHelper::transportGuard()); AiMcpClientHelper::liveTransports().remove(transportThread); });
    connect(transportThread, &QThread::finished, transport, &QObject::deleteLater);
    connect(transportThread, &QThread::finished, transportThread, &QObject::deleteLater);
    connect(transport, &AiMcpTransport::messageReceived, this, [this](const QJsonObject& message) { dispatch(message); });
    connect(transport, &AiMcpTransport::failed, this, [this](const QString& code, const QString& message) { if (!m_stopping) { reportFailure({code.toUtf8().constData(), message, m_descriptor.id}); stop(); } });
    connect(transport, &AiMcpTransport::exited, this, [this](int exitCode) { m_running = false; if (!m_stopping) { m_ready = false; completeAll({"ai_mcp_failed", QStringLiteral("The MCP server exited with code %1").arg(QString::number(exitCode)), m_descriptor.id}); } });
    // clang-format on
    transportThread->start();
    m_running = true;
    QMetaObject::invokeMethod(transport, "start", Qt::QueuedConnection);
    m_startTimeout.start(aiLimits().serverStartTimeoutMs);
}

void AiMcpClient::performInitialize() {
    const QJsonObject parameters{{QStringLiteral("protocolVersion"), QString::fromLatin1(mcpProtocolVersion)}, {QStringLiteral("capabilities"), capabilities()}, {QStringLiteral("clientInfo"), AiMcpClientHelper::clientInformation()}};
    // clang-format off
    request(QStringLiteral("initialize"), parameters, [this](utils::Result<QJsonObject> result) {
        if (!result.hasValue()) {
            reportFailure(result.error());
            return;
        }
        const QString negotiated = result.value().value(QStringLiteral("protocolVersion")).toString();
        if (negotiated.isEmpty()) {
            reportFailure({"ai_mcp_protocol_invalid", "The MCP server did not negotiate a protocol version", m_descriptor.id});
            stop();
            return;
        }
        m_capabilities = result.value().value(QStringLiteral("capabilities")).toObject();
        m_ready = true;
        m_startTimeout.stop();
        notify(QStringLiteral("notifications/initialized"), {});
        emit initialized();
        refreshTools();
    });
    // clang-format on
}

void AiMcpClient::stop() {
    m_startTimeout.stop();

    if (m_descriptor.transport == McpTransport::Http) {
        deleteSession();
    }

    cancelPending();
    m_stopping = true;
    m_ready = false;
    completeAll({"ai_mcp_stopped", "The MCP server was stopped", m_descriptor.id});
    m_running = false;
    releaseTransport();
}

// The transport ends its process and its own thread in one step, so a client that is started again never leaves the previous one behind.
void AiMcpClient::releaseTransport() {
    if (m_transport == nullptr) {
        return;
    }

    QObject* released = m_transport;
    m_transport = nullptr;
    released->disconnect(this);
    QMetaObject::invokeMethod(released, "shutdown", Qt::QueuedConnection);
}

void AiMcpClient::request(const QString& method, const QJsonObject& parameters, const McpReply& reply) {
    if (m_descriptor.transport == McpTransport::Stdio && !m_running) {
        reply(utils::Result<QJsonObject>::failure({"ai_mcp_unavailable", "The MCP server is not running", m_descriptor.id}));
        return;
    }

    const qint64 id = ++m_nextId;
    m_pending.insert(id, reply);
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), parameters}});
}

void AiMcpClient::callTool(const QString& name, const QJsonObject& arguments, const McpReply& reply) {
    request(QStringLiteral("tools/call"), {{QStringLiteral("name"), name}, {QStringLiteral("arguments"), arguments}}, reply);
}

void AiMcpClient::listResources(const QString& cursor, const McpReply& reply) {
    QJsonObject parameters;

    if (!cursor.isEmpty()) {
        parameters.insert(QStringLiteral("cursor"), cursor);
    }

    request(QStringLiteral("resources/list"), parameters, reply);
}

void AiMcpClient::readResource(const QString& uri, const McpReply& reply) {
    request(QStringLiteral("resources/read"), {{QStringLiteral("uri"), uri}}, reply);
}

void AiMcpClient::listPrompts(const QString& cursor, const McpReply& reply) {
    QJsonObject parameters;

    if (!cursor.isEmpty()) {
        parameters.insert(QStringLiteral("cursor"), cursor);
    }

    request(QStringLiteral("prompts/list"), parameters, reply);
}

void AiMcpClient::getPrompt(const QString& name, const QJsonObject& arguments, const McpReply& reply) {
    request(QStringLiteral("prompts/get"), {{QStringLiteral("name"), name}, {QStringLiteral("arguments"), arguments}}, reply);
}

void AiMcpClient::ping(const McpReply& reply) {
    request(QStringLiteral("ping"), {}, reply);
}

void AiMcpClient::dispatch(const QJsonObject& message) {
    if (message.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0")) {
        reportFailure({"ai_mcp_message_invalid", "The MCP server returned an invalid JSON-RPC version", m_descriptor.id});
        stop();
        return;
    }

    const bool hasMethod = message.contains(QStringLiteral("method"));
    const bool hasId = message.contains(QStringLiteral("id"));

    if (hasMethod && hasId) {
        handleServerRequest(message);
        return;
    }

    if (hasMethod) {
        handleNotification(message.value(QStringLiteral("method")).toString(), message.value(QStringLiteral("params")).toObject());
        return;
    }

    if (!hasId) {
        return;
    }

    const auto pending = m_pending.find(message.value(QStringLiteral("id")).toInteger(-1));

    if (pending == m_pending.end()) {
        return;
    }

    const McpReply reply = pending.value();
    m_pending.erase(pending);

    if (message.contains(QStringLiteral("error"))) {
        const QJsonObject error = message.value(QStringLiteral("error")).toObject();
        reply(utils::Result<QJsonObject>::failure({"ai_mcp_error", error.value(QStringLiteral("message")).toString(QStringLiteral("The MCP server reported an error")), QString::number(error.value(QStringLiteral("code")).toInteger(0))}));
        return;
    }

    reply(utils::Result<QJsonObject>::success(message.value(QStringLiteral("result")).toObject()));
}

// A server may call back into the client, and every method the client does not implement is answered explicitly.
void AiMcpClient::handleServerRequest(const QJsonObject& message) {
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue id = message.value(QStringLiteral("id"));

    if (method == QStringLiteral("ping")) {
        respond(id, {});
        return;
    }

    if (method == QStringLiteral("roots/list")) {
        QJsonArray roots;
        for (const auto& root : m_descriptor.roots) {
            roots.append(QJsonObject{{QStringLiteral("uri"), QUrl::fromLocalFile(root).toString()}, {QStringLiteral("name"), QDir(root).dirName()}});
        }
        respond(id, {{QStringLiteral("roots"), roots}});
        return;
    }

    if (method == QStringLiteral("sampling/createMessage")) {
        if (!m_descriptor.samplingEnabled || !m_samplingHandler) {
            respondWithError(id, methodNotFound, QStringLiteral("The client does not implement %1").arg(method));
            return;
        }
        // clang-format off
        const QPointer<AiMcpClient> guard(this);
        m_samplingHandler(message.value(QStringLiteral("params")).toObject(), m_descriptor.samplingMaximumTokens, [guard, id](utils::Result<QJsonObject> result) { if (guard.isNull()) { return; } if (result.hasValue()) { guard->respond(id, result.value()); } else { guard->respondWithError(id, internalError, result.error().message); } });
        // clang-format on
        return;
    }

    respondWithError(id, methodNotFound, QStringLiteral("The client does not implement %1").arg(method));
}

void AiMcpClient::handleNotification(const QString& method, const QJsonObject& parameters) {
    if (method == QStringLiteral("notifications/tools/list_changed")) {
        refreshTools();
    }

    if (method == QStringLiteral("notifications/progress")) {
        emit progressReported(parameters.value(QStringLiteral("progressToken")).toVariant().toString(), parameters.value(QStringLiteral("progress")).toDouble(), parameters.value(QStringLiteral("total")).toDouble(), parameters.value(QStringLiteral("message")).toString());
    }
}

void AiMcpClient::refreshTools() {
    // clang-format off
    request(QStringLiteral("tools/list"), {}, [this](utils::Result<QJsonObject> result) {
        if (!result.hasValue()) {
            reportFailure(result.error());
            return;
        }

        QVector<McpToolDescriptor> discovered;
        for (const auto& value : result.value().value(QStringLiteral("tools")).toArray()) {
            const QJsonObject tool = value.toObject();
            const QString name = tool.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                continue;
            }
            // A server that declares a tool read-only lets the agent run it beside another one instead of waiting for its turn.
            const QJsonObject annotations = tool.value(QStringLiteral("annotations")).toObject();
            const bool readOnly = annotations.value(QStringLiteral("readOnlyHint")).isBool() && annotations.value(QStringLiteral("readOnlyHint")).toBool();
            discovered.append({m_descriptor.id, name, tool.value(QStringLiteral("description")).toString(), tool.value(QStringLiteral("inputSchema")).toObject(), readOnly});
        }
        m_tools = std::move(discovered);
        emit toolsChanged();
    });
    // clang-format on
}

void AiMcpClient::notify(const QString& method, const QJsonObject& parameters) {
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), method}, {QStringLiteral("params"), parameters}});
}

void AiMcpClient::respond(const QJsonValue& id, const QJsonObject& result) {
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
}

void AiMcpClient::respondWithError(const QJsonValue& id, int code, const QString& message) {
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}});
}

void AiMcpClient::send(const QJsonObject& message) {
    if (m_descriptor.transport == McpTransport::Http) {
        post(message);
        return;
    }

    if (m_transport == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(m_transport, "send", Qt::QueuedConnection, Q_ARG(QJsonObject, message));
}

void AiMcpClient::startHttp() {
    m_startTimeout.start(aiLimits().serverStartTimeoutMs);
}

// The streamable transport posts every message to one endpoint and accepts either a single response or an event stream.
void AiMcpClient::post(const QJsonObject& message) {
    if (m_stopping) {
        return;
    }

    QNetworkRequest request{QUrl(m_descriptor.url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("accept"), QByteArrayLiteral("application/json, text/event-stream"));
    request.setRawHeader(QByteArrayLiteral("mcp-protocol-version"), QByteArrayLiteral(mcpProtocolVersion));
    request.setTransferTimeout(aiLimits().serverStartTimeoutMs);

    if (!m_sessionId.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("mcp-session-id"), m_sessionId);
    }

    if (!m_descriptor.apiKey.isEmpty()) {
        request.setRawHeader(QByteArrayLiteral("authorization"), QByteArrayLiteral("Bearer ") + m_descriptor.apiKey.toUtf8());
    }

    QNetworkReply* reply = m_network.post(request, QJsonDocument(message).toJson(QJsonDocument::Compact));
    auto answer = boundReply(reply, mcpMaximumMessageBytes);
    // clang-format off
    connect(reply, &QNetworkReply::finished, this, [this, reply, answer]() {
        reply->deleteLater();
        // An answer larger than the bound was stopped while it arrived, so nothing of it is dispatched.
        if (answer->truncated) {
            const utils::Error error{"ai_mcp_answer_too_large", "The MCP server answered with more than the permitted size", m_descriptor.id};
            reportFailure(error);
            completeAll(error);
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray session = reply->rawHeader(QByteArrayLiteral("mcp-session-id"));
        if (!session.isEmpty()) {
            m_sessionId = session;
        }

        // A terminated session is restarted from a fresh initialization rather than retried with the dead identity.
        if (status == 404 && !m_sessionId.isEmpty()) {
            m_sessionId.clear();
            reportFailure({"ai_mcp_session_expired", "The MCP session is no longer valid", m_descriptor.id});
            completeAll({"ai_mcp_session_expired", "The MCP session is no longer valid", m_descriptor.id});
            return;
        }
        if (reply->error() != QNetworkReply::NoError && status != 202) {
            const utils::Error error{"ai_mcp_failed", reply->errorString(), m_descriptor.id};
            reportFailure(error);
            completeAll(error);
            return;
        }
        answer->bytes.append(reply->read(mcpMaximumMessageBytes - answer->bytes.size()));
        consumeHttpPayload(reply->rawHeader(QByteArrayLiteral("content-type")), answer->bytes);
    });
    // clang-format on
}

// A server answers with content of any size, so the answer is turned into messages away from the interface and only the messages return to it.
void AiMcpClient::consumeHttpPayload(const QByteArray& contentType, const QByteArray& payload) {
    if (payload.isEmpty()) {
        return;
    }

    const bool eventStream = contentType.contains(QByteArrayLiteral("text/event-stream"));
    // clang-format off
    auto parsed = QtConcurrent::run([payload, eventStream]() {
        QVector<QJsonObject> messages;
        if (!eventStream) {
            const QJsonDocument document = QJsonDocument::fromJson(payload);
            if (document.isObject()) {
                messages.append(document.object());
            }
            return messages;
        }
        for (const auto& line : payload.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (!trimmed.startsWith(QByteArrayLiteral("data:"))) {
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(trimmed.mid(5).trimmed());
            if (document.isObject()) {
                messages.append(document.object());
            }
        }
        return messages;
    });
    parsed.then(this, [this](const QVector<QJsonObject>& messages) { for (const auto& message : messages) { dispatch(message); } });
    // clang-format on
}

void AiMcpClient::deleteSession() {
    if (m_sessionId.isEmpty()) {
        return;
    }

    QNetworkRequest request{QUrl(m_descriptor.url)};
    request.setRawHeader(QByteArrayLiteral("mcp-session-id"), m_sessionId);
    request.setRawHeader(QByteArrayLiteral("mcp-protocol-version"), QByteArrayLiteral(mcpProtocolVersion));
    m_sessionId.clear();
    QNetworkReply* reply = m_network.deleteResource(request);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

// A request the client abandons is cancelled at the server, so it stops spending work nobody will read.
void AiMcpClient::cancelPending() {
    if (m_stopping) {
        return;
    }

    for (auto entry = m_pending.constBegin(); entry != m_pending.constEnd(); ++entry) {
        notify(QStringLiteral("notifications/cancelled"), {{QStringLiteral("requestId"), entry.key()}, {QStringLiteral("reason"), QStringLiteral("The client stopped the execution")}});
    }
}

void AiMcpClient::completeAll(const utils::Error& error) {
    const auto pending = m_pending;
    m_pending.clear();

    for (const auto& reply : pending) {
        reply(utils::Result<QJsonObject>::failure(error));
    }
}

void AiMcpClient::reportFailure(const utils::Error& error) {
    emit failed(error);
}

} // namespace slotdeck::plugins::ai
