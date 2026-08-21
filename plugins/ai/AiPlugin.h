#pragma once

#include "AiChatClient.h"
#include "AiCommandRunner.h"
#include "AiMcpClient.h"
#include "AiProviderScope.h"
#include "AiRequestGate.h"
#include "AiTaskRepository.h"
#include "AiToolRegistry.h"
#include "plugins/PluginInterface.h"

#include <QChronoTimer>
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace slotdeck::plugins::ai {

enum class TaskRunState { Idle, Waiting, Running };

using ChatClientFactory = std::function<std::unique_ptr<AiChatClient>(AiRequestGate&)>;

class AiPlugin final : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SlotDeckPluginInterface_iid)
    Q_INTERFACES(slotdeck::plugins::PluginInterface)

  public:
    AiPlugin();
    explicit AiPlugin(ChatClientFactory clientFactory);
    ~AiPlugin() override;

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString titleKey() const override;
    [[nodiscard]] QStringList dependencies() const override;
    [[nodiscard]] int databaseSchemaVersion() const override;
    [[nodiscard]] TranslationCatalog translations() const override;
    [[nodiscard]] QString styleSheet(const ui::Theme& theme) const override;
    [[nodiscard]] QVector<NavigationItem> navigationItems(const ui::Theme& theme) const override;
    [[nodiscard]] QVector<SettingsGroup> settingsGroups() const override;
    [[nodiscard]] utils::Result<void> initialize(PluginHost& host) override;
    [[nodiscard]] QWidget* createNavigationView(const QString& itemId, QWidget* parent) override;
    [[nodiscard]] QWidget* createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) override;
    [[nodiscard]] QWidget* createProviderSelectionSection(QWidget* parent);
    [[nodiscard]] QWidget* createExecutionSection(QWidget* parent);
    void handleRequest(const QString& senderPluginId, const QString& topic, const QJsonObject& payload, PluginReply reply) override;
    void handleEvent(const QString& senderPluginId, const QString& topic, const QJsonObject& payload) override;
    void shutdown() override;

    [[nodiscard]] PluginHost& host() const;
    [[nodiscard]] const QVector<AiWorkspace>& workspaces() const;
    [[nodiscard]] const QVector<AiTask>& tasks() const;
    [[nodiscard]] TaskRunState runState(const QString& taskId) const;
    [[nodiscard]] ExecutionStatus lastExecutionStatus(const QString& taskId) const;
    [[nodiscard]] AgentStopReason lastStopReason(const QString& taskId) const;
    [[nodiscard]] bool hasLastExecution(const QString& taskId) const;
    [[nodiscard]] QString lastError(const QString& taskId) const;
    [[nodiscard]] ExecutionPhase executionPhase(const QString& taskId) const;
    [[nodiscard]] static QString phaseName(ExecutionPhase phase);
    [[nodiscard]] QString executionDetail(const QString& taskId) const;
    [[nodiscard]] const QVector<ModelConnection>& connections() const;
    [[nodiscard]] const QString& defaultConnectionKey() const;
    [[nodiscard]] std::optional<ModelConnection> defaultConnection() const;
    [[nodiscard]] utils::Result<AiAgent> agentForTask(const AiTask& task) const;
    [[nodiscard]] utils::Result<ModelConnection> connectionForAgent(const AiAgent& agent) const;
    [[nodiscard]] int parallelExecutions() const;
    [[nodiscard]] const ExecutionSettings& executionSettings() const;
    [[nodiscard]] QFuture<utils::Result<void>> saveExecutionSettings(const ExecutionSettings& settings);
    [[nodiscard]] SpeechSettings effectiveSpeechSettings() const;
    [[nodiscard]] const QVector<McpServerDescriptor>& mcpServers() const;
    [[nodiscard]] int mcpToolCount(const QString& serverId) const;
    [[nodiscard]] ToolPresentation toolPresentation(const QString& toolName, const QJsonObject& arguments) const;
    [[nodiscard]] QFuture<utils::Result<void>> saveMcpServers(const QVector<McpServerDescriptor>& servers);
    [[nodiscard]] QFuture<utils::Result<void>> saveSpeechSettings(const SpeechSettings& settings);
    [[nodiscard]] QFuture<utils::Result<void>> saveSearchSettings(const SearchSettings& settings);
    [[nodiscard]] SearchSettings effectiveSearchSettings() const;
    [[nodiscard]] QFuture<utils::Result<QVector<TaskExecution>>> executions(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<QVector<ExecutionLogEntry>>> executionLogs(const QString& executionId);

    [[nodiscard]] QFuture<utils::Result<void>> saveConnections(const QVector<ModelConnection>& connections, const QString& defaultConnectionKey);
    [[nodiscard]] QFuture<utils::Result<void>> replaceConnection(const QString& previousKey, const ModelConnection& connection);
    [[nodiscard]] QFuture<utils::Result<void>> saveRateLimits(const QVector<ProviderRateLimit>& limits);
    [[nodiscard]] QVector<ProviderRateLimit> rateLimits() const;
    [[nodiscard]] QVector<AiAgent> agents() const;
    [[nodiscard]] QVector<ConversationMessage> conversation(const QString& taskId) const;
    [[nodiscard]] QFuture<utils::Result<void>> loadConversation(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<bool>> loadOlderConversation(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<void>> sendMessage(const QString& taskId, const QString& text);
    [[nodiscard]] QFuture<utils::Result<void>> resetConversation(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<void>> saveAgents(const QVector<AiAgent>& agents);
    [[nodiscard]] QFuture<utils::Result<QString>> createWorkspace(const QString& name);
    [[nodiscard]] QFuture<utils::Result<void>> renameWorkspace(const QString& workspaceId, const QString& name);
    [[nodiscard]] QFuture<utils::Result<void>> removeWorkspace(const QString& workspaceId);
    [[nodiscard]] QFuture<utils::Result<void>> activateWorkspace(const QString& workspaceId);
    [[nodiscard]] QFuture<utils::Result<void>> saveTask(AiTask task);
    [[nodiscard]] QFuture<utils::Result<void>> removeTask(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<void>> moveTask(const QString& taskId, TaskColumn column);
    [[nodiscard]] QFuture<utils::Result<void>> startTask(const QString& taskId);
    [[nodiscard]] QFuture<utils::Result<void>> stopTask(const QString& taskId);

  signals:
    void workspacesChanged();
    void tasksChanged();
    void taskRunStateChanged(const QString& taskId);
    void conversationChanged(const QString& taskId);
    void conversationStreamed(const QString& taskId, const QString& text);
    void executionActivity(const QString& taskId);
    void executionSettingsChanged();

  private:
    struct PendingToolCall final {
        ToolCall call;
        ToolAccess access;
        bool started{false};
        bool finished{false};
        ToolResult result;
        QTimer* deadline{nullptr};
    };

    struct ActiveExecution final {
        AiChatClient* client{nullptr};
        AiChatClient* summaryClient{nullptr};
        AiCommandRunner* runner{nullptr};
        TaskExecution record;
        QJsonObject instructions;
        int iteration{0};
        int maximumIterations{0};
        QHash<QString, int> toolCallSignatures;
        QHash<QString, QString> lastToolFailures;
        QVector<PendingToolCall> toolCalls;
        QString streamed;
        qint64 deliveredSequence{0};
        qint64 summarizedUntil{0};
        QString sandboxRoot;
        ModelConnection connection;
        // A picture a tool read belongs to the run that read it, so it travels with the run rather than with the stored text.
        QHash<QString, ToolResult> seenImages;
    };

    [[nodiscard]] const AiTask* task(const QString& taskId) const;
    [[nodiscard]] bool hasCapacity() const;
    [[nodiscard]] utils::Result<void> reloadState();
    void dispatchQueue();
    void startExecution(const AiTask& task);
    void startAgentExecution(const AiTask& task, const QString& executionId);
    void continueAgent(const QString& taskId);
    void summarizeDroppedTurns(const QString& taskId, const FittedConversation& fitted);
    void stopOrphanedTasks(const QStringList& taskIds);
    [[nodiscard]] ConversationMessage buildMessage(const QString& taskId, ConversationRole role, const QString& content, const QJsonArray& toolCalls, const QString& toolCallId);
    [[nodiscard]] QFuture<utils::Result<void>> recordConversation(const QString& taskId, const QVector<ConversationMessage>& messages);
    [[nodiscard]] QFuture<utils::Result<void>> enqueueRun(const QString& taskId);
    void continueWhenAnswerIsPending(const QString& taskId, qint64 deliveredSequence);
    void reportSending(const QString& taskId);
    void reportThrottle(const QString& taskId, const QString& executionId, ThrottleReason reason, qint64 milliseconds);
    void releaseSummaryClient(const QString& taskId, AiChatClient* client);
    void applySummary(const QString& taskId, const QString& executionId, const FittedConversation& fitted, const QString& summary);
    void handleToolCalls(const QString& taskId, const QString& content, const QVector<ToolCall>& calls);
    void dispatchPendingTools(const QString& taskId, const QString& executionId);
    void completeToolCall(const QString& taskId, const QString& executionId, const QString& name, ToolResult result);
    void refreshToolConfiguration();
    void restartMcpClients();
    void runSampling(const QJsonObject& parameters, int maximumTokens, McpReply reply);
    [[nodiscard]] QString environmentSection() const;
    [[nodiscard]] QString systemPromptData(const AiTask& task, const QVector<SkillDescriptor>& skills) const;
    [[nodiscard]] QString skillCatalog(const QVector<SkillDescriptor>& skills) const;
    [[nodiscard]] QString serverCatalog() const;
    [[nodiscard]] QString renderedSystemPrompt(const AiTask& task, const AiAgent& agent, const QVector<SkillDescriptor>& skills) const;
    [[nodiscard]] QJsonObject instructionMessage(const AiTask& task, const AiAgent& agent, const ModelConnection& connection, const QVector<SkillDescriptor>& skills) const;
    [[nodiscard]] static QJsonArray shapeForProtocol(const ModelConnection& connection, const QJsonArray& messages);
    [[nodiscard]] QJsonArray projectConversation(const QJsonObject& instructions, const ModelConnection& connection, const QVector<ConversationMessage>& conversation, const QHash<QString, ToolResult>& images, QVector<qint64>* sequences = nullptr) const;
    [[nodiscard]] QStringList readContextFiles(const QString& workdir) const;
    [[nodiscard]] const ModelDescriptor* findModelDescriptor(const ModelConnection& connection) const;
    [[nodiscard]] qint64 reservedContextTokens(const ModelConnection& connection) const;
    void startCommandExecution(const AiTask& task, const QString& executionId);
    void completeExecution(const QString& taskId, ExecutionStatus status, const QString& errorMessage, AgentStopReason stopReason);
    [[nodiscard]] QString taskOfExecution(const QString& executionId) const;
    void appendLog(const QString& executionId, ExecutionLogLevel level, ExecutionLogKind kind, const QString& detail = {});
    void forgetTask(const QString& taskId);
    void reportFailure(const utils::Error& error, const QString& message);
    void processSchedules();
    void armScheduleTimer();

    PluginHost* m_host{nullptr};
    std::unique_ptr<QObject> m_asyncContext;
    std::unique_ptr<AiTaskRepository> m_repository;
    std::unique_ptr<AiToolRegistry> m_tools;
    QVector<AiWorkspace> m_workspaces;
    QVector<AiTask> m_tasks;
    QStringList m_queue;
    QSet<QString> m_cancelledTaskIds;
    QHash<QString, QString> m_lastErrors;
    QHash<QString, ExecutionStatus> m_lastStatuses;
    QHash<QString, AgentStopReason> m_lastStopReasons;
    QHash<QString, ExecutionPhase> m_phases;
    QHash<QString, ExecutionPhase> m_phasesBeforeThrottle;
    QHash<QString, QVector<ConversationMessage>> m_conversations;
    QHash<QString, qint64> m_conversationSequences;
    QSet<QString> m_loadedConversations;
    QHash<QString, qint64> m_logSequences;
    QHash<QString, std::shared_ptr<ActiveExecution>> m_active;
    [[nodiscard]] QFuture<utils::Result<void>> persistSettings(AiSettings next);

    AiSettings m_settings;
    AiProviderScope m_providerScope;
    AiSettings m_committedSettings;
    quint64 m_settingsRevision{0};
    QHash<QString, AiMcpClient*> m_mcpClients;
    QChronoTimer m_scheduleTimer;
    ChatClientFactory m_clientFactory;
    AiRequestGate m_gate;
};

} // namespace slotdeck::plugins::ai
