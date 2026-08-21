#pragma once

#include "terminal/ShellProfile.h"
#include "utils/Result.h"

#include <QProcessEnvironment>
#include <QString>

namespace slotdeck::terminalcore {

class PosixShellIntegration final {
  public:
    [[nodiscard]] static utils::Result<void> configure(const ShellProfile& profile, const QString& historyFile, QProcessEnvironment& environment);
};

} // namespace slotdeck::terminalcore
