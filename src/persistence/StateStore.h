#pragma once

#include "persistence/PluginDatabase.h"
#include "utils/Result.h"

#include <QJsonObject>
#include <QMutex>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

namespace slotdeck::persistence {

class StateStore final {
  public:
    explicit StateStore(QString filePath);
    ~StateStore();

    [[nodiscard]] utils::Result<void> initialize();
    [[nodiscard]] const QString& filePath() const;
    // Nothing stored may keep the application from opening, so what could not be used is recorded rather than refused.
    [[nodiscard]] const QStringList& rebuiltSchemas() const;
    [[nodiscard]] const QString& replacedDatabasePath() const;
    [[nodiscard]] utils::Result<bool> wasCleanShutdown() const;
    [[nodiscard]] QJsonObject settings(const QString& ownerId) const;
    [[nodiscard]] utils::Result<void> saveSettings(const QString& ownerId, const QJsonObject& document);

    [[nodiscard]] utils::Result<void> markShutdown(bool clean);
    [[nodiscard]] utils::Result<int> pluginSchemaVersion(const QString& pluginId) const;
    [[nodiscard]] utils::Result<void> migratePluginDatabase(const QString& pluginId, const QVector<DatabaseMigration>& migrations);
    [[nodiscard]] utils::Result<void> executePluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] utils::Result<void> executePluginDatabaseTransaction(const QString& pluginId, const QVector<DatabaseStatement>& statements);
    [[nodiscard]] utils::Result<DatabaseRows> queryPluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) const;

  private:
    [[nodiscard]] utils::Result<void> validatePluginSchema(const QString& pluginId, const QVector<DatabaseMigration>& migrations);
    [[nodiscard]] utils::Result<void> applyPluginMigrations(const QString& pluginId, const QVector<DatabaseMigration>& migrations);
    [[nodiscard]] utils::Result<void> dropPluginSchema(const QString& pluginId);
    [[nodiscard]] utils::Result<void> initializeCoreSchema();
    [[nodiscard]] utils::Result<void> validateCoreSchema() const;
    [[nodiscard]] utils::Result<void> execute(const QString& statement, const QVariantList& bindings = {}) const;
    [[nodiscard]] utils::Result<void> executeSingleRowMutation(const QString& statement, const QVariantList& bindings) const;
    [[nodiscard]] utils::Result<DatabaseRows> query(const QString& statement, const QVariantList& bindings = {}) const;
    [[nodiscard]] utils::Result<void> beginTransaction();
    [[nodiscard]] utils::Result<void> commitTransaction();
    void rollbackTransaction();

    [[nodiscard]] utils::Result<void> openDatabase();
    [[nodiscard]] utils::Result<void> replaceUnusableDatabase();

    QString m_filePath;
    QString m_connectionName;
    QStringList m_rebuiltSchemas;
    QString m_replacedDatabasePath;
    mutable QMutex m_mutex;
    QSqlDatabase m_database;
};

} // namespace slotdeck::persistence
