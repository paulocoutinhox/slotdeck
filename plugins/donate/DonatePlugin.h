#pragma once

#include "plugins/PluginInterface.h"

#include <QObject>

namespace slotdeck::plugins::donate {

class DonatePlugin final : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SlotDeckPluginInterface_iid)
    Q_INTERFACES(slotdeck::plugins::PluginInterface)

  public:
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

  private:
    PluginHost* m_host{nullptr};
};

} // namespace slotdeck::plugins::donate
