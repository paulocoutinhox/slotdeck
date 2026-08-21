#pragma once

#include "CodeEditorState.h"
#include "plugins/PluginInterface.h"

namespace slotdeck::plugins::codeeditor {

class CodeEditorRepository final {
  public:
    explicit CodeEditorRepository(PluginHost& host);

    [[nodiscard]] utils::Result<void> initialize();
    [[nodiscard]] utils::Result<QVector<CodeWorkspaceState>> load() const;
    [[nodiscard]] CodeEditorSettings loadSettings() const;
    [[nodiscard]] QFuture<utils::Result<void>> save(const QVector<CodeWorkspaceState>& workspaces);
    [[nodiscard]] QFuture<utils::Result<void>> saveSettings(const CodeEditorSettings& settings);

  private:
    PluginHost& m_host;
};

} // namespace slotdeck::plugins::codeeditor
