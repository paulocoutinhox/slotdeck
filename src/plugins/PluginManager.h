#pragma once

#include "plugins/LocalizationService.h"
#include "plugins/PluginInterface.h"
#include "utils/Result.h"

#include <QObject>

#include <memory>
#include <optional>
#include <vector>

class QPluginLoader;

namespace slotdeck::persistence {
class DatabaseExecutor;
class StateStore;
} // namespace slotdeck::persistence

namespace slotdeck::filesystem {
class FileSystemService;
} // namespace slotdeck::filesystem

namespace slotdeck::ui {
class Theme;
}

namespace slotdeck::plugins {

class ScopedPluginHost;
struct PendingPluginRequest;

struct PluginNavigationItem final {
    QString pluginId;
    NavigationItem item;
};

struct PluginSettingsContribution final {
    QString pluginId;
    SettingsGroup group;
};

class PluginManager final : public QObject {
    Q_OBJECT

  public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;

    [[nodiscard]] utils::Result<void> loadPlugins();
    [[nodiscard]] utils::Result<void> initialize(QString applicationDataPath, persistence::StateStore& stateStore, persistence::DatabaseExecutor& databaseExecutor);
    void shutdown();
    void unloadPlugins();

    [[nodiscard]] QVector<PluginNavigationItem> navigationItems() const;
    [[nodiscard]] QVector<PluginSettingsContribution> settings() const;
    [[nodiscard]] QHash<QString, int> databaseSchemaVersions() const;
    [[nodiscard]] QWidget* createNavigationView(const QString& pluginId, const QString& itemId, QWidget* parent) const;
    [[nodiscard]] QWidget* createSettingsSection(const QString& pluginId, const QString& groupId, const QString& sectionId, QWidget* parent) const;
    [[nodiscard]] QString pluginTitle(const QString& pluginId) const;
    [[nodiscard]] QString styleSheet() const;
    void setTheme(const ui::Theme& theme);
    [[nodiscard]] const ui::Theme& theme() const;

    [[nodiscard]] utils::Result<void> setLocale(const QString& localeName);
    [[nodiscard]] const QString& localeName() const;
    [[nodiscard]] QString translate(const QString& key) const;
    void notify(const QString& title, const QString& message, AlertSeverity severity);
    void publishCoreEvent(const QString& topic, const QJsonObject& payload);

  signals:
    void notificationRequested(const QString& title, const QString& message, AlertSeverity severity);
    void navigationRequested(const QString& pluginId, const QString& navigationId);

  private:
    friend class ScopedPluginHost;

    [[nodiscard]] utils::Result<void> registerPlugin(PluginInterface& plugin);
    [[nodiscard]] PluginInterface* plugin(const QString& pluginId) const;
    [[nodiscard]] bool pluginAvailable(const QString& pluginId) const;
    [[nodiscard]] PluginHost* hostFor(const QString& pluginId) const;
    void showNavigation(const QString& pluginId, const QString& navigationId);
    [[nodiscard]] const QString& applicationDataPath() const;
    [[nodiscard]] QJsonObject settings(const QString& ownerId) const;
    [[nodiscard]] QFuture<utils::Result<void>> saveSettings(const QString& ownerId, const QJsonObject& document);
    [[nodiscard]] utils::Result<void> migrateDatabase(const QString& pluginId, const QVector<persistence::DatabaseMigration>& migrations);
    [[nodiscard]] utils::Result<void> executeBootstrapDatabaseTransaction(const QString& pluginId, const QVector<persistence::DatabaseStatement>& statements);
    [[nodiscard]] utils::Result<persistence::DatabaseRows> queryBootstrapDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings) const;
    [[nodiscard]] QFuture<utils::Result<void>> executeDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] QFuture<utils::Result<void>> executeDatabaseTransaction(const QString& pluginId, const QVector<persistence::DatabaseStatement>& statements);
    [[nodiscard]] QFuture<utils::Result<persistence::DatabaseRows>> queryDatabase(const QString& pluginId, const QString& statement, const QVariantList& bindings);
    [[nodiscard]] QFuture<utils::Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes);
    [[nodiscard]] QFuture<utils::Result<QVector<filesystem::DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries);
    [[nodiscard]] QFuture<utils::Result<void>> writeFile(const QString& path, const QByteArray& content);
    [[nodiscard]] QFuture<utils::Result<void>> createFile(const QString& path);
    [[nodiscard]] QFuture<utils::Result<void>> createDirectory(const QString& path);
    [[nodiscard]] QFuture<utils::Result<void>> movePath(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<utils::Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<utils::Result<void>> removeFile(const QString& path);
    [[nodiscard]] QFuture<utils::Result<void>> removeDirectory(const QString& path);
    [[nodiscard]] bool confirm(QWidget* parent, const QString& title, const QString& message, const QString& detail, const QString& action, bool destructive) const;
    void request(const QString& senderPluginId, const QString& targetPluginId, const QString& topic, const QJsonObject& payload, QObject& callbackContext, PluginReply reply);
    void completeRequest(quint64 requestId, utils::Result<QJsonObject> result);
    void removeRequest(quint64 requestId);
    void cancelRequests();
    void publish(const QString& senderPluginId, const QString& topic, const QJsonObject& payload);
    void log(const QString& senderPluginId, LogLevel level, const QString& category, const QString& message, const QJsonObject& details);
    void notifyPlugin(const QString& senderPluginId, const QString& title, const QString& message, AlertSeverity severity);
    LocalizationService m_localization;
    QString m_applicationDataPath;
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
    std::vector<std::unique_ptr<PluginHost>> m_pluginHosts;
    QVector<PluginInterface*> m_plugins;
    QVector<PluginInterface*> m_initializedPlugins;
    QVector<PluginNavigationItem> m_navigationItems;
    QHash<quint64, std::shared_ptr<PendingPluginRequest>> m_pendingRequests;
    persistence::StateStore* m_stateStore{nullptr};
    persistence::DatabaseExecutor* m_databaseExecutor{nullptr};
    std::unique_ptr<filesystem::FileSystemService> m_fileSystem;
    std::optional<utils::Error> m_coreCatalogError;
    const ui::Theme* m_theme{nullptr};
    quint64 m_nextRequestId{0};
    bool m_initialized{false};
};

} // namespace slotdeck::plugins
