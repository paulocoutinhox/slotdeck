#pragma once

#include "utils/Result.h"

#include <QFuture>
#include <QHash>
#include <QString>

namespace slotdeck::persistence {

class ConfigurationTransfer final {
  public:
    [[nodiscard]] static utils::Result<void> exportDatabaseNow(const QString& databasePath, const QString& destinationPath);
    [[nodiscard]] static QFuture<utils::Result<void>> exportDatabase(const QString& databasePath, const QString& destinationPath);
    [[nodiscard]] static QFuture<utils::Result<void>> stageImport(const QString& sourcePath, const QString& pendingPath, const QHash<QString, int>& pluginSchemaVersions);
    [[nodiscard]] static utils::Result<bool> beginPendingImport(const QString& databasePath, const QString& pendingPath, const QString& backupPath, const QHash<QString, int>& pluginSchemaVersions);
    [[nodiscard]] static utils::Result<void> finalizePendingImport(const QString& pendingPath, const QString& backupPath);
    [[nodiscard]] static utils::Result<void> rollbackPendingImport(const QString& databasePath, const QString& pendingPath, const QString& backupPath);
};

} // namespace slotdeck::persistence
