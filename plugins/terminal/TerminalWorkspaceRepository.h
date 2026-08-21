#pragma once

#include "domain/Workspace.h"
#include "plugins/PluginInterface.h"

namespace slotdeck::plugins::terminalplugin {

class TerminalWorkspaceRepository final {
  public:
    explicit TerminalWorkspaceRepository(PluginHost& host);

    [[nodiscard]] utils::Result<void> saveInitial(const domain::Workspace& workspace);
    [[nodiscard]] QFuture<utils::Result<void>> save(const domain::Workspace& workspace);
    [[nodiscard]] utils::Result<domain::Workspace> loadLastOpened() const;

  private:
    PluginHost& m_host;
};

} // namespace slotdeck::plugins::terminalplugin
