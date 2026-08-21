#pragma once

#include "filesystem/FileSystemService.h"
#include "persistence/StateStore.h"
#include "plugins/PluginInterface.h"
#include "ui/Theme.h"

#include <QPointer>
#include <QSet>

#include <functional>
#include <optional>
#include <utility>

namespace slotdeck::test {

struct PluginRequest final {
    QString targetPluginId;
    QString topic;
    QJsonObject payload;
    QPointer<QObject> callbackContext;
};

struct PluginEvent final {
    QString topic;
    QJsonObject payload;
};

struct Notification final {
    QString title;
    QString message;
    plugins::AlertSeverity severity{plugins::AlertSeverity::Information};
};

struct LogMessage final {
    plugins::LogLevel level;
    QString category;
    QString message;
    QJsonObject details;
};

class TestPluginHost final : public plugins::PluginHost {
  public:
    [[nodiscard]] QString translate(const QString& key) const override {
        return translations.value(key, key);
    }

    [[nodiscard]] const ui::Theme& theme() const override {
        return ui::themeManager().theme();
    }

    [[nodiscard]] bool pluginAvailable(const QString& pluginId) const override {
        return availablePlugins.contains(pluginId);
    }

    [[nodiscard]] const QString& applicationDataPath() const override {
        return dataPath;
    }

    [[nodiscard]] QJsonObject settings() const override {
        return settingsDocument;
    }

    [[nodiscard]] QFuture<utils::Result<void>> saveSettings(const QJsonObject& document) override {
        savedSettings.append(document);
        return settingsFutureHandler ? settingsFutureHandler(document) : QtFuture::makeReadyValueFuture(utils::Result<void>::success());
    }

    [[nodiscard]] utils::Result<void> migrateDatabase(const QVector<persistence::DatabaseMigration>& migrations) override {
        appliedMigrations += migrations;
        if (migrationHandler) {
            return migrationHandler(migrations);
        }
        if (migrationError.has_value()) {
            return utils::Result<void>::failure(migrationError.value());
        }
        if (!migrations.isEmpty()) {
            schemaVersion = migrations.last().version;
        }
        return utils::Result<void>::success();
    }

    [[nodiscard]] utils::Result<void> executeBootstrapDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) override {
        databaseTransactions.append(statements);
        if (transactionHandler) {
            return transactionHandler(statements);
        }
        if (transactionError.has_value()) {
            return utils::Result<void>::failure(transactionError.value());
        }
        return utils::Result<void>::success();
    }

    [[nodiscard]] utils::Result<persistence::DatabaseRows> queryBootstrapDatabase(const QString& statement, const QVariantList& bindings) const override {
        return queryResult(statement, bindings);
    }

    [[nodiscard]] QFuture<utils::Result<void>> executeDatabase(const QString& statement, const QVariantList& bindings) override {
        databaseExecutions.append({{QStringLiteral("statement"), statement}, {QStringLiteral("bindings"), bindings}});
        if (executeHandler) {
            return QtFuture::makeReadyValueFuture(executeHandler(statement, bindings));
        }
        if (executeFutureHandler) {
            return executeFutureHandler(statement, bindings);
        }
        if (executeError.has_value()) {
            return QtFuture::makeReadyValueFuture(utils::Result<void>::failure(executeError.value()));
        }
        return QtFuture::makeReadyValueFuture(utils::Result<void>::success());
    }

    [[nodiscard]] QFuture<utils::Result<void>> executeDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) override {
        databaseTransactions.append(statements);
        if (transactionFutureHandler) {
            return transactionFutureHandler(statements);
        }
        if (transactionError.has_value()) {
            return QtFuture::makeReadyValueFuture(utils::Result<void>::failure(transactionError.value()));
        }
        return QtFuture::makeReadyValueFuture(utils::Result<void>::success());
    }

    [[nodiscard]] QFuture<utils::Result<persistence::DatabaseRows>> queryDatabase(const QString& statement, const QVariantList& bindings) override {
        if (queryFutureHandler) {
            return queryFutureHandler(statement, bindings);
        }
        return QtFuture::makeReadyValueFuture(queryResult(statement, bindings));
    }

    // A suite that exercises files binds the real service, because a double answering more than the service answers proves nothing.
    // A statement the real store refuses must not pass here, and a query it answers by real SQL must not be answered by a double that only pretends to.
    void useDatabase(persistence::StateStore& store, const QString& pluginId) {
        // clang-format off
        migrationHandler = [&store, pluginId](const QVector<persistence::DatabaseMigration>& migrations) { return store.migratePluginDatabase(pluginId, migrations); };
        transactionHandler = [&store, pluginId](const QVector<persistence::DatabaseStatement>& statements) { return store.executePluginDatabaseTransaction(pluginId, statements); };
        queryHandler = [&store, pluginId](const QString& statement, const QVariantList& bindings) { return store.queryPluginDatabase(pluginId, statement, bindings); };
        executeHandler = [&store, pluginId](const QString& statement, const QVariantList& bindings) { return store.executePluginDatabase(pluginId, statement, bindings); };
        transactionFutureHandler = [&store, pluginId](const QVector<persistence::DatabaseStatement>& statements) { return QtFuture::makeReadyValueFuture(store.executePluginDatabaseTransaction(pluginId, statements)); };
        executeFutureHandler = [&store, pluginId](const QString& statement, const QVariantList& bindings) { return QtFuture::makeReadyValueFuture(store.executePluginDatabase(pluginId, statement, bindings)); };
        // clang-format on
    }

    void useFileSystem(filesystem::FileSystemService& service) {
        // clang-format off
        readFileHandler = [&service](const QString& path, qint64 maximumBytes) { return service.readFile(path, maximumBytes); };
        listDirectoryHandler = [&service](const QString& path, int maximumEntries) { return service.listDirectory(path, maximumEntries); };
        writeFileHandler = [&service](const QString& path, const QByteArray& content) { return service.writeFile(path, content); };
        pathOperationHandler = [&service](const QString& operation, const QString& source, const QString& destination) {
            if (operation == QStringLiteral("create-file")) {
                return service.createFile(source);
            }
            if (operation == QStringLiteral("create-directory")) {
                return service.createDirectory(source);
            }
            if (operation == QStringLiteral("move")) {
                return service.movePath(source, destination);
            }
            if (operation == QStringLiteral("copy")) {
                return service.copyFile(source, destination);
            }
            if (operation == QStringLiteral("remove-file")) {
                return service.removeFile(source);
            }
            return service.removeDirectory(source);
        };
        // clang-format on
    }

    [[nodiscard]] QFuture<utils::Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes) override {
        return readFileHandler ? readFileHandler(path, maximumBytes) : QtFuture::makeReadyValueFuture(utils::Result<QByteArray>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<utils::Result<QVector<filesystem::DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries) override {
        return listDirectoryHandler ? listDirectoryHandler(path, maximumEntries) : QtFuture::makeReadyValueFuture(utils::Result<QVector<filesystem::DirectoryEntry>>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<utils::Result<void>> writeFile(const QString& path, const QByteArray& content) override {
        return writeFileHandler ? writeFileHandler(path, content) : QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<utils::Result<void>> createFile(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("create-file"), path, {}) : QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<utils::Result<void>> createDirectory(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("create-directory"), path, {}) : QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<utils::Result<void>> movePath(const QString& sourcePath, const QString& destinationPath) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("move"), sourcePath, destinationPath) : QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", sourcePath}));
    }

    [[nodiscard]] QFuture<utils::Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("copy"), sourcePath, destinationPath) : QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", sourcePath}));
    }

    [[nodiscard]] QFuture<utils::Result<void>> removeFile(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("remove-file"), path, {}) : QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] QFuture<utils::Result<void>> removeDirectory(const QString& path) override {
        return pathOperationHandler ? pathOperationHandler(QStringLiteral("remove-directory"), path, {}) : QtFuture::makeReadyValueFuture(utils::Result<void>::failure({"test_filesystem_unavailable", "The test filesystem is unavailable", path}));
    }

    [[nodiscard]] utils::Result<persistence::DatabaseRows> queryResult(const QString& statement, const QVariantList& bindings) const {
        if (queryError.has_value()) {
            return utils::Result<persistence::DatabaseRows>::failure(queryError.value());
        }
        if (queryHandler) {
            return queryHandler(statement, bindings);
        }
        return utils::Result<persistence::DatabaseRows>::success(databaseRows);
    }

    [[nodiscard]] bool confirm(QWidget*, const QString&, const QString&, const QString&, const QString&, bool) const override {
        return confirmation;
    }

    void request(const QString& targetPluginId, const QString& topic, const QJsonObject& payload, QObject& callbackContext, plugins::PluginReply reply) override {
        requests.append({targetPluginId, topic, payload, &callbackContext});
        if (requestHandler) {
            requestHandler(targetPluginId, topic, payload, &callbackContext, std::move(reply));
        }
    }

    void publish(const QString& topic, const QJsonObject& payload) override {
        events.append({topic, payload});
    }

    void log(plugins::LogLevel level, const QString& category, const QString& message, const QJsonObject& details) override {
        logs.append({level, category, message, details});
    }

    void notify(const QString& title, const QString& message, plugins::AlertSeverity severity) override {
        notifications.append({title, message, severity});
    }

    void showNavigation(const QString& navigationId) override {
        revealedNavigation.append(navigationId);
    }

    QString dataPath;
    QSet<QString> availablePlugins;
    QHash<QString, QString> translations;
    QVector<PluginRequest> requests;
    QVector<PluginEvent> events;
    QVector<Notification> notifications;
    QStringList revealedNavigation;
    QVector<LogMessage> logs;
    QJsonObject settingsDocument;
    QVector<QJsonObject> savedSettings;
    QVector<persistence::DatabaseMigration> appliedMigrations;
    QVector<QVariantMap> databaseExecutions;
    QVector<QVector<persistence::DatabaseStatement>> databaseTransactions;
    std::function<utils::Result<void>(const QVector<persistence::DatabaseMigration>&)> migrationHandler;
    std::function<utils::Result<void>(const QVector<persistence::DatabaseStatement>&)> transactionHandler;
    std::function<utils::Result<void>(const QString&, const QVariantList&)> executeHandler;
    persistence::DatabaseRows databaseRows;
    std::optional<utils::Error> migrationError;
    std::optional<utils::Error> executeError;
    std::optional<utils::Error> transactionError;
    std::optional<utils::Error> queryError;
    bool confirmation{true};
    int schemaVersion{0};
    std::function<void(const QString&, const QString&, const QJsonObject&, QObject*, plugins::PluginReply)> requestHandler;
    std::function<utils::Result<persistence::DatabaseRows>(const QString&, const QVariantList&)> queryHandler;
    std::function<QFuture<utils::Result<void>>(const QString&, const QVariantList&)> executeFutureHandler;
    std::function<QFuture<utils::Result<void>>(const QVector<persistence::DatabaseStatement>&)> transactionFutureHandler;
    std::function<QFuture<utils::Result<persistence::DatabaseRows>>(const QString&, const QVariantList&)> queryFutureHandler;
    std::function<QFuture<utils::Result<void>>(const QJsonObject&)> settingsFutureHandler;
    std::function<QFuture<utils::Result<QByteArray>>(const QString&, qint64)> readFileHandler;
    std::function<QFuture<utils::Result<QVector<filesystem::DirectoryEntry>>>(const QString&, int)> listDirectoryHandler;
    std::function<QFuture<utils::Result<void>>(const QString&, const QByteArray&)> writeFileHandler;
    std::function<QFuture<utils::Result<void>>(const QString&, const QString&, const QString&)> pathOperationHandler;
};

} // namespace slotdeck::test
