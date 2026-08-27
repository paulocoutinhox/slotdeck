#pragma once

#include "persistence/PluginDatabase.h"
#include "utils/Result.h"

#include <QFuture>
#include <QJsonObject>
#include <QObject>
#include <QThread>

#include <memory>

namespace slotdeck::domain {
struct ApplicationSettings;
}

namespace slotdeck::persistence {

class StateStore;
class DatabaseWorker;

class DatabaseExecutor final : public QObject {
    Q_OBJECT

  public:
    explicit DatabaseExecutor(QString filePath, QObject* parent = nullptr);
    ~DatabaseExecutor() override;

    DatabaseExecutor(const DatabaseExecutor&) = delete;
    DatabaseExecutor& operator=(const DatabaseExecutor&) = delete;

    [[nodiscard]] QFuture<utils::Result<void>> saveSettings(const QString& ownerId, const QJsonObject& document);
    [[nodiscard]] QFuture<utils::Result<void>> exportConfiguration(const QString& destinationPath);
    [[nodiscard]] QFuture<utils::Result<void>> executePluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] QFuture<utils::Result<void>> executePluginDatabaseTransaction(const QString& pluginId, const QVector<DatabaseStatement>& statements);
    [[nodiscard]] QFuture<utils::Result<DatabaseRows>> queryPluginDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);

  private:
    QThread m_workerThread;
    std::unique_ptr<DatabaseWorker> m_worker;
};

} // namespace slotdeck::persistence
