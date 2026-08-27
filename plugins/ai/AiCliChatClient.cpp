#include "AiCliChatClient.h"

#include "AiProviderCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QStandardPaths>

namespace slotdeck::plugins::ai {

class AiCliChatClientHelper final {
  public:
    static QString roleHeading(const QString& role);
};

QString AiCliChatClientHelper::roleHeading(const QString& role) {
    if (role == QStringLiteral("assistant")) {
        return QStringLiteral("Assistant");
    }
    if (role == QStringLiteral("system")) {
        return QStringLiteral("Instructions");
    }

    return QStringLiteral("User");
}

const QStringList& commandLineSearchDirectoryNames() {
    // clang-format off
    static const QStringList directories{QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin"), QStringLiteral("/opt/local/bin"), QStringLiteral("/snap/bin"), QStringLiteral(".local/bin"), QStringLiteral(".bun/bin"), QStringLiteral(".deno/bin"), QStringLiteral(".cargo/bin"), QStringLiteral(".npm-global/bin"), QStringLiteral("AppData/Local/Programs"), QStringLiteral("AppData/Roaming/npm")};
    // clang-format on
    return directories;
}

QStringList commandLineSearchDirectories() {
    QStringList resolved;

    for (const auto& directory : commandLineSearchDirectoryNames()) {
        const QString absolute = QDir::isAbsolutePath(directory) ? directory : QDir(QDir::homePath()).filePath(directory);

        if (QFileInfo(absolute).isDir()) {
            resolved.append(absolute);
        }
    }

    return resolved;
}

QString resolveCommandLineProgram(const QString& program) {
    const QString onPath = QStandardPaths::findExecutable(program);

    return onPath.isEmpty() ? QStandardPaths::findExecutable(program, commandLineSearchDirectories()) : onPath;
}

QStringList commandLineArguments(const CommandLineDescriptor& descriptor, const QString& prompt, const QString& workdir) {
    QStringList arguments;
    arguments.reserve(descriptor.arguments.size());

    for (const auto& argument : descriptor.arguments) {
        if (argument == QString::fromLatin1(commandLinePromptPlaceholder)) {
            arguments.append(prompt);
            continue;
        }

        if (argument == QString::fromLatin1(commandLineWorkdirPlaceholder)) {
            arguments.append(workdir);
            continue;
        }

        arguments.append(argument);
    }

    return arguments;
}

QString renderConversationPrompt(const QJsonArray& messages) {
    QStringList rendered;

    for (const auto& value : messages) {
        const QJsonObject message = value.toObject();
        const QString content = message.value(QStringLiteral("content")).toString();

        if (content.isEmpty()) {
            continue;
        }

        rendered.append(QStringLiteral("## %1\n\n%2").arg(AiCliChatClientHelper::roleHeading(message.value(QStringLiteral("role")).toString()), content));
    }

    return rendered.join(QStringLiteral("\n\n"));
}

AiCliChatClient::AiCliChatClient(QObject* parent) : AiChatClient(parent) {
    // clang-format off
    connect(&m_runner, &AiCommandRunner::outputReceived, this, [this](const QString& text) { emit contentReceived(text); });
    connect(&m_runner, &AiCommandRunner::finished, this, [this](int exitCode, const QString& output) { completeRun(exitCode, output); });
    connect(&m_runner, &AiCommandRunner::failed, this, [this](const utils::Error& error) { m_running = false; emit failed(error); });
    // clang-format on
}

void AiCliChatClient::send(const ChatRequest& request, const std::function<QString(const QString&)>& translate) {
    const ProviderDescriptor* provider = findProvider(request.connection.providerId);

    if (provider == nullptr || provider->protocol != WireProtocol::CommandLine) {
        emit failed({"ai_cli_provider_invalid", translate(QStringLiteral("ai.error.cli-provider-invalid")), request.connection.providerId});
        return;
    }

    if (request.workdir.isEmpty()) {
        emit failed({"ai_cli_workdir_required", translate(QStringLiteral("ai.error.cli-workdir-required")), provider->id});
        return;
    }

    const QString program = resolveCommandLineProgram(provider->commandLine.program);

    if (program.isEmpty()) {
        emit failed({"ai_cli_program_missing", translate(QStringLiteral("ai.error.cli-program-missing")), provider->commandLine.program});
        return;
    }

    const QString prompt = renderConversationPrompt(request.messages);
    const QStringList arguments = commandLineArguments(provider->commandLine, prompt, request.workdir);
    m_running = true;
    emit started();
    emit requestSent(program, arguments.join(QLatin1Char('\n')));
    m_runner.startProgram(program, arguments, request.workdir, 0);
}

// A command line agent runs its own tools, so its run is one turn that ends when the program does.
void AiCliChatClient::completeRun(int exitCode, const QString& output) {
    m_running = false;

    if (exitCode != 0) {
        emit failed({"ai_cli_failed", output.trimmed(), QString::number(exitCode)});
        return;
    }

    emit finished(output.trimmed(), {}, {}, QStringLiteral("stop"));
}

void AiCliChatClient::cancel() {
    m_running = false;
    m_runner.cancel();
}

bool AiCliChatClient::running() const {
    return m_running;
}

} // namespace slotdeck::plugins::ai
