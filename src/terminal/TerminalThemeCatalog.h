#pragma once

#include "domain/TerminalTheme.h"

#include <QString>
#include <QVector>

namespace slotdeck::terminalcore {

[[nodiscard]] const QVector<domain::TerminalTheme>& terminalThemes();
[[nodiscard]] const domain::TerminalTheme& terminalTheme(const QString& id);
[[nodiscard]] bool terminalThemeExists(const QString& id);

} // namespace slotdeck::terminalcore
