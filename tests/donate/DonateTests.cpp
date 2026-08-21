#include "DonatePlugin.h"
#include "DonateView.h"
#include "TestPluginHost.h"
#include "TestTranslations.h"
#include "ui/Icons.h"

#include <QLabel>
#include <QPushButton>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace slotdeck::plugins::donate {

TEST(DonatePluginTest, PublishesFinalNavigationMetadataAndBundledProfile) {
    DonatePlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("donate"));
    EXPECT_EQ(plugin.titleKey(), QStringLiteral("donate.plugin.title"));
    EXPECT_TRUE(plugin.dependencies().isEmpty());
    EXPECT_TRUE(plugin.settingsGroups().isEmpty());
    EXPECT_FALSE(plugin.styleSheet(ui::themeManager().theme()).isEmpty());
    ASSERT_EQ(plugin.navigationItems(ui::themeManager().theme()).size(), 1);
    EXPECT_EQ(plugin.navigationItems(ui::themeManager().theme()).first().placement, NavigationPlacement::Secondary);
    EXPECT_EQ(plugin.navigationItems(ui::themeManager().theme()).first().order, NavigationOrder::Support);
    EXPECT_EQ(plugin.navigationItems(ui::themeManager().theme()).first().icon.pixmap(32).toImage(), ui::icon(ui::IconName::Donate, ui::themeManager().theme()).pixmap(32).toImage());
    EXPECT_FALSE(profilePixmap(176).isNull());
    EXPECT_EQ(profilePixmap(176, 2.0).deviceIndependentSize(), QSizeF(176.0, 176.0));
    EXPECT_TRUE(profilePixmap(0).isNull());
    EXPECT_TRUE(profilePixmap(176, 0.0).isNull());

    const auto catalog = plugin.translations();
    EXPECT_TRUE(catalog.contains(QStringLiteral("en")));
    EXPECT_EQ(catalog.value(QStringLiteral("pt")).value(QStringLiteral("donate.navigation.support")), QStringLiteral("Doar"));
}

TEST(DonatePluginTest, InitializesBuildsItsViewAndRejectsUnsupportedOperations) {
    test::TestPluginHost host;
    DonatePlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    EXPECT_EQ(plugin.initialize(host).error().code, QStringLiteral("donate_already_initialized"));

    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("support"), nullptr));
    ASSERT_NE(view, nullptr);
    auto* profile = view->findChild<QLabel*>(QStringLiteral("donateProfile"));
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->size(), QSize(176, 176));
    EXPECT_FALSE(profile->pixmap(Qt::ReturnByValue).isNull());
    EXPECT_EQ(view->findChildren<QPushButton*>().size(), 2);
    EXPECT_EQ(plugin.createNavigationView(QStringLiteral("unknown"), nullptr), nullptr);
    EXPECT_EQ(plugin.createSettingsSection(QStringLiteral("unknown"), QStringLiteral("general"), nullptr), nullptr);

    std::optional<utils::Result<QJsonObject>> response;
    // clang-format off
    plugin.handleRequest(QStringLiteral("test"), QStringLiteral("unknown"), {}, [&response](utils::Result<QJsonObject> result) { response = std::move(result); });
    // clang-format on
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->error().code, QStringLiteral("donate_request_unknown"));
    plugin.handleEvent(QStringLiteral("test"), QStringLiteral("event"), {});
    plugin.shutdown();
    EXPECT_EQ(plugin.createNavigationView(QStringLiteral("support"), nullptr), nullptr);
}
} // namespace slotdeck::plugins::donate

TEST(DonateTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    slotdeck::plugins::donate::DonatePlugin plugin;
    slotdeck::test::expectCompleteCatalog(QStringLiteral("donate"), plugin.translations());
}
