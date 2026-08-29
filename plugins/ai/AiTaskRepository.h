#pragma once

#include "AiMcpClient.h"
#include "AiModelConnection.h"
#include "plugins/PluginInterface.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>

#include <optional>

namespace slotdeck::plugins::ai {

enum class TaskColumn { Todo, Doing, Blocked, Review, Done };
enum class ScheduleKind { Once, Interval, Cron };
enum class ExecutionStatus { Running, Succeeded, Failed, Cancelled };

// A run that ends without answering did not necessarily fail, so what stopped it is named beside its status.
enum class AgentStopReason { Answered, IterationLimit, OutputBudget, ToolRepetition, Cancelled, Failed };
enum class ExecutionLogLevel { Debug, Info, Warning, Error };

// The kind names the event in the user language while the detail keeps the exchanged payload verbatim.
enum class ExecutionLogKind { Started, Iteration, Compacted, RequestSent, FirstTokenReceived, ResponseReceived, UsageReported, ToolCalled, ToolReturned, Throttled, Succeeded, Failed, Cancelled };

enum class ExecutionPhase { Idle, Queued, Throttled, Sending, Streaming, CallingTool, Compacting, Running };

enum class TaskExecutionKind { Agent, Command };

enum class SpeechProvider { ElevenLabs, OpenAi };

struct SpeechSettings final {
    SpeechProvider provider{SpeechProvider::ElevenLabs};
    QString voiceId;
    QString apiKey;
};

[[nodiscard]] QString speechProviderIdentifier(SpeechProvider provider);
[[nodiscard]] std::optional<SpeechProvider> speechProviderFromIdentifier(const QString& identifier);
[[nodiscard]] QString speechProviderAddress(SpeechProvider provider);
[[nodiscard]] QString speechProviderKeyVariable(SpeechProvider provider);
[[nodiscard]] QString speechProviderDefaultVoice(SpeechProvider provider);
[[nodiscard]] QStringList speechProviderDeclaredVoices(SpeechProvider provider);
[[nodiscard]] SpeechSettings declaredSpeechSettings(SpeechProvider provider);

enum class SearchProvider { Brave, Tavily, SearxNg };

struct SearchSettings final {
    SearchProvider provider{SearchProvider::Brave};
    QString instanceUrl;
    QString apiKey;
};

[[nodiscard]] QString searchProviderIdentifier(SearchProvider provider);
[[nodiscard]] std::optional<SearchProvider> searchProviderFromIdentifier(const QString& identifier);
[[nodiscard]] QString searchAddress(const SearchSettings& settings);
[[nodiscard]] QString searchProviderKeyVariable(SearchProvider provider);
[[nodiscard]] SearchSettings declaredSearchSettings(SearchProvider provider);

constexpr int defaultChatFontSize = 10;

struct ExecutionSettings final {
    int maximumIterations{8};
    int commandTimeoutSeconds{600};
    int parallelExecutions{1};
    int chatFontSize{defaultChatFontSize};
};

// An agent is the specialist a task is handed to, carrying its own instructions and the connection it speaks through.
struct AiAgent final {
    QString id;
    QString name;
    QString description;
    QString systemPrompt;
    QString connectionKey;
    int maximumIterations{8};

    [[nodiscard]] bool operator==(const AiAgent& other) const = default;
};

// A service decides how often it answers, so the limit belongs to the provider and is shared by every connection and every workspace that reaches it.
struct ProviderRateLimit final {
    QString providerId;
    int minimumIntervalMs{0};
    int maximumRequestsPerMinute{0};
    int maximumConcurrentRequests{0};

    [[nodiscard]] bool operator==(const ProviderRateLimit& other) const = default;
};

[[nodiscard]] const AiAgent* findAgent(const QVector<AiAgent>& agents, const QString& agentId);
[[nodiscard]] utils::Result<AiAgent> validateAgent(const AiAgent& agent);
[[nodiscard]] utils::Result<void> validateAgentSet(const QVector<AiAgent>& agents, const QVector<ModelConnection>& connections);

struct AiSettings final {
    QVector<ModelConnection> connections;
    QString defaultConnectionKey;
    ExecutionSettings execution;
    SearchSettings search;
    SpeechSettings speech;
    QVector<McpServerDescriptor> mcpServers;
    QVector<ProviderRateLimit> rateLimits;
    QVector<AiAgent> agents;
};

struct ExecutionLogEntry final {
    QString id;
    QString executionId;
    qint64 sequence{0};
    QDateTime timestampUtc;
    ExecutionLogLevel level{ExecutionLogLevel::Info};
    ExecutionLogKind kind{ExecutionLogKind::Started};
    QString detail;
};

struct TaskExecution final {
    QString id;
    QString taskId;
    ExecutionStatus status{ExecutionStatus::Running};
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
    qint64 inputTokens{0};
    qint64 outputTokens{0};
    QString finishReason;
    QString errorMessage;
    QString content;
    AgentStopReason stopReason{AgentStopReason::Answered};
    // The run is priced by what it really spoke to, and a run recorded before the product knew that carries neither.
    QString providerId{};
    QString modelId{};
};

struct TaskSchedule final {
    ScheduleKind kind{ScheduleKind::Once};
    bool enabled{true};
    QDateTime onceAtUtc;
    qint64 intervalSeconds{0};
    QString cronExpression;
    QByteArray timeZoneId;
    QDateTime nextRunAtUtc;
    QDateTime lastTriggeredAtUtc;
};

struct AiWorkspace final {
    QString id;
    QString name;
    int position{0};
    bool active{false};
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
};

struct AiTask final {
    QString id;
    QString workspaceId;
    QString title;
    QString description;
    QString prompt;
    QString issueUrl;
    // The agent this task is handed to, which is empty for a command because a command never reaches a model.
    QString agentId;
    TaskExecutionKind executionKind{TaskExecutionKind::Agent};
    QString workdir;
    QString command;
    int commandTimeoutSeconds{600};
    TaskColumn column{TaskColumn::Todo};
    int position{0};
    QDateTime createdAtUtc;
    QDateTime updatedAtUtc;
    std::optional<TaskSchedule> schedule;
};

// The conversation of a task is the durable dialogue it holds with its agent, and a run is one turn inside it.
enum class ConversationRole { User, Assistant, Tool };

struct ConversationMessage final {
    QString id;
    QString taskId;
    qint64 sequence{0};
    ConversationRole role{ConversationRole::User};
    QString content;
    QJsonArray toolCalls;
    QString toolCallId;
    // The bytes a tool returned belong to the result it answered, so the model still sees the picture on every later run.
    QByteArray imageData;
    QByteArray imageMediaType;
    // A summary turn replaces everything up to this sequence when the conversation is projected, so it is summarized once.
    qint64 summarizedUntil{0};
    QDateTime createdAtUtc;

    [[nodiscard]] bool operator==(const ConversationMessage& other) const = default;
};

struct TaskOutcome final {
    ExecutionStatus status{ExecutionStatus::Succeeded};
    QString errorMessage;
    AgentStopReason stopReason{AgentStopReason::Answered};
};

class AiTaskRepository final {
  public:
    explicit AiTaskRepository(PluginHost& host);

    [[nodiscard]] utils::Result<void> initialize();
    [[nodiscard]] utils::Result<QVector<AiWorkspace>> workspaces() const;
    [[nodiscard]] utils::Result<QVector<AiTask>> tasks() const;
    [[nodiscard]] utils::Result<QStringList> queuedTaskIds() const;
    [[nodiscard]] utils::Result<QHash<QString, TaskOutcome>> lastOutcomes() const;
    [[nodiscard]] utils::Result<QHash<QString, qint64>> conversationSequences() const;
    [[nodiscard]] QFuture<utils::Result<QVector<ConversationMessage>>> conversation(const QString& taskId, qint64 beforeSequence, int maximumMessages);
    [[nodiscard]] QFuture<utils::Result<void>> appendConversation(const QVector<ConversationMessage>& messages);
    [[nodiscard]] QFuture<utils::Result<void>> clearConversation(const QString& taskId);
    [[nodiscard]] AiSettings settings() const;
    [[nodiscard]] QFuture<utils::Result<void>> createWorkspace(const AiWorkspace& workspace);
    [[nodiscard]] QFuture<utils::Result<void>> renameWorkspace(const QString& workspaceId, const QString& name, const QDateTime& updatedAtUtc);
    [[nodiscard]] QFuture<utils::Result<void>> removeWorkspace(const QString& workspaceId, const QVector<AiWorkspace>& remaining);
    [[nodiscard]] QFuture<utils::Result<void>> activateWorkspace(const QString& workspaceId);
    [[nodiscard]] QFuture<utils::Result<void>> saveTask(const AiTask& task);
    [[nodiscard]] QFuture<utils::Result<void>> removeTask(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<void>> moveTask(const QString& taskId, TaskColumn column, int position, const QDateTime& updatedAtUtc);
    [[nodiscard]] QFuture<utils::Result<void>> enqueueTask(const QString& taskId, const QDateTime& queuedAtUtc, const std::optional<TaskSchedule>& updatedSchedule = std::nullopt);
    [[nodiscard]] QFuture<utils::Result<void>> cancelTask(const QString& taskId, const QDateTime& cancelledAtUtc);
    [[nodiscard]] QFuture<utils::Result<void>> completeTask(const QString& taskId, TaskColumn column, const QDateTime& finishedAtUtc);
    [[nodiscard]] QFuture<utils::Result<QVector<TaskExecution>>> executions(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<QVector<ExecutionLogEntry>>> executionLogs(const QString& executionId);
    [[nodiscard]] QFuture<utils::Result<void>> saveSettings(const AiSettings& settings);
    [[nodiscard]] QFuture<utils::Result<void>> startExecution(const TaskExecution& execution);
    [[nodiscard]] QFuture<utils::Result<void>> finishExecution(const TaskExecution& execution);
    [[nodiscard]] QFuture<utils::Result<void>> appendExecutionLog(const ExecutionLogEntry& entry);

    [[nodiscard]] static bool validTask(const AiTask& task);
    [[nodiscard]] static QString taskExecutionKindName(TaskExecutionKind kind);
    [[nodiscard]] static utils::Result<TaskExecutionKind> parseTaskExecutionKind(const QString& value);
    [[nodiscard]] static QString executionStatusName(ExecutionStatus status);
    [[nodiscard]] static QString agentStopReasonName(AgentStopReason reason);
    [[nodiscard]] static std::optional<AgentStopReason> agentStopReasonFromName(const QString& name);
    [[nodiscard]] static utils::Result<ExecutionStatus> parseExecutionStatus(const QString& value);
    [[nodiscard]] static QString conversationRoleName(ConversationRole role);
    [[nodiscard]] static utils::Result<ConversationRole> parseConversationRole(const QString& value);
    [[nodiscard]] static QString executionLogLevelName(ExecutionLogLevel level);
    [[nodiscard]] static utils::Result<ExecutionLogLevel> parseExecutionLogLevel(const QString& value);
    [[nodiscard]] static QString executionLogKindName(ExecutionLogKind kind);
    [[nodiscard]] static bool carriesExchangedPayload(ExecutionLogKind kind);
    [[nodiscard]] static utils::Result<ExecutionLogKind> parseExecutionLogKind(const QString& value);

    [[nodiscard]] static QString columnName(TaskColumn column);
    [[nodiscard]] static utils::Result<TaskColumn> parseColumn(const QString& value);
    [[nodiscard]] static QString scheduleKindName(ScheduleKind kind);
    [[nodiscard]] static utils::Result<ScheduleKind> parseScheduleKind(const QString& value);
    [[nodiscard]] static utils::Result<void> validateSchedule(const TaskSchedule& schedule);
    [[nodiscard]] static const QVector<TaskColumn>& columns();

  private:
    [[nodiscard]] QVector<persistence::DatabaseStatement> scheduleStatements(const AiTask& task) const;

    PluginHost& m_host;
};

} // namespace slotdeck::plugins::ai
