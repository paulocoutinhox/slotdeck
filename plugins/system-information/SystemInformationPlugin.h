#pragma once

#include "SystemInformation.h"
#include "plugins/PluginInterface.h"

#include <QObject>

#include <memory>
#include <optional>

namespace slotdeck::plugins::system_information {

class SystemInformationPlugin final : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SlotDeckPluginInterface_iid)
    Q_INTERFACES(slotdeck::plugins::PluginInterface)

  public:
    SystemInformationPlugin();
    explicit SystemInformationPlugin(std::shared_ptr<SystemInformationProvider> provider);
    ~SystemInformationPlugin() override;
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString titleKey() const override;
    [[nodiscard]] QStringList dependencies() const override;
    [[nodiscard]] int databaseSchemaVersion() const override;
    [[nodiscard]] TranslationCatalog translations() const override;
    [[nodiscard]] QString styleSheet(const ui::Theme& theme) const override;
    [[nodiscard]] QVector<NavigationItem> navigationItems(const ui::Theme& theme) const override;
    [[nodiscard]] QVector<SettingsGroup> settingsGroups() const override;
    [[nodiscard]] utils::Result<void> initialize(PluginHost& host) override;
    [[nodiscard]] QWidget* createNavigationView(const QString& itemId, QWidget* parent) override;
    [[nodiscard]] QWidget* createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) override;
    void handleRequest(const QString& senderPluginId, const QString& topic, const QJsonObject& payload, PluginReply reply) override;
    void handleEvent(const QString& senderPluginId, const QString& topic, const QJsonObject& payload) override;
    void shutdown() override;

    [[nodiscard]] const std::optional<SystemSnapshot>& snapshot() const;
    [[nodiscard]] bool isRefreshing() const;
    [[nodiscard]] utils::Result<void> refresh();

  signals:
    void snapshotChanged();
    void refreshStateChanged(bool refreshing);

  private:
    void completeRefresh(quint64 generation, utils::Result<SystemSnapshot> result);

    PluginHost* m_host{nullptr};
    std::unique_ptr<QObject> m_asyncContext;
    std::shared_ptr<SystemInformationProvider> m_provider;
    std::optional<SystemInformationCollection> m_collection;
    std::optional<SystemSnapshot> m_snapshot;
    quint64 m_collectionGeneration{0};
    bool m_refreshing{false};
};

} // namespace slotdeck::plugins::system_information
