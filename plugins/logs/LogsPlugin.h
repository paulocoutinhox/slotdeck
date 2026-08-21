#pragma once

#include "plugins/PluginInterface.h"

#include <QDateTime>

#include <memory>

namespace slotdeck::plugins::logs {

struct LogEntry final {
    qint64 sequence{0};
    QDateTime timestampUtc;
    QString sourcePluginId;
    QString level;
    QString category;
    QString message;
    QJsonObject details;
};

class LogsPlugin final : public QObject, public PluginInterface {
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

    [[nodiscard]] QFuture<utils::Result<QVector<LogEntry>>> entries(qint64 beforeSequence, int limit);
    [[nodiscard]] QFuture<utils::Result<void>> clearEntries();

  signals:
    void entriesChanged();

  private:
    [[nodiscard]] QFuture<utils::Result<void>> appendEntry(const QString& senderPluginId, const QJsonObject& payload);

    PluginHost* m_host{nullptr};
    std::unique_ptr<QObject> m_asyncContext;
};

} // namespace slotdeck::plugins::logs
