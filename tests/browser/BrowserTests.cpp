#include "BrowserBookmarksView.h"
#include "BrowserPlugin.h"
#include "BrowserTranslations.h"
#include "TestPluginHost.h"
#include "TestTranslations.h"
#include "ui/Icons.h"

#include <QCoreApplication>
#include <QDir>
#include <QPromise>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>

namespace slotdeck::plugins::browser {

TEST(BrowserPluginTest, PublishesCompleteMetadataAndNormalizesSupportedAddresses) {
    BrowserPlugin plugin;
    EXPECT_EQ(plugin.id(), QStringLiteral("browser"));
    EXPECT_EQ(plugin.titleKey(), QStringLiteral("browser.plugin.title"));
    EXPECT_TRUE(plugin.dependencies().isEmpty());
    EXPECT_EQ(plugin.navigationItems(ui::themeManager().theme()).size(), 1);
    EXPECT_EQ(plugin.settingsGroups().size(), 1);
    const auto catalog = plugin.translations();
    EXPECT_TRUE(catalog.contains(QStringLiteral("en")));
    EXPECT_TRUE(catalog.contains(QStringLiteral("pt")));
    EXPECT_EQ(catalog.value(QStringLiteral("en")).value(QStringLiteral("browser.bookmarks.title")), QStringLiteral("Bookmarks"));
    EXPECT_EQ(catalog.value(QStringLiteral("pt")).value(QStringLiteral("browser.bookmarks.title")), QStringLiteral("Favoritos"));

    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("example.com")).value(), QUrl(QStringLiteral("https://example.com")));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("http://localhost:8080/path")).value(), QUrl(QStringLiteral("http://localhost:8080/path")));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("about:blank")).value(), QUrl(QStringLiteral("about:blank")));
    EXPECT_EQ(BrowserPlugin::normalizeAddress({}).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("javascript:alert(1)")).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(BrowserPlugin::normalizeAddress(QStringLiteral("https:///missing-host")).error().code, QStringLiteral("browser_address_invalid"));
}

class BrowserTestsHelper final {
  public:
    static test::TestPluginHost browserHost();
};

TEST(BrowserPluginTest, StartsWithTheHomepageTabWhenNoTabWasStored) {
    test::TestPluginHost host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.tabs().size(), 1);
    EXPECT_EQ(plugin.tabs().first().url, plugin.homepage());
    EXPECT_TRUE(plugin.tabs().first().active);
    plugin.shutdown();
}
TEST(BrowserViewTest, PresentsEitherTheTabsOrTheEmptyStateAndNeverBoth) {
    test::TestPluginHost host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    std::unique_ptr<QWidget> view(plugin.createNavigationView(QStringLiteral("web"), nullptr));
    ASSERT_NE(view, nullptr);
    view->resize(900, 600);
    view->show();

    auto* pages = view->findChild<QStackedWidget*>(QStringLiteral("browserPages"));
    auto* tabs = view->findChild<QTabWidget*>(QStringLiteral("browserTabs"));
    ASSERT_NE(pages, nullptr);
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 1);
    EXPECT_EQ(pages->currentWidget(), tabs);

    ASSERT_TRUE(plugin.closeTab(plugin.tabs().first().id).hasValue());
    EXPECT_EQ(tabs->count(), 0);
    EXPECT_NE(pages->currentWidget(), tabs);
    EXPECT_TRUE(pages->currentWidget()->isVisible());
    EXPECT_FALSE(tabs->isVisible());

    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("about:blank")), true).hasValue());
    EXPECT_EQ(tabs->count(), 1);
    EXPECT_EQ(pages->currentWidget(), tabs);
    EXPECT_TRUE(tabs->isVisible());

    view.reset();
    plugin.shutdown();
}
TEST(BrowserPluginTest, RestoresMutatesAndPersistsTheCompleteTabSession) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.tabs().size(), 1);
    EXPECT_TRUE(plugin.tabs().first().active);
    EXPECT_EQ(host.appliedMigrations.size(), 1);
    EXPECT_EQ(host.databaseTransactions.size(), 1);

    const QString originalId = plugin.tabs().first().id;
    const auto created = plugin.createTab(QUrl(QStringLiteral("https://example.com")), true);
    ASSERT_TRUE(created.hasValue());
    ASSERT_EQ(plugin.tabs().size(), 2);
    EXPECT_TRUE(plugin.tabs().last().active);
    EXPECT_FALSE(plugin.tabs().first().active);
    EXPECT_TRUE(plugin.updateTabTitle(created.value(), QStringLiteral("Example")).hasValue());
    EXPECT_TRUE(plugin.updateTabUrl(created.value(), QUrl(QStringLiteral("https://example.org"))).hasValue());
    EXPECT_TRUE(plugin.moveTab(1, 0).hasValue());
    EXPECT_TRUE(plugin.activateTab(originalId).hasValue());
    EXPECT_TRUE(plugin.closeTab(originalId).hasValue());
    EXPECT_EQ(plugin.tabs().size(), 1);
    EXPECT_EQ(plugin.tabs().first().title, QStringLiteral("Example"));
    EXPECT_TRUE(plugin.tabs().first().active);
    EXPECT_GT(host.databaseTransactions.size(), 1);

    const QString finalId = plugin.tabs().first().id;
    EXPECT_TRUE(plugin.closeTab(finalId).hasValue());
    EXPECT_TRUE(plugin.tabs().isEmpty()) << "closing the last tab releases its renderer instead of opening a replacement";

    const auto restored = plugin.createTab(plugin.homepage(), true);
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ(plugin.tabs().size(), 1);
    EXPECT_EQ(plugin.tabs().first().url, plugin.homepage());
    EXPECT_TRUE(plugin.tabs().first().active);

    EXPECT_EQ(plugin.closeTab(QStringLiteral("missing")).error().code, QStringLiteral("browser_tab_unknown"));
    EXPECT_EQ(plugin.activateTab(QStringLiteral("missing")).error().code, QStringLiteral("browser_tab_unknown"));
    EXPECT_EQ(plugin.moveTab(-1, 0).error().code, QStringLiteral("browser_tab_position_invalid"));
    EXPECT_EQ(plugin.updateTabTitle(plugin.tabs().first().id, {}).error().code, QStringLiteral("browser_tab_title_invalid"));
    EXPECT_EQ(plugin.setHomepage(QStringLiteral("invalid value")).error().code, QStringLiteral("browser_address_invalid"));
}
TEST(BrowserPluginTest, KeepsTabsAndBookmarksCompleteThroughManyRoundsOfMutation) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    const auto firstGroup = plugin.createBookmarkGroup(QStringLiteral("First"));
    ASSERT_TRUE(firstGroup.hasValue());
    const auto secondGroup = plugin.createBookmarkGroup(QStringLiteral("Second"));
    ASSERT_TRUE(secondGroup.hasValue());

    for (int round = 0; round < 25; ++round) {
        QStringList openedTabs;
        for (int index = 0; index < 6; ++index) {
            const auto created = plugin.createTab(QUrl(QStringLiteral("https://example.com/%1/%2").arg(round).arg(index)), index % 2 == 0);
            ASSERT_TRUE(created.hasValue());
            openedTabs.append(created.value());
        }
        ASSERT_TRUE(plugin.moveTab(static_cast<int>(plugin.tabs().size()) - 1, 0).hasValue());
        ASSERT_TRUE(plugin.activateTab(openedTabs.first()).hasValue());

        const auto bookmark = plugin.createBookmark(QStringLiteral("Entry %1").arg(round), QStringLiteral("https://example.com/%1").arg(round), round % 2 == 0 ? firstGroup.value() : secondGroup.value());
        ASSERT_TRUE(bookmark.hasValue());

        QVector<BrowserBookmarkPlacement> placements;
        for (const auto& entry : plugin.bookmarks()) {
            placements.prepend({entry.id, entry.groupId == firstGroup.value() ? secondGroup.value() : firstGroup.value()});
        }
        ASSERT_TRUE(plugin.applyBookmarkLayout({secondGroup.value(), firstGroup.value()}, placements).hasValue());

        for (const auto& tabId : openedTabs) {
            ASSERT_TRUE(plugin.closeTab(tabId).hasValue());
        }
        ASSERT_TRUE(plugin.removeBookmark(bookmark.value()).hasValue());
    }

    // Every round left the session exactly as it found it, so nothing was orphaned by a layout applied over a mutation.
    EXPECT_EQ(plugin.tabs().size(), 1);
    EXPECT_TRUE(plugin.tabs().first().active);
    EXPECT_TRUE(plugin.bookmarks().isEmpty());
    EXPECT_EQ(plugin.bookmarkGroups().size(), 2);
    plugin.shutdown();
}

TEST(BrowserPluginTest, HandlesOpenRequestsAndReportsPersistenceFailures) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    host.transactionError = utils::Error{"database_failed", "Database failed", {}};
    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("https://example.com")), true).hasValue());
    QCoreApplication::processEvents();
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().severity, AlertSeverity::Error);
    EXPECT_EQ(plugin.tabs().size(), 1);

    std::optional<utils::Result<QJsonObject>> reply;
    // clang-format off
    plugin.handleRequest(QStringLiteral("sample"), QStringLiteral("browser.open"), {{QStringLiteral("url"), QStringLiteral("qt.io")}}, [&reply](utils::Result<QJsonObject> result) { reply = std::move(result); });
    // clang-format on
    ASSERT_TRUE(reply.has_value());
    EXPECT_TRUE(reply->hasValue());
    EXPECT_FALSE(reply->value().value(QStringLiteral("tabId")).toString().isEmpty());

    // A caller asking for a page wants to read it, so the Browser reveals itself rather than opening a tab nobody is looking at.
    EXPECT_EQ(host.revealedNavigation, QStringList{QStringLiteral("web")});

    // A request the plugin does not answer reveals nothing.
    std::optional<utils::Result<QJsonObject>> refused;
    // clang-format off
    plugin.handleRequest(QStringLiteral("sample"), QStringLiteral("browser.open"), {{QStringLiteral("url"), QStringLiteral("qt.io")}, {QStringLiteral("unknown"), true}}, [&refused](utils::Result<QJsonObject> answer) { refused = std::move(answer); });
    // clang-format on
    ASSERT_TRUE(refused.has_value());
    EXPECT_FALSE(refused->hasValue());
    EXPECT_EQ(host.revealedNavigation.size(), 1);

    reply.reset();
    // clang-format off
    plugin.handleRequest(QStringLiteral("sample"), QStringLiteral("unknown"), {}, [&reply](utils::Result<QJsonObject> result) { reply = std::move(result); });
    // clang-format on
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->error().code, QStringLiteral("plugin_message_topic_unknown"));
}
TEST(BrowserPluginTest, CancelsPendingPersistenceCallbacksDuringShutdown) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    auto transaction = std::make_shared<QPromise<utils::Result<void>>>();
    transaction->start();
    // clang-format off
    host.transactionFutureHandler = [transaction](const QVector<persistence::DatabaseStatement>&) { return transaction->future(); };
    // clang-format on

    ASSERT_TRUE(plugin.createTab(QUrl(QStringLiteral("https://example.com")), true).hasValue());
    plugin.shutdown();
    transaction->addResult(utils::Result<void>::failure({"write_failed", "Write failed", {}}));
    transaction->finish();
    QCoreApplication::processEvents();

    EXPECT_TRUE(host.notifications.isEmpty());
}
TEST(BrowserPluginTest, RejectsCorruptPersistedSessions) {
    auto host = BrowserTestsHelper::browserHost();
    // clang-format off
    host.queryHandler = [](const QString&, const QVariantList&) {
        return utils::Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("tab")}, {QStringLiteral("position"), 0}, {QStringLiteral("title"), QStringLiteral("Broken")}, {QStringLiteral("url"), QStringLiteral("https://example.com")}, {QStringLiteral("created_at_utc"), QStringLiteral("invalid")}, {QStringLiteral("updated_at_utc"), QStringLiteral("invalid")}, {QStringLiteral("active"), 1}}});
    };
    // clang-format on
    BrowserPlugin plugin;
    EXPECT_EQ(plugin.initialize(host).error().code, QStringLiteral("browser_session_invalid"));
}
TEST(BrowserPluginTest, RestoresOrderedUrlsTitlesTimestampsAndTheActiveTab) {
    auto host = BrowserTestsHelper::browserHost();
    host.settingsDocument = {{QStringLiteral("homepage"), QStringLiteral("https://slotdeck.local")}};
    const QString created = QStringLiteral("2026-08-14T12:00:00.000Z");
    const QString updated = QStringLiteral("2026-08-14T12:01:00.000Z");
    // clang-format off
    host.queryHandler = [created, updated](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_bookmark_groups")) || statement.contains(QStringLiteral("browser_bookmarks"))) {
            return utils::Result<persistence::DatabaseRows>::success({});
        }
        return utils::Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("first")}, {QStringLiteral("position"), 0}, {QStringLiteral("title"), QStringLiteral("First")}, {QStringLiteral("url"), QStringLiteral("https://example.com/one")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}, {QStringLiteral("active"), 0}}, {{QStringLiteral("id"), QStringLiteral("second")}, {QStringLiteral("position"), 1}, {QStringLiteral("title"), QStringLiteral("Second")}, {QStringLiteral("url"), QStringLiteral("https://example.com/two")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}, {QStringLiteral("active"), 1}}});
    };
    // clang-format on
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.tabs().size(), 2);
    EXPECT_EQ(plugin.homepage(), QUrl(QStringLiteral("https://slotdeck.local")));
    EXPECT_EQ(plugin.tabs().first().id, QStringLiteral("first"));
    EXPECT_EQ(plugin.tabs().last().title, QStringLiteral("Second"));
    EXPECT_EQ(plugin.tabs().last().url, QUrl(QStringLiteral("https://example.com/two")));
    EXPECT_EQ(plugin.tabs().last().createdAtUtc.timeSpec(), Qt::UTC);
    EXPECT_EQ(plugin.tabs().last().updatedAtUtc.timeSpec(), Qt::UTC);
    EXPECT_TRUE(plugin.tabs().last().active);
}
TEST(BrowserPluginTest, ManagesOrderedBookmarkGroupsAndBookmarksWithStrictValidation) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.bookmarkGroups().isEmpty());
    ASSERT_TRUE(plugin.bookmarks().isEmpty());
    ASSERT_EQ(host.appliedMigrations.size(), 1);
    EXPECT_TRUE(host.appliedMigrations.first().statements.join(QLatin1Char(' ')).contains(QStringLiteral("browser_bookmark_groups")));
    EXPECT_TRUE(host.appliedMigrations.first().statements.join(QLatin1Char(' ')).contains(QStringLiteral("browser_bookmarks")));

    const auto work = plugin.createBookmarkGroup(QStringLiteral(" Work "));
    const auto personal = plugin.createBookmarkGroup(QStringLiteral("Personal"));
    ASSERT_TRUE(work.hasValue());
    ASSERT_TRUE(personal.hasValue());
    EXPECT_EQ(plugin.bookmarkGroups().first().name, QStringLiteral("Work"));
    EXPECT_EQ(plugin.createBookmarkGroup(QStringLiteral("  ")).error().code, QStringLiteral("browser_bookmark_group_name_invalid"));
    EXPECT_EQ(plugin.updateBookmarkGroup(QStringLiteral("missing"), QStringLiteral("Name")).error().code, QStringLiteral("browser_bookmark_group_unknown"));
    EXPECT_EQ(plugin.updateBookmarkGroup(work.value(), {}).error().code, QStringLiteral("browser_bookmark_group_name_invalid"));
    ASSERT_TRUE(plugin.updateBookmarkGroup(work.value(), QStringLiteral("Engineering")).hasValue());
    EXPECT_EQ(plugin.bookmarkGroups().first().name, QStringLiteral("Engineering"));

    const auto qt = plugin.createBookmark(QStringLiteral(" Qt "), QStringLiteral("qt.io"), work.value());
    const auto news = plugin.createBookmark(QStringLiteral("News"), QStringLiteral("https://example.com/news"), {});
    ASSERT_TRUE(qt.hasValue());
    ASSERT_TRUE(news.hasValue());
    ASSERT_EQ(plugin.bookmarks().size(), 2);
    EXPECT_EQ(plugin.bookmarks().first().name, QStringLiteral("Qt"));
    EXPECT_EQ(plugin.bookmarks().first().url, QUrl(QStringLiteral("https://qt.io")));
    EXPECT_EQ(plugin.bookmarks().first().createdAtUtc.timeSpec(), Qt::UTC);
    EXPECT_EQ(plugin.createBookmark({}, QStringLiteral("https://example.com"), {}).error().code, QStringLiteral("browser_bookmark_name_invalid"));
    EXPECT_EQ(plugin.createBookmark(QStringLiteral("Invalid"), QStringLiteral("javascript:alert(1)"), {}).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(plugin.createBookmark(QStringLiteral("Invalid"), QStringLiteral("https://example.com"), QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_group_unknown"));

    ASSERT_TRUE(plugin.updateBookmark(qt.value(), QStringLiteral("Qt Docs"), QStringLiteral("https://doc.qt.io"), personal.value()).hasValue());
    EXPECT_EQ(plugin.bookmarks().first().groupId, personal.value());
    EXPECT_EQ(plugin.bookmarks().first().url, QUrl(QStringLiteral("https://doc.qt.io")));
    EXPECT_EQ(plugin.updateBookmark(QStringLiteral("missing"), QStringLiteral("Name"), QStringLiteral("https://example.com"), {}).error().code, QStringLiteral("browser_bookmark_unknown"));
    EXPECT_EQ(plugin.updateBookmark(qt.value(), {}, QStringLiteral("https://example.com"), {}).error().code, QStringLiteral("browser_bookmark_name_invalid"));
    EXPECT_EQ(plugin.updateBookmark(qt.value(), QStringLiteral("Name"), QStringLiteral("invalid value"), {}).error().code, QStringLiteral("browser_address_invalid"));
    EXPECT_EQ(plugin.updateBookmark(qt.value(), QStringLiteral("Name"), QStringLiteral("https://example.com"), QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_group_unknown"));

    const QVector<QString> groupOrder{personal.value(), work.value()};
    const QVector<BrowserBookmarkPlacement> bookmarkOrder{{news.value(), personal.value()}, {qt.value(), {}}};
    ASSERT_TRUE(plugin.applyBookmarkLayout(groupOrder, bookmarkOrder).hasValue());
    EXPECT_EQ(plugin.bookmarkGroups().first().id, personal.value());
    EXPECT_EQ(plugin.bookmarks().first().id, news.value());
    EXPECT_EQ(plugin.bookmarks().first().groupId, personal.value());
    EXPECT_TRUE(plugin.bookmarks().last().groupId.isEmpty());
    EXPECT_EQ(plugin.applyBookmarkLayout({personal.value()}, bookmarkOrder).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout({personal.value(), personal.value()}, bookmarkOrder).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout({personal.value(), QStringLiteral("missing")}, bookmarkOrder).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout(groupOrder, {{news.value(), {}}, {news.value(), {}}}).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout(groupOrder, {{news.value(), QStringLiteral("missing")}, {qt.value(), {}}}).error().code, QStringLiteral("browser_bookmark_layout_invalid"));
    EXPECT_EQ(plugin.applyBookmarkLayout(groupOrder, {{QStringLiteral("missing"), {}}, {qt.value(), {}}}).error().code, QStringLiteral("browser_bookmark_layout_invalid"));

    ASSERT_TRUE(plugin.removeBookmarkGroup(personal.value()).hasValue());
    EXPECT_TRUE(plugin.bookmarks().first().groupId.isEmpty());
    EXPECT_EQ(plugin.removeBookmarkGroup(QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_group_unknown"));
    ASSERT_TRUE(plugin.removeBookmark(news.value()).hasValue());
    EXPECT_EQ(plugin.removeBookmark(QStringLiteral("missing")).error().code, QStringLiteral("browser_bookmark_unknown"));
    EXPECT_EQ(plugin.bookmarks().size(), 1);
    EXPECT_GT(host.databaseTransactions.size(), 6);
}
// Removing a group keeps every bookmark it held, in the order they were in, because a group is a way of arranging them and not a thing that owns them.
TEST(BrowserPluginTest, KeepsEveryBookmarkOfAGroupInOrderWhenThatGroupIsRemoved) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());

    const auto group = plugin.createBookmarkGroup(QStringLiteral("Reading"));
    ASSERT_TRUE(group.hasValue());
    const auto loose = plugin.createBookmark(QStringLiteral("Loose"), QStringLiteral("https://example.com/loose"), {});
    const auto first = plugin.createBookmark(QStringLiteral("First"), QStringLiteral("https://example.com/first"), group.value());
    const auto second = plugin.createBookmark(QStringLiteral("Second"), QStringLiteral("https://example.com/second"), group.value());
    const auto third = plugin.createBookmark(QStringLiteral("Third"), QStringLiteral("https://example.com/third"), group.value());
    ASSERT_TRUE(loose.hasValue() && first.hasValue() && second.hasValue() && third.hasValue());
    ASSERT_EQ(plugin.bookmarks().size(), 4);

    const QVector<QString> before = QVector<QString>{first.value(), second.value(), third.value()};
    ASSERT_TRUE(plugin.removeBookmarkGroup(group.value()).hasValue());
    EXPECT_TRUE(plugin.bookmarkGroups().isEmpty());
    ASSERT_EQ(plugin.bookmarks().size(), 4);

    QVector<QString> after;

    for (const auto& bookmark : plugin.bookmarks()) {
        EXPECT_TRUE(bookmark.groupId.isEmpty()) << bookmark.name.toStdString();
        if (before.contains(bookmark.id)) {
            after.append(bookmark.id);
        }
    }

    EXPECT_EQ(after, before);

    // What is written back numbers the ungrouped collection from zero without a gap, which is what the next start demands of it.
    ASSERT_FALSE(host.databaseTransactions.isEmpty());
    QVector<int> written;

    for (const auto& statement : host.databaseTransactions.constLast()) {
        if (statement.statement.contains(QStringLiteral("INSERT INTO browser_bookmarks"))) {
            written.append(statement.bindings.at(2).toInt());
        }
    }

    ASSERT_EQ(written.size(), 4);

    for (int index = 0; index < written.size(); ++index) {
        EXPECT_EQ(written.at(index), index);
    }
}

TEST(BrowserPluginTest, RestoresCompleteBookmarkStateAndRejectsCorruptRows) {
    const QString created = QStringLiteral("2026-08-14T12:00:00.000Z");
    const QString updated = QStringLiteral("2026-08-14T12:01:00.000Z");
    auto host = BrowserTestsHelper::browserHost();
    // clang-format off
    host.queryHandler = [created, updated](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_tabs"))) {
            return utils::Result<persistence::DatabaseRows>::success({});
        }
        if (statement.contains(QStringLiteral("browser_bookmark_groups"))) {
            return utils::Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("work")}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("Work")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}});
        }
        if (statement.contains(QStringLiteral("browser_bookmarks"))) {
            return utils::Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("ungrouped")}, {QStringLiteral("group_id"), QVariant{}}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("SlotDeck")}, {QStringLiteral("url"), QStringLiteral("https://slotdeck.local")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}, {{QStringLiteral("id"), QStringLiteral("grouped")}, {QStringLiteral("group_id"), QStringLiteral("work")}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("Qt")}, {QStringLiteral("url"), QStringLiteral("https://qt.io")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}});
        }
        return utils::Result<persistence::DatabaseRows>::failure({"unexpected_query", "Unexpected browser query", statement});
    };
    // clang-format on
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_EQ(plugin.bookmarkGroups().size(), 1);
    ASSERT_EQ(plugin.bookmarks().size(), 2);
    EXPECT_EQ(plugin.bookmarkGroups().first().id, QStringLiteral("work"));
    EXPECT_TRUE(plugin.bookmarks().first().groupId.isEmpty());
    EXPECT_EQ(plugin.bookmarks().last().groupId, QStringLiteral("work"));
    EXPECT_EQ(plugin.bookmarks().last().updatedAtUtc.timeSpec(), Qt::UTC);

    auto corruptHost = BrowserTestsHelper::browserHost();
    // clang-format off
    corruptHost.queryHandler = [created, updated](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_tabs")) || statement.contains(QStringLiteral("browser_bookmark_groups"))) {
            return utils::Result<persistence::DatabaseRows>::success({});
        }
        return utils::Result<persistence::DatabaseRows>::success({{{QStringLiteral("id"), QStringLiteral("orphan")}, {QStringLiteral("group_id"), QStringLiteral("missing")}, {QStringLiteral("position"), 0}, {QStringLiteral("name"), QStringLiteral("Orphan")}, {QStringLiteral("url"), QStringLiteral("https://example.com")}, {QStringLiteral("created_at_utc"), created}, {QStringLiteral("updated_at_utc"), updated}}});
    };
    // clang-format on
    BrowserPlugin corruptPlugin;
    EXPECT_EQ(corruptPlugin.initialize(corruptHost).error().code, QStringLiteral("browser_bookmarks_invalid"));
}
TEST(BrowserPluginTest, RollsBackBookmarkMutationsWhenAsynchronousPersistenceFails) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    ASSERT_TRUE(plugin.createBookmarkGroup(QStringLiteral("Committed")).hasValue());
    QCoreApplication::processEvents();
    ASSERT_EQ(plugin.bookmarkGroups().size(), 1);
    host.transactionError = utils::Error{"database_failed", "Database failed", {}};

    ASSERT_TRUE(plugin.createBookmark(QStringLiteral("Transient"), QStringLiteral("https://example.com"), plugin.bookmarkGroups().first().id).hasValue());
    QCoreApplication::processEvents();
    EXPECT_TRUE(plugin.bookmarks().isEmpty());
    EXPECT_EQ(plugin.bookmarkGroups().size(), 1);
    ASSERT_EQ(host.notifications.size(), 1);
    EXPECT_EQ(host.notifications.first().severity, AlertSeverity::Error);
}
TEST(BrowserBookmarksViewTest, PresentsGroupsAndOpensTheSelectedBookmarkInEitherTarget) {
    auto host = BrowserTestsHelper::browserHost();
    BrowserPlugin plugin;
    ASSERT_TRUE(plugin.initialize(host).hasValue());
    const auto group = plugin.createBookmarkGroup(QStringLiteral("Work"));
    ASSERT_TRUE(group.hasValue());
    ASSERT_TRUE(plugin.createBookmark(QStringLiteral("Qt"), QStringLiteral("https://qt.io"), group.value()).hasValue());

    BrowserBookmarksView view(plugin);
    auto* tree = view.findChild<QTreeWidget*>(QStringLiteral("browserBookmarksTree"));
    auto* openCurrent = view.findChild<QPushButton*>(QStringLiteral("browserBookmarkOpenCurrent"));
    auto* openNew = view.findChild<QPushButton*>(QStringLiteral("browserBookmarkOpenNew"));
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(openCurrent, nullptr);
    ASSERT_NE(openNew, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 2);
    EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("Ungrouped"));
    ASSERT_EQ(tree->topLevelItem(1)->childCount(), 1);
    QTreeWidgetItem* bookmark = tree->topLevelItem(1)->child(0);

    // A glyph beside a name follows the selection of its row, otherwise it sits on the accent in a colour that reads through it.
    EXPECT_EQ(bookmark->icon(0).pixmap(32, 32).toImage(), ui::icon(ui::IconName::Bookmark, plugin.host().theme().color(ui::ThemeColor::Text)).pixmap(32, 32).toImage());
    tree->setCurrentItem(bookmark);
    QCoreApplication::processEvents();
    EXPECT_EQ(bookmark->icon(0).pixmap(32, 32).toImage(), ui::icon(ui::IconName::Bookmark, plugin.host().theme().color(ui::ThemeColor::OnAccent)).pixmap(32, 32).toImage());

    QSignalSpy opened(&view, &BrowserBookmarksView::openRequested);
    openCurrent->click();
    openNew->click();
    ASSERT_EQ(opened.count(), 2);
    EXPECT_EQ(opened.at(0).at(0).toUrl(), QUrl(QStringLiteral("https://qt.io")));
    EXPECT_FALSE(opened.at(0).at(1).toBool());
    EXPECT_TRUE(opened.at(1).at(1).toBool());
}

test::TestPluginHost BrowserTestsHelper::browserHost() {
    test::TestPluginHost host;
    host.dataPath = QDir::tempPath();
    host.translations = translations::english();
    // clang-format off
    host.queryHandler = [](const QString& statement, const QVariantList&) {
        if (statement.contains(QStringLiteral("browser_tabs"))) {
            return utils::Result<persistence::DatabaseRows>::success({});
        }
        if (statement.contains(QStringLiteral("browser_bookmark_groups")) || statement.contains(QStringLiteral("browser_bookmarks"))) {
            return utils::Result<persistence::DatabaseRows>::success({});
        }
        return utils::Result<persistence::DatabaseRows>::failure({"unexpected_query", "Unexpected browser query", statement});
    };
    // clang-format on
    return host;
}
} // namespace slotdeck::plugins::browser

TEST(BrowserTranslationsTest, SpellsEveryKeyInEveryLanguageTheSelectorOffers) {
    slotdeck::plugins::browser::BrowserPlugin plugin;
    slotdeck::test::expectCompleteCatalog(QStringLiteral("browser"), plugin.translations());
}
