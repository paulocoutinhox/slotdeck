#pragma once

#include "domain/TerminalTheme.h"

#include <QString>
#include <QVector>

namespace slotdeck::terminalcore {

[[nodiscard]] const QVector<domain::TerminalTheme>& terminalThemes();
// A theme is resolved from an identifier the caller already validated, and an unknown one is answered rather than thrown.
[[nodiscard]] const domain::TerminalTheme* terminalTheme(const QString& id);
[[nodiscard]] bool terminalThemeExists(const QString& id);

} // namespace slotdeck::terminalcore
