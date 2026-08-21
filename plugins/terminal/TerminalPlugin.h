#pragma once

#include "TerminalSettingsStore.h"
#include "TerminalWorkspaceRepository.h"
#include "plugins/PluginInterface.h"
#include "workspace/WorkspaceManager.h"

#include <QObject>

#include <memory>

namespace slotdeck::plugins::terminalplugin {

class TerminalPlugin final : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SlotDeckPluginInterface_iid)
    Q_INTERFACES(slotdeck::plugins::PluginInterface)

  public:
    TerminalPlugin();
    ~TerminalPlugin() override;

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
    [[nodiscard]] QJsonObject workspaceSnapshot() const;
    void publishWorkspaceChanged();

    PluginHost* m_host{nullptr};
    std::unique_ptr<TerminalSettingsStore> m_settings;
    std::unique_ptr<TerminalWorkspaceRepository> m_repository;
    std::unique_ptr<workspace::WorkspaceManager> m_manager;
};

} // namespace slotdeck::plugins::terminalplugin
