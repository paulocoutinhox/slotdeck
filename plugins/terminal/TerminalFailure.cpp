#include "TerminalFailure.h"

#include <QHash>

namespace slotdeck::plugins::terminalplugin {

QString terminalFailureMessage(const utils::Error& error, PluginHost& host) {
    static const QHash<QString, QString> keys{
        {QStringLiteral("terminal_input_queue_full"), QStringLiteral("terminal.error.input-queue-full")}, {QStringLiteral("terminal_not_running"), QStringLiteral("terminal.error.not-running")}, {QStringLiteral("shell_not_executable"), QStringLiteral("terminal.error.shell-not-executable")}, {QStringLiteral("terminal_working_directory_missing"), QStringLiteral("terminal.error.workdir-missing")}, {QStringLiteral("terminal_spawn_failed"), QStringLiteral("terminal.error.spawn-failed")}, {QStringLiteral("terminal_backend_failed"), QStringLiteral("terminal.error.backend-failed")}, {QStringLiteral("terminal_workspace_invalid"), QStringLiteral("terminal.error.workspace-invalid")},
    };
    const QString key = keys.value(error.code);

    // A fault of the emulator is written for the log, because nothing the reader can do answers it.
    return key.isEmpty() ? error.message : host.translate(key);
}

} // namespace slotdeck::plugins::terminalplugin
