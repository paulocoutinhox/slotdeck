#pragma once

#include "utils/Result.h"

#include <QHash>
#include <QSqlDatabase>
#include <QString>

namespace slotdeck::persistence {

inline constexpr int coreDatabaseSchemaVersion = 1;

[[nodiscard]] const QHash<QString, QString>& coreDatabaseTableSchemas();
[[nodiscard]] utils::Result<QHash<QString, int>> validateCoreDatabase(const QSqlDatabase& database);

} // namespace slotdeck::persistence
