#include "DonatePlugin.h"

#include "DonateTranslations.h"
#include "DonateView.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace slotdeck::plugins::donate {

QString DonatePlugin::id() const {
    return QStringLiteral("donate");
}

QString DonatePlugin::titleKey() const {
    return QStringLiteral("donate.plugin.title");
}

QStringList DonatePlugin::dependencies() const {
    return {};
}

int DonatePlugin::databaseSchemaVersion() const {
    return 0;
}

TranslationCatalog DonatePlugin::translations() const {
    return translations::catalog();
}

QString DonatePlugin::styleSheet(const ui::Theme&) const {
    return QStringLiteral("QFrame#donateCard { background: @panel; border: 1px solid @border; border-radius: 8px; } QLabel#donateName { color: @text; font-size: 12px; font-weight: 600; } QLabel#donateTitle { color: @text; font-size: 22px; font-weight: 700; } QLabel#donateDescription { color: @textMuted; font-size: 14px; }");
}

QVector<NavigationItem> DonatePlugin::navigationItems(const ui::Theme& theme) const {
    return {{QStringLiteral("support"), QStringLiteral("donate.navigation.support"), ui::icon(ui::IconName::Donate, theme), NavigationPlacement::Secondary, NavigationOrder::Support}};
}

QVector<SettingsGroup> DonatePlugin::settingsGroups() const {
    return {};
}

utils::Result<void> DonatePlugin::initialize(PluginHost& host) {
    if (m_host != nullptr) {
        return utils::Result<void>::failure({"donate_already_initialized", "The Donate plugin is already initialized", {}});
    }

    m_host = &host;
    return utils::Result<void>::success();
}

QWidget* DonatePlugin::createNavigationView(const QString& itemId, QWidget* parent) {
    return itemId == QStringLiteral("support") && m_host != nullptr ? new DonateView(*m_host, parent) : nullptr;
}

QWidget* DonatePlugin::createSettingsSection(const QString&, const QString&, QWidget*) {
    return nullptr;
}

void DonatePlugin::handleRequest(const QString&, const QString& topic, const QJsonObject&, PluginReply reply) {
    reply(unhandledTopic(topic));
}

void DonatePlugin::handleEvent(const QString&, const QString&, const QJsonObject&) {}

void DonatePlugin::shutdown() {
    m_host = nullptr;
}

} // namespace slotdeck::plugins::donate
