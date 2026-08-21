#pragma once

#include "AiCommandRunner.h"
#include "AiMcpClient.h"
#include "AiModelConnection.h"
#include "AiSkillCatalog.h"
#include "AiTaskRepository.h"
#include "AiToolContract.h"
#include "plugins/PluginInterface.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>

#include <memory>

#include <functional>

namespace slotdeck::plugins::ai {

using ToolCompletion = std::function<void(ToolResult)>;

enum class ToolAccessKind { None, Read, Write, Everything };

// A tool declares what it touches, so the agent runs two calls of one turn together only when they cannot reach the same file.
struct ToolAccess final {
    ToolAccessKind kind{ToolAccessKind::Everything};
    QStringList paths;
};

[[nodiscard]] bool toolAccessesConflict(const ToolAccess& first, const ToolAccess& second);

// Tools reach storage and the network, so every invocation completes through a callback instead of blocking the caller.
class AiToolRegistry final : public QObject {
    Q_OBJECT

  public:
    AiToolRegistry(PluginHost& host, QObject* parent = nullptr);

    void setMediaConfiguration(const ModelConnection& connection, const QString& address);
    void setSpeechConfiguration(const SpeechSettings& settings, const QString& address);
    void setSearchConfiguration(const SearchSettings& settings, const QString& address);
    void setTaskContext(const AiTask& task, const ModelConnection& connection);
    void setMcpClients(const QList<AiMcpClient*>& clients);

    [[nodiscard]] const QVector<ToolSchema>& schemas() const;
    void discoverSkills(const QString& sandboxRoot, const AiSkillCatalog::Completion& completion);
    void forgetSkills();
    [[nodiscard]] ToolPresentation presentation(const QString& toolName, const QJsonObject& arguments) const;
    [[nodiscard]] ToolAccess accessOf(const ToolCall& call, const QString& sandboxRoot) const;
    [[nodiscard]] int deadlineMsFor(const ToolCall& call) const;
    void invoke(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void cancel(const QString& callId);

  private:
    [[nodiscard]] utils::Result<QString> resolveSandboxPath(const QString& sandboxRoot, const QString& path) const;
    void readFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void writeFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void editFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void readImage(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void listDirectory(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void createDirectory(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void movePath(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void copyFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void removePath(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void describePath(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion) const;
    void searchFiles(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void runCommand(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void fetchUrl(const ToolCall& call, const ToolCompletion& completion);
    void searchWeb(const ToolCall& call, const ToolCompletion& completion);
    void generateImage(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void generateSpeech(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void listVoices(const ToolCall& call, const ToolCompletion& completion);
    void listSkills(const QString& sandboxRoot, const ToolCompletion& completion, const ToolCall& call);
    void searchSkills(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void readSkill(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void readSkillFile(const ToolCall& call, const QString& sandboxRoot, const ToolCompletion& completion);
    void describeTask(const ToolCall& call, const ToolCompletion& completion) const;
    void callMcpTool(const ToolCall& call, const ToolCompletion& completion);
    void listServerCatalog(const ToolCall& call, bool prompts, const ToolCompletion& completion);
    void readServerResource(const ToolCall& call, const ToolCompletion& completion);
    void readServerPrompt(const ToolCall& call, const ToolCompletion& completion);
    [[nodiscard]] AiMcpClient* readyServer(const QString& serverId) const;
    [[nodiscard]] utils::Result<void> writeGeneratedFile(const QString& path, const QByteArray& content) const;

    PluginHost& m_host;
    std::unique_ptr<AiSkillCatalog> m_skills;
    QNetworkAccessManager m_network;
    QVector<ToolSchema> m_nativeSchemas;
    QVector<ToolSchema> m_schemas;
    QHash<QString, QPair<QPointer<AiMcpClient>, QString>> m_mcpTools;
    QSet<QString> m_readOnlyMcpTools;
    struct RunningCommand final {
        QPointer<AiCommandRunner> runner;
        ToolCompletion completion;
    };

    QHash<QString, RunningCommand> m_runningCommands;
    QVector<QPointer<AiMcpClient>> m_mcpClients;
    ModelConnection m_media;
    QString m_mediaAddress;
    QString m_speechAddress;
    QString m_searchAddress;
    SpeechSettings m_speech;
    SearchSettings m_search;
    AiTask m_task;
    ModelConnection m_taskConnection;
};

} // namespace slotdeck::plugins::ai
