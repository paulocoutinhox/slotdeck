#include "AiTaskRepository.h"

#include "ui/Components.h"

#include "AiAgentPrompt.h"

#include "CronExpression.h"
#include "persistence/StoredValues.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimeZone>
#include <QUrl>

#include <limits>
#include <utility>

namespace slotdeck::plugins::ai {

class AiTaskRepositoryHelper final {
  public:
    static QString entryName(const QString& key, int index);
    static bool readPosition(const QVariant& value, int& output);
    static utils::Result<std::optional<TaskSchedule>> parseSchedule(const QVariantMap& row);
    static QString mcpTransportIdentifier(McpTransport transport);
    static std::optional<McpTransport> mcpTransportFromIdentifier(const QString& value);
    static utils::Result<McpServerDescriptor> validateMcpServer(const McpServerDescriptor& server);
    static utils::Result<ExecutionSettings> executionFromDocument(const QJsonObject& document, const ExecutionSettings& declared);
    static utils::Result<SearchSettings> searchFromDocument(const QJsonObject& document, const SearchSettings& declared);
    static utils::Result<SpeechSettings> speechFromDocument(const QJsonObject& document, const SpeechSettings& declared);
    static QJsonObject connectionDocument(const ModelConnection& connection);
    static QJsonObject mcpServerDocument(const McpServerDescriptor& server);
    static QJsonObject rateLimitDocument(const ProviderRateLimit& limit);
    static QJsonObject agentDocument(const AiAgent& agent);
    static utils::Result<AiAgent> agentFromDocument(const QJsonObject& document);
    static utils::Result<ProviderRateLimit> rateLimitFromDocument(const QJsonObject& document);
    static utils::Result<ModelConnection> connectionFromDocument(const QJsonObject& document);
    static utils::Result<McpServerDescriptor> mcpServerFromDocument(const QJsonObject& document);
    static AiSettings settingsFromDocument(const QJsonObject& document);
    static QJsonObject settingsDocument(const AiSettings& settings);
    static utils::Result<QVector<TaskExecution>> parseExecutions(const persistence::DatabaseRows& rows);
    static utils::Result<QVector<ExecutionLogEntry>> parseExecutionLogs(const persistence::DatabaseRows& rows);
    static utils::Result<QVector<ConversationMessage>> parseConversation(const persistence::DatabaseRows& rows);
};

bool AiTaskRepositoryHelper::readPosition(const QVariant& value, int& output) {
    qint64 position = -1;
    if (!persistence::readStoredInteger(value, position) || position < 0 || position > std::numeric_limits<int>::max()) {
        return false;
    }
    output = static_cast<int>(position);
    return true;
}

utils::Result<std::optional<TaskSchedule>> AiTaskRepositoryHelper::parseSchedule(const QVariantMap& row) {
    const QString kindName = row.value(QStringLiteral("schedule_kind")).toString();
    if (kindName.isEmpty()) {
        return utils::Result<std::optional<TaskSchedule>>::success(std::nullopt);
    }
    const auto kind = AiTaskRepository::parseScheduleKind(kindName);
    qint64 enabled = -1;
    qint64 intervalSeconds = 0;
    if (!kind.hasValue() || !persistence::readStoredInteger(row.value(QStringLiteral("schedule_enabled")), enabled) || (enabled != 0 && enabled != 1) || (!row.value(QStringLiteral("interval_seconds")).isNull() && !persistence::readStoredInteger(row.value(QStringLiteral("interval_seconds")), intervalSeconds))) {
        return utils::Result<std::optional<TaskSchedule>>::failure(!kind.hasValue() ? kind.error() : utils::Error{"ai_tasks_schedule_invalid", "A stored AI task schedule is invalid", kindName});
    }

    TaskSchedule schedule{kind.value(), enabled == 1, persistence::parseStoredTimestamp(row.value(QStringLiteral("once_at_utc"))), intervalSeconds, row.value(QStringLiteral("cron_expression")).toString(), row.value(QStringLiteral("time_zone_id")).toByteArray(), persistence::parseStoredTimestamp(row.value(QStringLiteral("next_run_at_utc"))), persistence::parseStoredTimestamp(row.value(QStringLiteral("last_triggered_at_utc")))};
    const auto validation = AiTaskRepository::validateSchedule(schedule);
    return validation.hasValue() ? utils::Result<std::optional<TaskSchedule>>::success(schedule) : utils::Result<std::optional<TaskSchedule>>::failure(validation.error());
}

AiTaskRepository::AiTaskRepository(PluginHost& host) : m_host(host) {}

utils::Result<void> AiTaskRepository::initialize() {
    const QStringList schema{QStringLiteral("CREATE TABLE ai_tasks_workspaces(id TEXT PRIMARY KEY, name TEXT NOT NULL, position INTEGER NOT NULL CHECK(position >= 0), active INTEGER NOT NULL CHECK(active IN (0, 1)), created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT"), QStringLiteral("CREATE TABLE ai_tasks_tasks(id TEXT PRIMARY KEY, workspace_id TEXT NOT NULL REFERENCES ai_tasks_workspaces(id) ON DELETE CASCADE, title TEXT NOT NULL, description TEXT NOT NULL, prompt TEXT NOT NULL, issue_url TEXT NOT NULL, agent_id TEXT NOT NULL, execution_kind TEXT NOT NULL CHECK(execution_kind IN ('agent', 'command')), workdir TEXT NOT NULL, command TEXT NOT NULL, command_timeout_seconds INTEGER NOT NULL CHECK(command_timeout_seconds >= 0), column_name TEXT NOT NULL CHECK(column_name IN ('todo', 'doing', 'blocked', 'review', 'done')), position INTEGER NOT NULL CHECK(position >= 0), created_at_utc TEXT NOT NULL, updated_at_utc TEXT NOT NULL) STRICT"), QStringLiteral("CREATE TABLE ai_tasks_schedules(task_id TEXT PRIMARY KEY NOT NULL REFERENCES ai_tasks_tasks(id) ON DELETE CASCADE, schedule_kind TEXT NOT NULL CHECK(schedule_kind IN ('once', 'interval', 'cron')), enabled INTEGER NOT NULL CHECK(enabled IN (0, 1)), once_at_utc TEXT, interval_seconds INTEGER CHECK(interval_seconds >= 60), cron_expression TEXT, time_zone_id TEXT NOT NULL, next_run_at_utc TEXT, last_triggered_at_utc TEXT) STRICT"), QStringLiteral("CREATE TABLE ai_tasks_queue(task_id TEXT PRIMARY KEY NOT NULL REFERENCES ai_tasks_tasks(id) ON DELETE CASCADE, queued_at_utc TEXT NOT NULL) STRICT"), QStringLiteral("CREATE TABLE ai_tasks_executions(id TEXT PRIMARY KEY, task_id TEXT NOT NULL REFERENCES ai_tasks_tasks(id) ON DELETE CASCADE, status TEXT NOT NULL CHECK(status IN ('running', 'succeeded', 'failed', 'cancelled')), started_at_utc TEXT NOT NULL, finished_at_utc TEXT, input_tokens INTEGER NOT NULL CHECK(input_tokens >= 0), output_tokens INTEGER NOT NULL CHECK(output_tokens >= 0), finish_reason TEXT NOT NULL, error_message TEXT NOT NULL, content TEXT NOT NULL, stop_reason TEXT NOT NULL CHECK(stop_reason IN ('answered', 'iteration-limit', 'output-budget', 'tool-repetition', 'cancelled', 'failed'))) STRICT"), QStringLiteral("CREATE TABLE ai_tasks_logs(id TEXT PRIMARY KEY, execution_id TEXT NOT NULL REFERENCES ai_tasks_executions(id) ON DELETE CASCADE, sequence INTEGER NOT NULL CHECK(sequence >= 0), timestamp_utc TEXT NOT NULL, level TEXT NOT NULL CHECK(level IN ('debug', 'info', 'warning', 'error')), kind TEXT NOT NULL, detail TEXT NOT NULL) STRICT"), QStringLiteral("CREATE TABLE ai_tasks_messages(id TEXT PRIMARY KEY, task_id TEXT NOT NULL REFERENCES ai_tasks_tasks(id) ON DELETE CASCADE, sequence INTEGER NOT NULL CHECK(sequence >= 0), role TEXT NOT NULL CHECK(role IN ('user', 'assistant', 'tool')), content TEXT NOT NULL, tool_calls TEXT NOT NULL, tool_call_id TEXT NOT NULL, summarized_until INTEGER NOT NULL CHECK(summarized_until >= 0), created_at_utc TEXT NOT NULL, image_data BLOB, image_media_type TEXT NOT NULL) STRICT"), QStringLiteral("CREATE INDEX ai_tasks_messages_task_index ON ai_tasks_messages(task_id, sequence)"), QStringLiteral("CREATE INDEX ai_tasks_schedules_due_index ON ai_tasks_schedules(enabled, next_run_at_utc) WHERE enabled = 1"), QStringLiteral("CREATE INDEX ai_tasks_tasks_workspace_index ON ai_tasks_tasks(workspace_id, column_name, position)"), QStringLiteral("CREATE INDEX ai_tasks_executions_task_index ON ai_tasks_executions(task_id, started_at_utc)"), QStringLiteral("CREATE INDEX ai_tasks_logs_execution_index ON ai_tasks_logs(execution_id, sequence)")};
    return m_host.migrateDatabase({{1, schema}});
}

QString AiTaskRepositoryHelper::mcpTransportIdentifier(McpTransport transport) {
    return transport == McpTransport::Http ? QStringLiteral("http") : QStringLiteral("stdio");
}

std::optional<McpTransport> AiTaskRepositoryHelper::mcpTransportFromIdentifier(const QString& value) {
    if (value == QStringLiteral("http")) {
        return McpTransport::Http;
    }
    if (value == QStringLiteral("stdio")) {
        return McpTransport::Stdio;
    }
    return std::nullopt;
}

utils::Result<McpServerDescriptor> AiTaskRepositoryHelper::validateMcpServer(const McpServerDescriptor& server) {
    const bool stdio = server.transport == McpTransport::Stdio;
    if (server.id.trimmed().isEmpty() || server.samplingMaximumTokens < 0 || (stdio ? server.command.trimmed().isEmpty() : server.url.trimmed().isEmpty())) {
        return utils::Result<McpServerDescriptor>::failure({"ai_mcp_server_invalid", "The MCP server is invalid", server.id});
    }
    return utils::Result<McpServerDescriptor>::success(server);
}

utils::Result<ExecutionSettings> AiTaskRepositoryHelper::executionFromDocument(const QJsonObject& document, const ExecutionSettings& declared) {
    ExecutionSettings settings = declared;
    if (!hasKnownKeys(document, {QStringLiteral("maximumIterations"), QStringLiteral("commandTimeoutSeconds"), QStringLiteral("parallelExecutions"), QStringLiteral("chatFontSize")}) || !readSettingsInteger(document, QStringLiteral("maximumIterations"), settings.maximumIterations) || !readSettingsInteger(document, QStringLiteral("commandTimeoutSeconds"), settings.commandTimeoutSeconds) || !readSettingsInteger(document, QStringLiteral("parallelExecutions"), settings.parallelExecutions) || !readSettingsInteger(document, QStringLiteral("chatFontSize"), settings.chatFontSize) || !ui::validContentFontSize(settings.chatFontSize) || settings.maximumIterations < 0 || settings.commandTimeoutSeconds < 0 || settings.parallelExecutions < 0) {
        return utils::Result<ExecutionSettings>::failure({"ai_tasks_settings_invalid", "The AI execution settings are invalid", {}});
    }
    return utils::Result<ExecutionSettings>::success(settings);
}

utils::Result<SearchSettings> AiTaskRepositoryHelper::searchFromDocument(const QJsonObject& document, const SearchSettings& declared) {
    SearchSettings settings = declared;
    QString provider = searchProviderIdentifier(settings.provider);
    if (!hasKnownKeys(document, {QStringLiteral("provider"), QStringLiteral("instanceUrl"), QStringLiteral("apiKey")}) || !readSettingsText(document, QStringLiteral("provider"), provider) || !readSettingsText(document, QStringLiteral("instanceUrl"), settings.instanceUrl) || !readSettingsText(document, QStringLiteral("apiKey"), settings.apiKey)) {
        return utils::Result<SearchSettings>::failure({"ai_tasks_settings_invalid", "The AI search settings are invalid", {}});
    }

    const auto parsed = searchProviderFromIdentifier(provider);
    if (!parsed.has_value()) {
        return utils::Result<SearchSettings>::failure({"ai_tasks_settings_invalid", "The AI search service is unknown", provider});
    }
    settings.provider = *parsed;
    return utils::Result<SearchSettings>::success(settings);
}

utils::Result<SpeechSettings> AiTaskRepositoryHelper::speechFromDocument(const QJsonObject& document, const SpeechSettings& declared) {
    SpeechSettings settings = declared;
    QString provider = speechProviderIdentifier(settings.provider);
    if (!hasKnownKeys(document, {QStringLiteral("provider"), QStringLiteral("voiceId"), QStringLiteral("apiKey")}) || !readSettingsText(document, QStringLiteral("provider"), provider) || !readSettingsText(document, QStringLiteral("voiceId"), settings.voiceId) || !readSettingsText(document, QStringLiteral("apiKey"), settings.apiKey)) {
        return utils::Result<SpeechSettings>::failure({"ai_tasks_settings_invalid", "The AI speech settings are invalid", {}});
    }

    const auto parsed = speechProviderFromIdentifier(provider);
    if (!parsed.has_value()) {
        return utils::Result<SpeechSettings>::failure({"ai_tasks_settings_invalid", "The AI speech service is unknown", provider});
    }
    settings.provider = *parsed;
    return utils::Result<SpeechSettings>::success(settings);
}

QJsonObject AiTaskRepositoryHelper::connectionDocument(const ModelConnection& connection) {
    return {{QStringLiteral("providerId"), connection.providerId}, {QStringLiteral("modelId"), connection.modelId}, {QStringLiteral("displayName"), connection.displayName}, {QStringLiteral("apiKey"), connection.apiKey}, {QStringLiteral("address"), connection.address}, {QStringLiteral("parameters"), connection.parameters}, {QStringLiteral("extraParameters"), connection.extraParameters}};
}

const AiAgent* findAgent(const QVector<AiAgent>& agents, const QString& agentId) {
    // clang-format off
    const auto found = std::find_if(agents.constBegin(), agents.constEnd(), [&agentId](const AiAgent& agent) { return agent.id == agentId; });
    // clang-format on
    return found == agents.constEnd() ? nullptr : &(*found);
}

utils::Result<AiAgent> validateAgent(const AiAgent& agent) {
    static const QRegularExpression identifierPattern{QStringLiteral("^[a-z0-9-]+$")};
    AiAgent validated = agent;
    validated.name = agent.name.trimmed();
    validated.description = agent.description.trimmed();
    validated.systemPrompt = agent.systemPrompt.trimmed();

    if (!identifierPattern.match(validated.id).hasMatch() || validated.name.isEmpty() || validated.systemPrompt.isEmpty() || validated.connectionKey.isEmpty()) {
        return utils::Result<AiAgent>::failure({"ai_agent_invalid", "The AI agent is invalid", validated.id});
    }
    if (validated.maximumIterations < 0 || validated.maximumIterations > aiLimits().maximumAgentIterations) {
        return utils::Result<AiAgent>::failure({"ai_agent_invalid", "The AI agent iteration limit is out of range", validated.id});
    }

    const QStringList unknown = unknownPromptTags(validated.systemPrompt);
    if (!unknown.isEmpty()) {
        return utils::Result<AiAgent>::failure({"ai_agent_tag_unknown", "The system prompt carries a tag nobody declares", unknown.join(QStringLiteral(", "))});
    }

    return utils::Result<AiAgent>::success(validated);
}

// An agent names a connection, so a set that names one nobody configured is refused where it is read.
utils::Result<void> validateAgentSet(const QVector<AiAgent>& agents, const QVector<ModelConnection>& connections) {
    QSet<QString> identifiers;
    for (const auto& agent : agents) {
        const auto validated = validateAgent(agent);
        if (!validated.hasValue()) {
            return utils::Result<void>::failure(validated.error());
        }
        if (identifiers.contains(agent.id)) {
            return utils::Result<void>::failure({"ai_agent_duplicate", "The AI agent identifier is already used", agent.id});
        }
        if (findConnection(connections, agent.connectionKey) == nullptr) {
            return utils::Result<void>::failure({"ai_connection_unknown", "The connection the agent runs on is not configured", agent.connectionKey});
        }
        identifiers.insert(agent.id);
    }

    return utils::Result<void>::success();
}

QJsonObject AiTaskRepositoryHelper::agentDocument(const AiAgent& agent) {
    return {{QStringLiteral("id"), agent.id}, {QStringLiteral("name"), agent.name}, {QStringLiteral("description"), agent.description}, {QStringLiteral("systemPrompt"), agent.systemPrompt}, {QStringLiteral("connectionKey"), agent.connectionKey}, {QStringLiteral("maximumIterations"), agent.maximumIterations}};
}

utils::Result<AiAgent> AiTaskRepositoryHelper::agentFromDocument(const QJsonObject& document) {
    AiAgent agent;
    const bool typed = hasKnownKeys(document, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("description"), QStringLiteral("systemPrompt"), QStringLiteral("connectionKey"), QStringLiteral("maximumIterations")}) && readSettingsText(document, QStringLiteral("id"), agent.id) && readSettingsText(document, QStringLiteral("name"), agent.name) && readSettingsText(document, QStringLiteral("description"), agent.description) && readSettingsText(document, QStringLiteral("systemPrompt"), agent.systemPrompt) && readSettingsText(document, QStringLiteral("connectionKey"), agent.connectionKey) && readSettingsInteger(document, QStringLiteral("maximumIterations"), agent.maximumIterations);
    if (!typed) {
        return utils::Result<AiAgent>::failure({"ai_agent_invalid", "The stored AI agent is invalid", agent.id});
    }

    return validateAgent(agent);
}

QJsonObject AiTaskRepositoryHelper::rateLimitDocument(const ProviderRateLimit& limit) {
    return {{QStringLiteral("providerId"), limit.providerId}, {QStringLiteral("minimumIntervalMs"), limit.minimumIntervalMs}, {QStringLiteral("maximumRequestsPerMinute"), limit.maximumRequestsPerMinute}, {QStringLiteral("maximumConcurrentRequests"), limit.maximumConcurrentRequests}};
}

// A limit names a provider the catalog declares, and every value it carries is a count nobody can write as a negative.
utils::Result<ProviderRateLimit> AiTaskRepositoryHelper::rateLimitFromDocument(const QJsonObject& document) {
    ProviderRateLimit limit;
    const bool typed = hasKnownKeys(document, {QStringLiteral("providerId"), QStringLiteral("minimumIntervalMs"), QStringLiteral("maximumRequestsPerMinute"), QStringLiteral("maximumConcurrentRequests")}) && readSettingsText(document, QStringLiteral("providerId"), limit.providerId) && readSettingsInteger(document, QStringLiteral("minimumIntervalMs"), limit.minimumIntervalMs) && readSettingsInteger(document, QStringLiteral("maximumRequestsPerMinute"), limit.maximumRequestsPerMinute) && readSettingsInteger(document, QStringLiteral("maximumConcurrentRequests"), limit.maximumConcurrentRequests);
    if (!typed || findProvider(limit.providerId) == nullptr) {
        return utils::Result<ProviderRateLimit>::failure({"ai_rate_limit_invalid", "The stored provider rate limit is invalid", limit.providerId});
    }
    if (limit.minimumIntervalMs < 0 || limit.minimumIntervalMs > aiLimits().maximumRequestDelayMs || limit.maximumRequestsPerMinute < 0 || limit.maximumRequestsPerMinute > aiLimits().maximumRequestsPerMinute || limit.maximumConcurrentRequests < 0 || limit.maximumConcurrentRequests > aiLimits().maximumConcurrentRequests) {
        return utils::Result<ProviderRateLimit>::failure({"ai_rate_limit_invalid", "The stored provider rate limit is out of range", limit.providerId});
    }

    return utils::Result<ProviderRateLimit>::success(limit);
}

QJsonObject AiTaskRepositoryHelper::mcpServerDocument(const McpServerDescriptor& server) {
    return {{QStringLiteral("id"), server.id}, {QStringLiteral("transport"), AiTaskRepositoryHelper::mcpTransportIdentifier(server.transport)}, {QStringLiteral("command"), server.command}, {QStringLiteral("arguments"), QJsonArray::fromStringList(server.arguments)}, {QStringLiteral("workdir"), server.workdir}, {QStringLiteral("url"), server.url}, {QStringLiteral("apiKey"), server.apiKey}, {QStringLiteral("roots"), QJsonArray::fromStringList(server.roots)}, {QStringLiteral("samplingEnabled"), server.samplingEnabled}, {QStringLiteral("samplingMaximumTokens"), server.samplingMaximumTokens}};
}

utils::Result<ModelConnection> AiTaskRepositoryHelper::connectionFromDocument(const QJsonObject& document) {
    if (!hasKnownKeys(document, {QStringLiteral("providerId"), QStringLiteral("modelId"), QStringLiteral("displayName"), QStringLiteral("apiKey"), QStringLiteral("address"), QStringLiteral("parameters"), QStringLiteral("extraParameters")})) {
        return utils::Result<ModelConnection>::failure({"ai_tasks_settings_invalid", "A stored AI connection carries an unknown value", {}});
    }

    ModelConnection connection;
    const bool typed = readSettingsText(document, QStringLiteral("providerId"), connection.providerId) && readSettingsText(document, QStringLiteral("modelId"), connection.modelId) && readSettingsText(document, QStringLiteral("displayName"), connection.displayName) && readSettingsText(document, QStringLiteral("apiKey"), connection.apiKey) && readSettingsText(document, QStringLiteral("address"), connection.address) && readSettingsObject(document, QStringLiteral("parameters"), connection.parameters) && readSettingsObject(document, QStringLiteral("extraParameters"), connection.extraParameters);
    if (!typed) {
        return utils::Result<ModelConnection>::failure({"ai_tasks_settings_invalid", "A stored AI connection is invalid", connection.providerId});
    }
    return validateConnection(connection);
}

utils::Result<McpServerDescriptor> AiTaskRepositoryHelper::mcpServerFromDocument(const QJsonObject& document) {
    if (!hasKnownKeys(document, {QStringLiteral("id"), QStringLiteral("transport"), QStringLiteral("command"), QStringLiteral("arguments"), QStringLiteral("workdir"), QStringLiteral("url"), QStringLiteral("apiKey"), QStringLiteral("roots"), QStringLiteral("samplingEnabled"), QStringLiteral("samplingMaximumTokens")})) {
        return utils::Result<McpServerDescriptor>::failure({"ai_tasks_settings_invalid", "A stored MCP server carries an unknown value", {}});
    }

    McpServerDescriptor server;
    QString transport = AiTaskRepositoryHelper::mcpTransportIdentifier(server.transport);
    const bool typed = readSettingsText(document, QStringLiteral("id"), server.id) && readSettingsText(document, QStringLiteral("transport"), transport) && readSettingsText(document, QStringLiteral("command"), server.command) && readSettingsText(document, QStringLiteral("workdir"), server.workdir) && readSettingsText(document, QStringLiteral("url"), server.url) && readSettingsText(document, QStringLiteral("apiKey"), server.apiKey) && readSettingsBool(document, QStringLiteral("samplingEnabled"), server.samplingEnabled) && readSettingsInteger(document, QStringLiteral("samplingMaximumTokens"), server.samplingMaximumTokens) && readSettingsTextList(document, QStringLiteral("arguments"), server.arguments) && readSettingsTextList(document, QStringLiteral("roots"), server.roots);
    const auto parsedTransport = AiTaskRepositoryHelper::mcpTransportFromIdentifier(transport);
    if (!typed || !parsedTransport.has_value()) {
        return utils::Result<McpServerDescriptor>::failure({"ai_tasks_settings_invalid", "A stored MCP server is invalid", server.id});
    }

    server.transport = *parsedTransport;
    return AiTaskRepositoryHelper::validateMcpServer(server);
}

QString AiTaskRepositoryHelper::entryName(const QString& key, int index) {
    return QStringLiteral("%1[%2]").arg(key, QString::number(index));
}

// The complete configuration of the plugin is one document, so a provider, a service or a server added later needs no schema.
// Every value the document does not carry, and every entry it carries in a shape this plugin cannot use, is simply the declared default.
AiSettings AiTaskRepositoryHelper::settingsFromDocument(const QJsonObject& document) {
    AiSettings settings;
    QVector<QJsonObject> connectionDocuments;
    QVector<QJsonObject> serverDocuments;
    QVector<QJsonObject> rateLimitDocuments;
    QVector<QJsonObject> agentDocuments;
    QJsonObject executionDocument;
    QJsonObject searchDocument;
    QJsonObject speechDocument;
    plugins::SettingsReader reader(document);
    reader.readText(QStringLiteral("defaultConnectionKey"), settings.defaultConnectionKey);
    reader.readObjectList(QStringLiteral("connections"), connectionDocuments);
    reader.readObjectList(QStringLiteral("mcpServers"), serverDocuments);
    reader.readObjectList(QStringLiteral("rateLimits"), rateLimitDocuments);
    reader.readObjectList(QStringLiteral("agents"), agentDocuments);
    reader.readObject(QStringLiteral("execution"), executionDocument);
    reader.readObject(QStringLiteral("search"), searchDocument);
    reader.readObject(QStringLiteral("speech"), speechDocument);

    for (const auto& value : connectionDocuments) {
        const auto connection = AiTaskRepositoryHelper::connectionFromDocument(value);
        if (connection.hasValue() && validateConnectionSet(settings.connections + QVector<ModelConnection>{connection.value()}).hasValue()) {
            settings.connections.append(connection.value());
        }
    }
    if (findConnection(settings.connections, settings.defaultConnectionKey) == nullptr) {
        settings.defaultConnectionKey.clear();
    }

    for (const auto& value : serverDocuments) {
        if (const auto server = AiTaskRepositoryHelper::mcpServerFromDocument(value); server.hasValue()) {
            settings.mcpServers.append(server.value());
        }
    }

    for (const auto& value : rateLimitDocuments) {
        const auto limit = AiTaskRepositoryHelper::rateLimitFromDocument(value);
        // One provider is limited once, because two rows for the same service would each claim to be the rule.
        // clang-format off
        const bool duplicated = limit.hasValue() && std::any_of(settings.rateLimits.constBegin(), settings.rateLimits.constEnd(), [&limit](const ProviderRateLimit& stored) { return stored.providerId == limit.value().providerId; });
        // clang-format on
        if (limit.hasValue() && !duplicated) {
            settings.rateLimits.append(limit.value());
        }
    }

    for (const auto& value : agentDocuments) {
        const auto agent = AiTaskRepositoryHelper::agentFromDocument(value);
        if (agent.hasValue() && validateAgentSet(settings.agents + QVector<AiAgent>{agent.value()}, settings.connections).hasValue()) {
            settings.agents.append(agent.value());
        }
    }

    if (const auto execution = AiTaskRepositoryHelper::executionFromDocument(executionDocument, settings.execution); execution.hasValue()) {
        settings.execution = execution.value();
    }
    if (const auto search = AiTaskRepositoryHelper::searchFromDocument(searchDocument, settings.search); search.hasValue()) {
        settings.search = search.value();
    }
    if (const auto speech = AiTaskRepositoryHelper::speechFromDocument(speechDocument, settings.speech); speech.hasValue()) {
        settings.speech = speech.value();
    }
    return settings;
}

QJsonObject AiTaskRepositoryHelper::settingsDocument(const AiSettings& settings) {
    QJsonArray connections;
    for (const auto& connection : settings.connections) {
        connections.append(AiTaskRepositoryHelper::connectionDocument(connection));
    }
    QJsonArray servers;
    for (const auto& server : settings.mcpServers) {
        servers.append(AiTaskRepositoryHelper::mcpServerDocument(server));
    }
    QJsonArray rateLimits;
    for (const auto& limit : settings.rateLimits) {
        rateLimits.append(AiTaskRepositoryHelper::rateLimitDocument(limit));
    }
    QJsonArray agents;
    for (const auto& agent : settings.agents) {
        agents.append(AiTaskRepositoryHelper::agentDocument(agent));
    }

    const QJsonObject execution{{QStringLiteral("maximumIterations"), settings.execution.maximumIterations}, {QStringLiteral("commandTimeoutSeconds"), settings.execution.commandTimeoutSeconds}, {QStringLiteral("parallelExecutions"), settings.execution.parallelExecutions}, {QStringLiteral("chatFontSize"), settings.execution.chatFontSize}};
    const QJsonObject search{{QStringLiteral("provider"), searchProviderIdentifier(settings.search.provider)}, {QStringLiteral("instanceUrl"), settings.search.instanceUrl}, {QStringLiteral("apiKey"), settings.search.apiKey}};
    const QJsonObject speech{{QStringLiteral("provider"), speechProviderIdentifier(settings.speech.provider)}, {QStringLiteral("voiceId"), settings.speech.voiceId}, {QStringLiteral("apiKey"), settings.speech.apiKey}};
    return {{QStringLiteral("connections"), connections}, {QStringLiteral("defaultConnectionKey"), settings.defaultConnectionKey}, {QStringLiteral("execution"), execution}, {QStringLiteral("search"), search}, {QStringLiteral("speech"), speech}, {QStringLiteral("mcpServers"), servers}, {QStringLiteral("rateLimits"), rateLimits}, {QStringLiteral("agents"), agents}};
}

AiSettings AiTaskRepository::settings() const {
    return AiTaskRepositoryHelper::settingsFromDocument(m_host.settings());
}

QFuture<utils::Result<void>> AiTaskRepository::saveSettings(const AiSettings& settings) {
    return m_host.saveSettings(AiTaskRepositoryHelper::settingsDocument(settings));
}

utils::Result<QVector<AiWorkspace>> AiTaskRepository::workspaces() const {
    const auto rows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT id, name, position, active, created_at_utc, updated_at_utc FROM ai_tasks_workspaces ORDER BY position"));
    if (!rows.hasValue()) {
        return utils::Result<QVector<AiWorkspace>>::failure(rows.error());
    }

    QVector<AiWorkspace> values;
    QSet<QString> identifiers;
    int activeWorkspaces = 0;
    for (const auto& row : rows.value()) {
        AiWorkspace workspace;
        workspace.id = row.value(QStringLiteral("id")).toString();
        workspace.name = row.value(QStringLiteral("name")).toString();
        workspace.active = row.value(QStringLiteral("active")).toInt() == 1;
        workspace.createdAtUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("created_at_utc")));
        workspace.updatedAtUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("updated_at_utc")));
        if (!AiTaskRepositoryHelper::readPosition(row.value(QStringLiteral("position")), workspace.position) || workspace.id.isEmpty() || identifiers.contains(workspace.id) || workspace.name.trimmed().isEmpty() || workspace.name != workspace.name.trimmed() || workspace.position != static_cast<int>(values.size()) || !persistence::validStoredTimestamp(workspace.createdAtUtc) || !persistence::validStoredTimestamp(workspace.updatedAtUtc) || workspace.updatedAtUtc < workspace.createdAtUtc) {
            return utils::Result<QVector<AiWorkspace>>::failure({"ai_tasks_workspace_invalid", "A stored AI workspace is invalid", workspace.id});
        }
        identifiers.insert(workspace.id);
        activeWorkspaces += workspace.active ? 1 : 0;
        values.append(std::move(workspace));
    }
    if (!values.isEmpty() && activeWorkspaces != 1) {
        return utils::Result<QVector<AiWorkspace>>::failure({"ai_tasks_workspace_invalid", "The stored AI workspace selection is invalid", {}});
    }
    return utils::Result<QVector<AiWorkspace>>::success(std::move(values));
}

utils::Result<QVector<AiTask>> AiTaskRepository::tasks() const {
    const auto rows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT ai_tasks_tasks.id, ai_tasks_tasks.workspace_id, ai_tasks_tasks.title, ai_tasks_tasks.description, ai_tasks_tasks.prompt, ai_tasks_tasks.issue_url, ai_tasks_tasks.agent_id, ai_tasks_tasks.execution_kind, ai_tasks_tasks.workdir, ai_tasks_tasks.command, ai_tasks_tasks.command_timeout_seconds, ai_tasks_tasks.column_name, ai_tasks_tasks.position, ai_tasks_tasks.created_at_utc, ai_tasks_tasks.updated_at_utc, ai_tasks_schedules.schedule_kind, ai_tasks_schedules.enabled AS schedule_enabled, ai_tasks_schedules.once_at_utc, ai_tasks_schedules.interval_seconds, ai_tasks_schedules.cron_expression, ai_tasks_schedules.time_zone_id, ai_tasks_schedules.next_run_at_utc, ai_tasks_schedules.last_triggered_at_utc FROM ai_tasks_tasks LEFT JOIN ai_tasks_schedules ON ai_tasks_schedules.task_id = ai_tasks_tasks.id ORDER BY ai_tasks_tasks.workspace_id, ai_tasks_tasks.column_name, ai_tasks_tasks.position, ai_tasks_tasks.id"));
    if (!rows.hasValue()) {
        return utils::Result<QVector<AiTask>>::failure(rows.error());
    }

    QVector<AiTask> values;
    QSet<QString> identifiers;
    for (const auto& row : rows.value()) {
        const auto column = parseColumn(row.value(QStringLiteral("column_name")).toString());
        if (!column.hasValue()) {
            return utils::Result<QVector<AiTask>>::failure(column.error());
        }
        const auto executionKind = parseTaskExecutionKind(row.value(QStringLiteral("execution_kind")).toString());
        if (!executionKind.hasValue()) {
            return utils::Result<QVector<AiTask>>::failure(executionKind.error());
        }
        const auto schedule = AiTaskRepositoryHelper::parseSchedule(row);
        if (!schedule.hasValue()) {
            return utils::Result<QVector<AiTask>>::failure(schedule.error());
        }

        AiTask task;
        task.id = row.value(QStringLiteral("id")).toString();
        task.workspaceId = row.value(QStringLiteral("workspace_id")).toString();
        task.title = row.value(QStringLiteral("title")).toString();
        task.description = row.value(QStringLiteral("description")).toString();
        task.prompt = row.value(QStringLiteral("prompt")).toString();
        task.column = column.value();
        task.createdAtUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("created_at_utc")));
        task.updatedAtUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("updated_at_utc")));
        task.schedule = schedule.value();
        task.executionKind = executionKind.value();
        task.issueUrl = row.value(QStringLiteral("issue_url")).toString();
        task.agentId = row.value(QStringLiteral("agent_id")).toString();
        task.workdir = row.value(QStringLiteral("workdir")).toString();
        task.command = row.value(QStringLiteral("command")).toString();
        if (!AiTaskRepositoryHelper::readPosition(row.value(QStringLiteral("command_timeout_seconds")), task.commandTimeoutSeconds) || !AiTaskRepositoryHelper::readPosition(row.value(QStringLiteral("position")), task.position) || identifiers.contains(task.id) || !validTask(task) || !persistence::validStoredTimestamp(task.createdAtUtc) || !persistence::validStoredTimestamp(task.updatedAtUtc) || task.updatedAtUtc < task.createdAtUtc) {
            return utils::Result<QVector<AiTask>>::failure({"ai_tasks_task_invalid", "A stored AI task is invalid", task.id});
        }
        identifiers.insert(task.id);
        values.append(std::move(task));
    }
    return utils::Result<QVector<AiTask>>::success(std::move(values));
}

utils::Result<QStringList> AiTaskRepository::queuedTaskIds() const {
    const auto rows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT task_id, queued_at_utc FROM ai_tasks_queue ORDER BY queued_at_utc, task_id"));
    if (!rows.hasValue()) {
        return utils::Result<QStringList>::failure(rows.error());
    }

    QStringList identifiers;
    for (const auto& row : rows.value()) {
        const QString taskId = row.value(QStringLiteral("task_id")).toString();
        if (taskId.isEmpty() || identifiers.contains(taskId) || !persistence::validStoredTimestamp(persistence::parseStoredTimestamp(row.value(QStringLiteral("queued_at_utc"))))) {
            return utils::Result<QStringList>::failure({"ai_tasks_queue_invalid", "A stored AI task queue entry is invalid", taskId});
        }
        identifiers.append(taskId);
    }
    return utils::Result<QStringList>::success(std::move(identifiers));
}

// A card says what happened to its task, so the outcome of its newest run is read back rather than stored a second time on the task itself.
// SQLite answers a bare column of a query carrying one maximum from the row that holds it, which is what names the newest run of each task.
utils::Result<QHash<QString, TaskOutcome>> AiTaskRepository::lastOutcomes() const {
    const auto rows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT task_id, status, error_message, stop_reason, MAX(started_at_utc) FROM ai_tasks_executions GROUP BY task_id"));
    if (!rows.hasValue()) {
        return utils::Result<QHash<QString, TaskOutcome>>::failure(rows.error());
    }

    QHash<QString, TaskOutcome> outcomes;
    for (const auto& row : rows.value()) {
        const QString taskId = row.value(QStringLiteral("task_id")).toString();
        const auto status = parseExecutionStatus(row.value(QStringLiteral("status")).toString());
        const auto stopReason = agentStopReasonFromName(row.value(QStringLiteral("stop_reason")).toString());
        if (taskId.isEmpty() || !status.hasValue() || !stopReason.has_value()) {
            return utils::Result<QHash<QString, TaskOutcome>>::failure({"ai_tasks_execution_invalid", "A stored AI execution outcome is invalid", taskId});
        }
        outcomes.insert(taskId, {status.value(), row.value(QStringLiteral("error_message")).toString(), stopReason.value()});
    }
    return utils::Result<QHash<QString, TaskOutcome>>::success(std::move(outcomes));
}

QString searchProviderIdentifier(SearchProvider provider) {
    if (provider == SearchProvider::Tavily) {
        return QStringLiteral("tavily");
    }
    if (provider == SearchProvider::SearxNg) {
        return QStringLiteral("searxng");
    }
    return QStringLiteral("brave");
}

std::optional<SearchProvider> searchProviderFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("brave")) {
        return SearchProvider::Brave;
    }
    if (identifier == QStringLiteral("tavily")) {
        return SearchProvider::Tavily;
    }
    if (identifier == QStringLiteral("searxng")) {
        return SearchProvider::SearxNg;
    }
    return std::nullopt;
}

// A hosted service has one published endpoint, so only a self-hosted instance carries the address the user provides.
QString searchAddress(const SearchSettings& settings) {
    if (settings.provider == SearchProvider::Tavily) {
        return QStringLiteral("https://api.tavily.com");
    }
    if (settings.provider == SearchProvider::SearxNg) {
        return settings.instanceUrl;
    }
    return QStringLiteral("https://api.search.brave.com");
}

QString searchProviderKeyVariable(SearchProvider provider) {
    if (provider == SearchProvider::Tavily) {
        return QStringLiteral("TAVILY_API_KEY");
    }
    if (provider == SearchProvider::SearxNg) {
        return QString{};
    }
    return QStringLiteral("BRAVE_API_KEY");
}

// A service exists with the credential reference it officially documents, exactly as a provider does.
SearchSettings declaredSearchSettings(SearchProvider provider) {
    return {provider, {}, defaultSecretReference(searchProviderKeyVariable(provider))};
}

SpeechSettings declaredSpeechSettings(SpeechProvider provider) {
    return {provider, speechProviderDefaultVoice(provider), defaultSecretReference(speechProviderKeyVariable(provider))};
}

QString speechProviderIdentifier(SpeechProvider provider) {
    return provider == SpeechProvider::OpenAi ? QStringLiteral("openai") : QStringLiteral("elevenlabs");
}

std::optional<SpeechProvider> speechProviderFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("elevenlabs")) {
        return SpeechProvider::ElevenLabs;
    }
    if (identifier == QStringLiteral("openai")) {
        return SpeechProvider::OpenAi;
    }
    return std::nullopt;
}

QString speechProviderAddress(SpeechProvider provider) {
    return provider == SpeechProvider::OpenAi ? QStringLiteral("https://api.openai.com/v1") : QStringLiteral("https://api.elevenlabs.io");
}

QString speechProviderKeyVariable(SpeechProvider provider) {
    return provider == SpeechProvider::OpenAi ? QStringLiteral("OPENAI_API_KEY") : QStringLiteral("ELEVENLABS_API_KEY");
}

QString speechProviderDefaultVoice(SpeechProvider provider) {
    return provider == SpeechProvider::OpenAi ? QStringLiteral("alloy") : QString{};
}

// The OpenAI voices are a closed set the service documents, while ElevenLabs answers its own catalog.
QStringList speechProviderDeclaredVoices(SpeechProvider provider) {
    if (provider != SpeechProvider::OpenAi) {
        return {};
    }
    return {QStringLiteral("alloy"), QStringLiteral("ash"), QStringLiteral("ballad"), QStringLiteral("coral"), QStringLiteral("echo"), QStringLiteral("fable"), QStringLiteral("nova"), QStringLiteral("onyx"), QStringLiteral("sage"), QStringLiteral("shimmer")};
}

utils::Result<QVector<TaskExecution>> AiTaskRepositoryHelper::parseExecutions(const persistence::DatabaseRows& rows) {
    QVector<TaskExecution> values;
    for (const auto& row : rows) {
        const auto status = AiTaskRepository::parseExecutionStatus(row.value(QStringLiteral("status")).toString());
        if (!status.hasValue()) {
            return utils::Result<QVector<TaskExecution>>::failure(status.error());
        }
        const auto stopReason = AiTaskRepository::agentStopReasonFromName(row.value(QStringLiteral("stop_reason")).toString());
        if (!stopReason.has_value()) {
            return utils::Result<QVector<TaskExecution>>::failure({"ai_tasks_execution_invalid", "A stored AI execution stop reason is invalid", row.value(QStringLiteral("stop_reason")).toString()});
        }

        TaskExecution execution;
        execution.id = row.value(QStringLiteral("id")).toString();
        execution.taskId = row.value(QStringLiteral("task_id")).toString();
        execution.status = status.value();
        execution.startedAtUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("started_at_utc")));
        execution.finishedAtUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("finished_at_utc")));
        execution.finishReason = row.value(QStringLiteral("finish_reason")).toString();
        execution.errorMessage = row.value(QStringLiteral("error_message")).toString();
        execution.content = row.value(QStringLiteral("content")).toString();
        execution.stopReason = stopReason.value();
        if (!persistence::readStoredInteger(row.value(QStringLiteral("input_tokens")), execution.inputTokens) || !persistence::readStoredInteger(row.value(QStringLiteral("output_tokens")), execution.outputTokens) || execution.id.isEmpty() || execution.inputTokens < 0 || execution.outputTokens < 0 || !persistence::validStoredTimestamp(execution.startedAtUtc)) {
            return utils::Result<QVector<TaskExecution>>::failure({"ai_tasks_execution_invalid", "A stored AI execution is invalid", execution.id});
        }
        if (execution.status != ExecutionStatus::Running && (!persistence::validStoredTimestamp(execution.finishedAtUtc) || execution.finishedAtUtc < execution.startedAtUtc)) {
            return utils::Result<QVector<TaskExecution>>::failure({"ai_tasks_execution_invalid", "A stored AI execution is invalid", execution.id});
        }
        values.append(std::move(execution));
    }
    return utils::Result<QVector<TaskExecution>>::success(std::move(values));
}

utils::Result<QVector<ExecutionLogEntry>> AiTaskRepositoryHelper::parseExecutionLogs(const persistence::DatabaseRows& rows) {
    QVector<ExecutionLogEntry> values;
    for (const auto& row : rows) {
        const auto level = AiTaskRepository::parseExecutionLogLevel(row.value(QStringLiteral("level")).toString());
        if (!level.hasValue()) {
            return utils::Result<QVector<ExecutionLogEntry>>::failure(level.error());
        }
        const auto kind = AiTaskRepository::parseExecutionLogKind(row.value(QStringLiteral("kind")).toString());
        if (!kind.hasValue()) {
            return utils::Result<QVector<ExecutionLogEntry>>::failure(kind.error());
        }

        ExecutionLogEntry entry;
        entry.id = row.value(QStringLiteral("id")).toString();
        entry.executionId = row.value(QStringLiteral("execution_id")).toString();
        entry.timestampUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("timestamp_utc")));
        entry.level = level.value();
        entry.kind = kind.value();
        entry.detail = row.value(QStringLiteral("detail")).toString();
        if (!persistence::readStoredInteger(row.value(QStringLiteral("sequence")), entry.sequence) || entry.sequence < 0 || entry.id.isEmpty() || entry.executionId.isEmpty() || !persistence::validStoredTimestamp(entry.timestampUtc)) {
            return utils::Result<QVector<ExecutionLogEntry>>::failure({"ai_tasks_execution_invalid", "A stored AI execution log entry is invalid", entry.id});
        }
        values.append(std::move(entry));
    }
    return utils::Result<QVector<ExecutionLogEntry>>::success(std::move(values));
}

QFuture<utils::Result<QVector<TaskExecution>>> AiTaskRepository::executions(const QString& taskId) {
    if (taskId.isEmpty()) {
        return QtFuture::makeReadyValueFuture(utils::Result<QVector<TaskExecution>>::failure({"ai_tasks_execution_invalid", "The AI task identity is invalid", {}}));
    }

    auto future = m_host.queryDatabase(QStringLiteral("SELECT id, task_id, status, started_at_utc, finished_at_utc, input_tokens, output_tokens, finish_reason, error_message, content, stop_reason FROM ai_tasks_executions WHERE task_id = ? ORDER BY started_at_utc DESC, id DESC LIMIT 100"), {taskId});
    // clang-format off
    return future.then([](utils::Result<persistence::DatabaseRows> result) { return result.hasValue() ? AiTaskRepositoryHelper::parseExecutions(result.value()) : utils::Result<QVector<TaskExecution>>::failure(result.error()); });
    // clang-format on
}

QString AiTaskRepository::conversationRoleName(ConversationRole role) {
    switch (role) {
    case ConversationRole::User:
        return QStringLiteral("user");
    case ConversationRole::Assistant:
        return QStringLiteral("assistant");
    case ConversationRole::Tool:
        return QStringLiteral("tool");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("user"));
}

utils::Result<ConversationRole> AiTaskRepository::parseConversationRole(const QString& value) {
    static const QHash<QString, ConversationRole> roles{{QStringLiteral("user"), ConversationRole::User}, {QStringLiteral("assistant"), ConversationRole::Assistant}, {QStringLiteral("tool"), ConversationRole::Tool}};
    const auto found = roles.constFind(value);
    return found == roles.constEnd() ? utils::Result<ConversationRole>::failure({"ai_conversation_invalid", "A stored conversation role is invalid", value}) : utils::Result<ConversationRole>::success(found.value());
}

utils::Result<QVector<ConversationMessage>> AiTaskRepositoryHelper::parseConversation(const persistence::DatabaseRows& rows) {
    QVector<ConversationMessage> messages;
    messages.reserve(rows.size());
    for (const auto& row : rows) {
        ConversationMessage message;
        message.id = row.value(QStringLiteral("id")).toString();
        message.taskId = row.value(QStringLiteral("task_id")).toString();
        message.content = row.value(QStringLiteral("content")).toString();
        message.toolCallId = row.value(QStringLiteral("tool_call_id")).toString();
        message.createdAtUtc = persistence::parseStoredTimestamp(row.value(QStringLiteral("created_at_utc")).toString());
        message.imageData = row.value(QStringLiteral("image_data")).toByteArray();
        message.imageMediaType = row.value(QStringLiteral("image_media_type")).toString().toUtf8();

        const auto role = AiTaskRepository::parseConversationRole(row.value(QStringLiteral("role")).toString());
        if (!role.hasValue()) {
            return utils::Result<QVector<ConversationMessage>>::failure(role.error());
        }
        message.role = role.value();

        const QJsonDocument calls = QJsonDocument::fromJson(row.value(QStringLiteral("tool_calls")).toString().toUtf8());
        if (!calls.isNull() && !calls.isArray()) {
            return utils::Result<QVector<ConversationMessage>>::failure({"ai_conversation_invalid", "A stored conversation tool call list is invalid", message.id});
        }
        message.toolCalls = calls.array();

        if (message.id.isEmpty() || message.taskId.isEmpty() || !persistence::readStoredInteger(row.value(QStringLiteral("sequence")), message.sequence) || message.sequence < 0 || !persistence::readStoredInteger(row.value(QStringLiteral("summarized_until")), message.summarizedUntil) || message.summarizedUntil < 0 || !message.createdAtUtc.isValid()) {
            return utils::Result<QVector<ConversationMessage>>::failure({"ai_conversation_invalid", "A stored conversation message is invalid", message.id});
        }
        messages.append(std::move(message));
    }

    std::sort(messages.begin(), messages.end(), [](const ConversationMessage& left, const ConversationMessage& right) { return left.sequence < right.sequence; });
    return utils::Result<QVector<ConversationMessage>>::success(messages);
}

// The newest page is read first like every other history in the project, and it is answered in the order it was written.
QFuture<utils::Result<QVector<ConversationMessage>>> AiTaskRepository::conversation(const QString& taskId, qint64 beforeSequence, int maximumMessages) {
    if (taskId.isEmpty() || maximumMessages <= 0 || maximumMessages > 100 || beforeSequence < 0) {
        return QtFuture::makeReadyValueFuture(utils::Result<QVector<ConversationMessage>>::failure({"ai_conversation_invalid", "The conversation request is invalid", taskId}));
    }

    const QString statement = beforeSequence == 0 ? QStringLiteral("SELECT id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, created_at_utc, image_data, image_media_type FROM ai_tasks_messages WHERE task_id = ? ORDER BY sequence DESC LIMIT %1").arg(maximumMessages) : QStringLiteral("SELECT id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, created_at_utc, image_data, image_media_type FROM ai_tasks_messages WHERE task_id = ? AND sequence < ? ORDER BY sequence DESC LIMIT %1").arg(maximumMessages);
    QVariantList bindings{taskId};
    if (beforeSequence > 0) {
        bindings.append(beforeSequence);
    }

    auto future = m_host.queryDatabase(statement, bindings);
    // clang-format off
    return future.then([](utils::Result<persistence::DatabaseRows> result) { return result.hasValue() ? AiTaskRepositoryHelper::parseConversation(result.value()) : utils::Result<QVector<ConversationMessage>>::failure(result.error()); });
    // clang-format on
}

utils::Result<QHash<QString, qint64>> AiTaskRepository::conversationSequences() const {
    const auto rows = m_host.queryBootstrapDatabase(QStringLiteral("SELECT task_id, MAX(sequence) AS last_sequence FROM ai_tasks_messages GROUP BY task_id"));
    if (!rows.hasValue()) {
        return utils::Result<QHash<QString, qint64>>::failure(rows.error());
    }

    QHash<QString, qint64> sequences;
    for (const auto& row : rows.value()) {
        qint64 sequence = 0;
        const QString taskId = row.value(QStringLiteral("task_id")).toString();
        if (taskId.isEmpty() || !persistence::readStoredInteger(row.value(QStringLiteral("last_sequence")), sequence) || sequence < 0) {
            return utils::Result<QHash<QString, qint64>>::failure({"ai_conversation_invalid", "A stored conversation sequence is invalid", taskId});
        }
        sequences.insert(taskId, sequence);
    }

    return utils::Result<QHash<QString, qint64>>::success(sequences);
}

QFuture<utils::Result<void>> AiTaskRepository::appendConversation(const QVector<ConversationMessage>& messages) {
    QVector<persistence::DatabaseStatement> statements;
    statements.reserve(messages.size());
    for (const auto& message : messages) {
        if (message.id.isEmpty() || message.taskId.isEmpty() || message.sequence < 0 || message.summarizedUntil < 0 || !persistence::validStoredTimestamp(message.createdAtUtc)) {
            return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_conversation_invalid", "The conversation message is invalid", message.id}));
        }
        const QString calls = QString::fromUtf8(QJsonDocument(message.toolCalls).toJson(QJsonDocument::Compact));
        statements.append({QStringLiteral("INSERT INTO ai_tasks_messages(id, task_id, sequence, role, content, tool_calls, tool_call_id, summarized_until, created_at_utc, image_data, image_media_type) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"), {message.id, message.taskId, message.sequence, conversationRoleName(message.role), persistence::storedText(message.content), persistence::storedText(calls), persistence::storedText(message.toolCallId), message.summarizedUntil, persistence::storedTimestamp(message.createdAtUtc), message.imageData, persistence::storedText(QString::fromUtf8(message.imageMediaType))}});
    }

    return statements.isEmpty() ? QtFuture::makeReadyValueFuture(utils::Result<void>::success()) : m_host.executeDatabaseTransaction(statements);
}

// Resetting a task returns it to the state it was created in, so the runs of a conversation that no longer exists go with it.
QFuture<utils::Result<void>> AiTaskRepository::clearConversation(const QString& taskId) {
    if (taskId.isEmpty()) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_conversation_invalid", "The conversation request is invalid", taskId}));
    }

    const QVector<persistence::DatabaseStatement> statements{{QStringLiteral("DELETE FROM ai_tasks_messages WHERE task_id = ?"), {taskId}}, {QStringLiteral("DELETE FROM ai_tasks_executions WHERE task_id = ?"), {taskId}}};
    return m_host.executeDatabaseTransaction(statements);
}

QFuture<utils::Result<QVector<ExecutionLogEntry>>> AiTaskRepository::executionLogs(const QString& executionId) {
    if (executionId.isEmpty()) {
        return QtFuture::makeReadyValueFuture(utils::Result<QVector<ExecutionLogEntry>>::failure({"ai_tasks_execution_invalid", "The AI execution identity is invalid", {}}));
    }

    auto future = m_host.queryDatabase(QStringLiteral("SELECT id, execution_id, sequence, timestamp_utc, level, kind, detail FROM ai_tasks_logs WHERE execution_id = ? ORDER BY sequence DESC LIMIT 100"), {executionId});
    // clang-format off
    return future.then([](utils::Result<persistence::DatabaseRows> result) { return result.hasValue() ? AiTaskRepositoryHelper::parseExecutionLogs(result.value()) : utils::Result<QVector<ExecutionLogEntry>>::failure(result.error()); });
    // clang-format on
}

QFuture<utils::Result<void>> AiTaskRepository::createWorkspace(const AiWorkspace& workspace) {
    if (workspace.id.isEmpty() || workspace.name.trimmed().isEmpty() || workspace.name != workspace.name.trimmed() || workspace.position < 0 || !persistence::validStoredTimestamp(workspace.createdAtUtc) || !persistence::validStoredTimestamp(workspace.updatedAtUtc) || workspace.updatedAtUtc < workspace.createdAtUtc) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_workspace_invalid", "The AI workspace is invalid", workspace.id}));
    }

    QVector<persistence::DatabaseStatement> statements;
    if (workspace.active) {
        statements.append({QStringLiteral("UPDATE ai_tasks_workspaces SET active = ?"), {0}});
    }
    statements.append({QStringLiteral("INSERT INTO ai_tasks_workspaces(id, name, position, active, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?)"), {workspace.id, workspace.name, workspace.position, workspace.active ? 1 : 0, persistence::storedTimestamp(workspace.createdAtUtc), persistence::storedTimestamp(workspace.updatedAtUtc)}});
    return m_host.executeDatabaseTransaction(statements);
}

QFuture<utils::Result<void>> AiTaskRepository::renameWorkspace(const QString& workspaceId, const QString& name, const QDateTime& updatedAtUtc) {
    if (workspaceId.isEmpty() || name.trimmed().isEmpty() || name != name.trimmed() || !persistence::validStoredTimestamp(updatedAtUtc)) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_workspace_invalid", "The AI workspace name is invalid", workspaceId}));
    }
    return m_host.executeDatabaseTransaction({{QStringLiteral("UPDATE ai_tasks_workspaces SET name = ?, updated_at_utc = ? WHERE id = ?"), {name, persistence::storedTimestamp(updatedAtUtc), workspaceId}}});
}

QFuture<utils::Result<void>> AiTaskRepository::removeWorkspace(const QString& workspaceId, const QVector<AiWorkspace>& remaining) {
    if (workspaceId.isEmpty()) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_workspace_invalid", "The AI workspace identifier is invalid", workspaceId}));
    }

    int activeWorkspaces = 0;
    for (int index = 0; index < remaining.size(); ++index) {
        const auto& workspace = remaining.at(index);
        if (workspace.id.isEmpty() || workspace.id == workspaceId || workspace.position != index) {
            return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_workspace_invalid", "The remaining AI workspace order is invalid", workspace.id}));
        }
        activeWorkspaces += workspace.active ? 1 : 0;
    }
    if (!remaining.isEmpty() && activeWorkspaces != 1) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_workspace_invalid", "Exactly one AI workspace must remain active", workspaceId}));
    }

    QVector<persistence::DatabaseStatement> statements{{QStringLiteral("DELETE FROM ai_tasks_workspaces WHERE id = ?"), {workspaceId}}};
    for (const auto& workspace : remaining) {
        statements.append({QStringLiteral("UPDATE ai_tasks_workspaces SET position = ?, active = ? WHERE id = ?"), {workspace.position, workspace.active ? 1 : 0, workspace.id}});
    }
    return m_host.executeDatabaseTransaction(statements);
}

QFuture<utils::Result<void>> AiTaskRepository::activateWorkspace(const QString& workspaceId) {
    if (workspaceId.isEmpty()) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_workspace_invalid", "The AI workspace identifier is invalid", workspaceId}));
    }
    return m_host.executeDatabaseTransaction({{QStringLiteral("UPDATE ai_tasks_workspaces SET active = ?"), {0}}, {QStringLiteral("UPDATE ai_tasks_workspaces SET active = ? WHERE id = ?"), {1, workspaceId}}});
}

// One contract decides what a task must carry, because a reader stricter than its writer refuses to load what it stored.
bool AiTaskRepository::validTask(const AiTask& task) {
    const QUrl issue(task.issueUrl);
    if (!task.issueUrl.isEmpty() && (!issue.isValid() || issue.host().isEmpty() || (issue.scheme() != QStringLiteral("http") && issue.scheme() != QStringLiteral("https")))) {
        return false;
    }
    if (task.id.isEmpty() || task.workspaceId.isEmpty() || task.title.trimmed().isEmpty() || task.title != task.title.trimmed()) {
        return false;
    }
    if (task.executionKind == TaskExecutionKind::Command) {
        return !task.command.trimmed().isEmpty() && QDir(task.workdir).isAbsolute() && task.agentId.isEmpty();
    }
    // An agent runs on a configured connection, and whether that connection still exists is answered when the run starts.
    return !task.prompt.trimmed().isEmpty() && !task.agentId.trimmed().isEmpty() && (task.workdir.isEmpty() || QDir(task.workdir).isAbsolute());
}

QFuture<utils::Result<void>> AiTaskRepository::saveTask(const AiTask& task) {
    if (!validTask(task) || task.commandTimeoutSeconds < 0 || task.position < 0 || !persistence::validStoredTimestamp(task.createdAtUtc) || !persistence::validStoredTimestamp(task.updatedAtUtc) || task.updatedAtUtc < task.createdAtUtc) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_task_invalid", "The AI task is invalid", task.id}));
    }
    if (task.schedule.has_value()) {
        const auto validation = validateSchedule(task.schedule.value());
        if (!validation.hasValue()) {
            return QtFuture::makeReadyValueFuture(validation);
        }
    }

    QVector<persistence::DatabaseStatement> statements{{QStringLiteral("DELETE FROM ai_tasks_tasks WHERE id = ?"), {task.id}}, {QStringLiteral("INSERT INTO ai_tasks_tasks(id, workspace_id, title, description, prompt, issue_url, agent_id, execution_kind, workdir, command, command_timeout_seconds, column_name, position, created_at_utc, updated_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"), {task.id, task.workspaceId, persistence::storedText(task.title), persistence::storedText(task.description), persistence::storedText(task.prompt), persistence::storedText(task.issueUrl), persistence::storedText(task.agentId), taskExecutionKindName(task.executionKind), persistence::storedText(task.workdir), persistence::storedText(task.command), task.commandTimeoutSeconds, columnName(task.column), task.position, persistence::storedTimestamp(task.createdAtUtc), persistence::storedTimestamp(task.updatedAtUtc)}}};
    statements.append(scheduleStatements(task));
    return m_host.executeDatabaseTransaction(statements);
}

QFuture<utils::Result<void>> AiTaskRepository::removeTask(const QString& taskId) {
    if (taskId.isEmpty()) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_task_invalid", "The AI task identifier is invalid", taskId}));
    }
    return m_host.executeDatabaseTransaction({{QStringLiteral("DELETE FROM ai_tasks_tasks WHERE id = ?"), {taskId}}});
}

QFuture<utils::Result<void>> AiTaskRepository::moveTask(const QString& taskId, TaskColumn column, int position, const QDateTime& updatedAtUtc) {
    if (taskId.isEmpty() || position < 0 || !persistence::validStoredTimestamp(updatedAtUtc)) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_task_invalid", "The AI task move is invalid", taskId}));
    }
    return m_host.executeDatabaseTransaction({{QStringLiteral("UPDATE ai_tasks_tasks SET column_name = ?, position = ?, updated_at_utc = ? WHERE id = ?"), {columnName(column), position, persistence::storedTimestamp(updatedAtUtc), taskId}}});
}

QFuture<utils::Result<void>> AiTaskRepository::enqueueTask(const QString& taskId, const QDateTime& queuedAtUtc, const std::optional<TaskSchedule>& updatedSchedule) {
    if (taskId.isEmpty() || !persistence::validStoredTimestamp(queuedAtUtc)) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_queue_invalid", "The AI task queue entry is invalid", taskId}));
    }
    if (updatedSchedule.has_value()) {
        const auto validation = validateSchedule(updatedSchedule.value());
        if (!validation.hasValue()) {
            return QtFuture::makeReadyValueFuture(validation);
        }
    }

    QVector<persistence::DatabaseStatement> statements{{QStringLiteral("INSERT INTO ai_tasks_queue(task_id, queued_at_utc) VALUES(?, ?)"), {taskId, persistence::storedTimestamp(queuedAtUtc)}}, {QStringLiteral("UPDATE ai_tasks_tasks SET column_name = ?, updated_at_utc = ? WHERE id = ?"), {columnName(TaskColumn::Doing), persistence::storedTimestamp(queuedAtUtc), taskId}}};
    if (updatedSchedule.has_value()) {
        const auto& schedule = updatedSchedule.value();
        statements.append({QStringLiteral("UPDATE ai_tasks_schedules SET enabled = ?, next_run_at_utc = ?, last_triggered_at_utc = ? WHERE task_id = ?"), {schedule.enabled ? 1 : 0, schedule.nextRunAtUtc.isValid() ? persistence::storedTimestamp(schedule.nextRunAtUtc) : QVariant{}, schedule.lastTriggeredAtUtc.isValid() ? persistence::storedTimestamp(schedule.lastTriggeredAtUtc) : QVariant{}, taskId}});
    }
    return m_host.executeDatabaseTransaction(statements);
}

QFuture<utils::Result<void>> AiTaskRepository::cancelTask(const QString& taskId, const QDateTime& cancelledAtUtc) {
    if (taskId.isEmpty() || !persistence::validStoredTimestamp(cancelledAtUtc)) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_queue_invalid", "The AI task cancellation is invalid", taskId}));
    }
    return m_host.executeDatabaseTransaction({{QStringLiteral("DELETE FROM ai_tasks_queue WHERE task_id = ?"), {taskId}}, {QStringLiteral("UPDATE ai_tasks_tasks SET column_name = ?, updated_at_utc = ? WHERE id = ?"), {columnName(TaskColumn::Todo), persistence::storedTimestamp(cancelledAtUtc), taskId}}});
}

QFuture<utils::Result<void>> AiTaskRepository::completeTask(const QString& taskId, TaskColumn column, const QDateTime& finishedAtUtc) {
    if (taskId.isEmpty() || !persistence::validStoredTimestamp(finishedAtUtc)) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_task_invalid", "The AI task completion is invalid", taskId}));
    }
    return m_host.executeDatabaseTransaction({{QStringLiteral("DELETE FROM ai_tasks_queue WHERE task_id = ?"), {taskId}}, {QStringLiteral("UPDATE ai_tasks_tasks SET column_name = ?, updated_at_utc = ? WHERE id = ?"), {columnName(column), persistence::storedTimestamp(finishedAtUtc), taskId}}});
}

QFuture<utils::Result<void>> AiTaskRepository::startExecution(const TaskExecution& execution) {
    if (execution.id.isEmpty() || execution.taskId.isEmpty() || !persistence::validStoredTimestamp(execution.startedAtUtc)) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_execution_invalid", "The AI execution is invalid", execution.id}));
    }
    return m_host.executeDatabase(QStringLiteral("INSERT INTO ai_tasks_executions(id, task_id, status, started_at_utc, finished_at_utc, input_tokens, output_tokens, finish_reason, error_message, content, stop_reason) VALUES(?, ?, ?, ?, NULL, 0, 0, '', '', '', 'answered')"), {execution.id, execution.taskId, executionStatusName(ExecutionStatus::Running), persistence::storedTimestamp(execution.startedAtUtc)});
}

QFuture<utils::Result<void>> AiTaskRepository::finishExecution(const TaskExecution& execution) {
    if (execution.id.isEmpty() || execution.status == ExecutionStatus::Running || !persistence::validStoredTimestamp(execution.startedAtUtc) || !persistence::validStoredTimestamp(execution.finishedAtUtc) || execution.finishedAtUtc < execution.startedAtUtc || execution.inputTokens < 0 || execution.outputTokens < 0) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_execution_invalid", "The AI execution is invalid", execution.id}));
    }
    return m_host.executeDatabase(QStringLiteral("UPDATE ai_tasks_executions SET status = ?, finished_at_utc = ?, input_tokens = ?, output_tokens = ?, finish_reason = ?, error_message = ?, content = ?, stop_reason = ? WHERE id = ?"), {executionStatusName(execution.status), persistence::storedTimestamp(execution.finishedAtUtc), execution.inputTokens, execution.outputTokens, persistence::storedText(execution.finishReason), persistence::storedText(execution.errorMessage), persistence::storedText(execution.content), agentStopReasonName(execution.stopReason), execution.id});
}

QFuture<utils::Result<void>> AiTaskRepository::appendExecutionLog(const ExecutionLogEntry& entry) {
    if (entry.id.isEmpty() || entry.executionId.isEmpty() || entry.sequence < 0 || !persistence::validStoredTimestamp(entry.timestampUtc)) {
        return QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"ai_tasks_execution_invalid", "The AI execution log entry is invalid", entry.id}));
    }
    return m_host.executeDatabase(QStringLiteral("INSERT INTO ai_tasks_logs(id, execution_id, sequence, timestamp_utc, level, kind, detail) VALUES(?, ?, ?, ?, ?, ?, ?)"), {entry.id, entry.executionId, entry.sequence, persistence::storedTimestamp(entry.timestampUtc), executionLogLevelName(entry.level), executionLogKindName(entry.kind), persistence::storedText(entry.detail)});
}

QString AiTaskRepository::taskExecutionKindName(TaskExecutionKind kind) {
    return kind == TaskExecutionKind::Command ? QStringLiteral("command") : QStringLiteral("agent");
}

utils::Result<TaskExecutionKind> AiTaskRepository::parseTaskExecutionKind(const QString& value) {
    if (value == QStringLiteral("agent")) {
        return utils::Result<TaskExecutionKind>::success(TaskExecutionKind::Agent);
    }
    if (value == QStringLiteral("command")) {
        return utils::Result<TaskExecutionKind>::success(TaskExecutionKind::Command);
    }
    return utils::Result<TaskExecutionKind>::failure({"ai_tasks_task_invalid", "A stored AI task execution kind is invalid", value});
}

QString AiTaskRepository::executionStatusName(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::Running:
        return QStringLiteral("running");
    case ExecutionStatus::Succeeded:
        return QStringLiteral("succeeded");
    case ExecutionStatus::Failed:
        return QStringLiteral("failed");
    case ExecutionStatus::Cancelled:
        return QStringLiteral("cancelled");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("running"));
}

QString AiTaskRepository::agentStopReasonName(AgentStopReason reason) {
    switch (reason) {
    case AgentStopReason::Answered:
        return QStringLiteral("answered");
    case AgentStopReason::IterationLimit:
        return QStringLiteral("iteration-limit");
    case AgentStopReason::OutputBudget:
        return QStringLiteral("output-budget");
    case AgentStopReason::ToolRepetition:
        return QStringLiteral("tool-repetition");
    case AgentStopReason::Cancelled:
        return QStringLiteral("cancelled");
    case AgentStopReason::Failed:
        return QStringLiteral("failed");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("answered"));
}

std::optional<AgentStopReason> AiTaskRepository::agentStopReasonFromName(const QString& name) {
    static const QHash<QString, AgentStopReason> reasons{{QStringLiteral("answered"), AgentStopReason::Answered}, {QStringLiteral("iteration-limit"), AgentStopReason::IterationLimit}, {QStringLiteral("output-budget"), AgentStopReason::OutputBudget}, {QStringLiteral("tool-repetition"), AgentStopReason::ToolRepetition}, {QStringLiteral("cancelled"), AgentStopReason::Cancelled}, {QStringLiteral("failed"), AgentStopReason::Failed}};
    const auto position = reasons.constFind(name);
    return position == reasons.constEnd() ? std::nullopt : std::optional<AgentStopReason>(position.value());
}

utils::Result<ExecutionStatus> AiTaskRepository::parseExecutionStatus(const QString& value) {
    if (value == QStringLiteral("running")) {
        return utils::Result<ExecutionStatus>::success(ExecutionStatus::Running);
    }
    if (value == QStringLiteral("succeeded")) {
        return utils::Result<ExecutionStatus>::success(ExecutionStatus::Succeeded);
    }
    if (value == QStringLiteral("failed")) {
        return utils::Result<ExecutionStatus>::success(ExecutionStatus::Failed);
    }
    if (value == QStringLiteral("cancelled")) {
        return utils::Result<ExecutionStatus>::success(ExecutionStatus::Cancelled);
    }
    return utils::Result<ExecutionStatus>::failure({"ai_tasks_execution_invalid", "A stored AI execution status is invalid", value});
}

QString AiTaskRepository::executionLogLevelName(ExecutionLogLevel level) {
    switch (level) {
    case ExecutionLogLevel::Debug:
        return QStringLiteral("debug");
    case ExecutionLogLevel::Info:
        return QStringLiteral("info");
    case ExecutionLogLevel::Warning:
        return QStringLiteral("warning");
    case ExecutionLogLevel::Error:
        return QStringLiteral("error");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("info"));
}

// The exchanged payloads are too large for a grid cell, so the kind decides which entries are opened on demand.
bool AiTaskRepository::carriesExchangedPayload(ExecutionLogKind kind) {
    switch (kind) {
    case ExecutionLogKind::RequestSent:
    case ExecutionLogKind::ResponseReceived:
    case ExecutionLogKind::ToolCalled:
    case ExecutionLogKind::ToolReturned:
    case ExecutionLogKind::Compacted:
        return true;
    case ExecutionLogKind::Started:
    case ExecutionLogKind::Iteration:
    case ExecutionLogKind::FirstTokenReceived:
    case ExecutionLogKind::UsageReported:
    case ExecutionLogKind::Throttled:
    case ExecutionLogKind::Succeeded:
    case ExecutionLogKind::Failed:
    case ExecutionLogKind::Cancelled:
        return false;
    }
    Q_UNREACHABLE_RETURN(false);
}

QString AiTaskRepository::executionLogKindName(ExecutionLogKind kind) {
    switch (kind) {
    case ExecutionLogKind::Started:
        return QStringLiteral("started");
    case ExecutionLogKind::Iteration:
        return QStringLiteral("iteration");
    case ExecutionLogKind::Compacted:
        return QStringLiteral("compacted");
    case ExecutionLogKind::RequestSent:
        return QStringLiteral("request-sent");
    case ExecutionLogKind::ToolCalled:
        return QStringLiteral("tool-called");
    case ExecutionLogKind::ToolReturned:
        return QStringLiteral("tool-returned");
    case ExecutionLogKind::FirstTokenReceived:
        return QStringLiteral("first-token");
    case ExecutionLogKind::ResponseReceived:
        return QStringLiteral("response-received");
    case ExecutionLogKind::UsageReported:
        return QStringLiteral("usage-reported");
    case ExecutionLogKind::Throttled:
        return QStringLiteral("throttled");
    case ExecutionLogKind::Succeeded:
        return QStringLiteral("succeeded");
    case ExecutionLogKind::Failed:
        return QStringLiteral("failed");
    case ExecutionLogKind::Cancelled:
        return QStringLiteral("cancelled");
    }
    Q_UNREACHABLE_RETURN(QStringLiteral("started"));
}

utils::Result<ExecutionLogKind> AiTaskRepository::parseExecutionLogKind(const QString& value) {
    static const QHash<QString, ExecutionLogKind> kinds{{QStringLiteral("started"), ExecutionLogKind::Started}, {QStringLiteral("iteration"), ExecutionLogKind::Iteration}, {QStringLiteral("compacted"), ExecutionLogKind::Compacted}, {QStringLiteral("request-sent"), ExecutionLogKind::RequestSent}, {QStringLiteral("tool-called"), ExecutionLogKind::ToolCalled}, {QStringLiteral("tool-returned"), ExecutionLogKind::ToolReturned}, {QStringLiteral("first-token"), ExecutionLogKind::FirstTokenReceived}, {QStringLiteral("response-received"), ExecutionLogKind::ResponseReceived}, {QStringLiteral("usage-reported"), ExecutionLogKind::UsageReported}, {QStringLiteral("throttled"), ExecutionLogKind::Throttled}, {QStringLiteral("succeeded"), ExecutionLogKind::Succeeded}, {QStringLiteral("failed"), ExecutionLogKind::Failed}, {QStringLiteral("cancelled"), ExecutionLogKind::Cancelled}};
    const auto found = kinds.constFind(value);
    return found == kinds.constEnd() ? utils::Result<ExecutionLogKind>::failure({"ai_tasks_execution_invalid", "A stored AI execution log kind is invalid", value}) : utils::Result<ExecutionLogKind>::success(found.value());
}

utils::Result<ExecutionLogLevel> AiTaskRepository::parseExecutionLogLevel(const QString& value) {
    if (value == QStringLiteral("debug")) {
        return utils::Result<ExecutionLogLevel>::success(ExecutionLogLevel::Debug);
    }
    if (value == QStringLiteral("info")) {
        return utils::Result<ExecutionLogLevel>::success(ExecutionLogLevel::Info);
    }
    if (value == QStringLiteral("warning")) {
        return utils::Result<ExecutionLogLevel>::success(ExecutionLogLevel::Warning);
    }
    if (value == QStringLiteral("error")) {
        return utils::Result<ExecutionLogLevel>::success(ExecutionLogLevel::Error);
    }
    return utils::Result<ExecutionLogLevel>::failure({"ai_tasks_execution_invalid", "A stored AI execution log level is invalid", value});
}

QVector<persistence::DatabaseStatement> AiTaskRepository::scheduleStatements(const AiTask& task) const {
    QVector<persistence::DatabaseStatement> statements{{QStringLiteral("DELETE FROM ai_tasks_schedules WHERE task_id = ?"), {task.id}}};
    if (!task.schedule.has_value()) {
        return statements;
    }

    const auto& schedule = task.schedule.value();
    statements.append({QStringLiteral("INSERT INTO ai_tasks_schedules(task_id, schedule_kind, enabled, once_at_utc, interval_seconds, cron_expression, time_zone_id, next_run_at_utc, last_triggered_at_utc) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)"), {task.id, scheduleKindName(schedule.kind), schedule.enabled ? 1 : 0, schedule.onceAtUtc.isValid() ? persistence::storedTimestamp(schedule.onceAtUtc) : QVariant{}, schedule.kind == ScheduleKind::Interval ? QVariant{schedule.intervalSeconds} : QVariant{}, schedule.cronExpression.isEmpty() ? QVariant{} : QVariant{schedule.cronExpression}, QString::fromUtf8(schedule.timeZoneId), schedule.nextRunAtUtc.isValid() ? persistence::storedTimestamp(schedule.nextRunAtUtc) : QVariant{}, schedule.lastTriggeredAtUtc.isValid() ? persistence::storedTimestamp(schedule.lastTriggeredAtUtc) : QVariant{}}});
    return statements;
}

const QVector<TaskColumn>& AiTaskRepository::columns() {
    static const QVector<TaskColumn> values{TaskColumn::Todo, TaskColumn::Doing, TaskColumn::Blocked, TaskColumn::Review, TaskColumn::Done};
    return values;
}

QString AiTaskRepository::columnName(TaskColumn column) {
    switch (column) {
    case TaskColumn::Todo:
        return QStringLiteral("todo");
    case TaskColumn::Doing:
        return QStringLiteral("doing");
    case TaskColumn::Blocked:
        return QStringLiteral("blocked");
    case TaskColumn::Review:
        return QStringLiteral("review");
    case TaskColumn::Done:
        return QStringLiteral("done");
    }
    Q_UNREACHABLE_RETURN({});
}

utils::Result<TaskColumn> AiTaskRepository::parseColumn(const QString& value) {
    for (const auto column : columns()) {
        if (columnName(column) == value) {
            return utils::Result<TaskColumn>::success(column);
        }
    }
    return utils::Result<TaskColumn>::failure({"ai_tasks_column_invalid", "The AI task column is invalid", value});
}

QString AiTaskRepository::scheduleKindName(ScheduleKind kind) {
    switch (kind) {
    case ScheduleKind::Once:
        return QStringLiteral("once");
    case ScheduleKind::Interval:
        return QStringLiteral("interval");
    case ScheduleKind::Cron:
        return QStringLiteral("cron");
    }
    Q_UNREACHABLE_RETURN({});
}

utils::Result<ScheduleKind> AiTaskRepository::parseScheduleKind(const QString& value) {
    if (value == QStringLiteral("once")) {
        return utils::Result<ScheduleKind>::success(ScheduleKind::Once);
    }
    if (value == QStringLiteral("interval")) {
        return utils::Result<ScheduleKind>::success(ScheduleKind::Interval);
    }
    if (value == QStringLiteral("cron")) {
        return utils::Result<ScheduleKind>::success(ScheduleKind::Cron);
    }
    return utils::Result<ScheduleKind>::failure({"ai_tasks_schedule_kind_invalid", "The AI task schedule kind is invalid", value});
}

utils::Result<void> AiTaskRepository::validateSchedule(const TaskSchedule& schedule) {
    const bool nextValid = schedule.nextRunAtUtc.isValid();
    if (schedule.timeZoneId.isEmpty() || !QTimeZone(schedule.timeZoneId).isValid() || (schedule.lastTriggeredAtUtc.isValid() && !persistence::validStoredTimestamp(schedule.lastTriggeredAtUtc)) || (nextValid && !persistence::validStoredTimestamp(schedule.nextRunAtUtc)) || (schedule.enabled != nextValid)) {
        return utils::Result<void>::failure({"ai_tasks_schedule_invalid", "The AI task schedule is invalid", {}});
    }
    if (schedule.kind == ScheduleKind::Once) {
        const bool valid = persistence::validStoredTimestamp(schedule.onceAtUtc) && schedule.intervalSeconds == 0 && schedule.cronExpression.isEmpty() && (!schedule.enabled || schedule.nextRunAtUtc == schedule.onceAtUtc);
        return valid ? utils::Result<void>::success() : utils::Result<void>::failure({"ai_tasks_schedule_invalid", "The one-time AI task schedule is invalid", {}});
    }
    if (schedule.kind == ScheduleKind::Interval) {
        const bool valid = !schedule.onceAtUtc.isValid() && schedule.intervalSeconds >= 60 && schedule.cronExpression.isEmpty();
        return valid ? utils::Result<void>::success() : utils::Result<void>::failure({"ai_tasks_schedule_invalid", "The interval AI task schedule is invalid", {}});
    }

    const auto cron = CronExpression::parse(schedule.cronExpression);
    const bool valid = !schedule.onceAtUtc.isValid() && schedule.intervalSeconds == 0 && cron.hasValue();
    return valid ? utils::Result<void>::success() : utils::Result<void>::failure(cron.hasValue() ? utils::Error{"ai_tasks_schedule_invalid", "The cron AI task schedule is invalid", {}} : cron.error());
}

} // namespace slotdeck::plugins::ai
