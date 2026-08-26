#include "ui/MainWindow.h"

#include "app/ApplicationSettingsStore.h"
#include "persistence/CoreDatabaseSchema.h"
#include "ui/AppStyle.h"
#include "ui/ApplicationShortcuts.h"
#include "ui/Components.h"
#include "ui/ConfirmationDialog.h"
#include "ui/Icons.h"
#include "ui/ModeBar.h"
#include "ui/SettingsView.h"
#include "ui/Theme.h"
#include "ui/ToastOverlay.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace slotdeck::ui {

class MainWindowHelper final {
  public:
    static QString modeId(const QString& pluginId, const QString& itemId);
};

QString MainWindowHelper::modeId(const QString& pluginId, const QString& itemId) {
    return pluginId + QLatin1Char('/') + itemId;
}

MainWindow::MainWindow(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, QVector<CoreSettingsContribution> coreSettings, QWidget* parent) : QMainWindow(parent), m_pluginManager(pluginManager), m_settings(settings), m_coreSettings(std::move(coreSettings)) {
    m_windowGeometryTimer.setSingleShot(true);
    m_windowGeometryTimer.setInterval(200);
    connect(&m_windowGeometryTimer, &QTimer::timeout, this, &MainWindow::persistWindowGeometry);

    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(m_pluginManager.translate(QStringLiteral("slotdeck.window.title")));
    setMinimumSize(820, 520);
    resize(1360, 840);
    setStyleSheet(applicationStyleSheet(m_pluginManager.theme()) + m_pluginManager.styleSheet());
    m_toasts = new ToastOverlay(m_pluginManager.theme(), this);
    createActions();
    showLoadingPage();
    if (!m_settings.windowGeometry().isEmpty()) {
        restoreGeometry(m_settings.windowGeometry());
    }
}

// Everything the reader waits for is behind this, so the window is already on screen when it runs.
utils::Result<void> MainWindow::buildInterface() {
    auto* loading = takeCentralWidget();
    const auto result = createInterface();
    if (!result.hasValue()) {
        setCentralWidget(loading);
        return result;
    }

    loading->deleteLater();
    m_ready = true;
    m_toasts->raise();
    connect(&m_pluginManager, &plugins::PluginManager::notificationRequested, this, &MainWindow::showNotification);
    // clang-format off
    connect(&m_settings, &app::ApplicationSettingsStore::saveFailed, this, [this](const QString& message) { showNotification(m_pluginManager.translate(QStringLiteral("slotdeck.window.title")), message, plugins::AlertSeverity::Error); });
    // clang-format on
    return result;
}

bool MainWindow::ready() const {
    return m_ready;
}

// A window that is still loading says so in the middle of itself, because an empty frame reads as a product that failed to open.
void MainWindow::showLoadingPage() {
    auto* loading = new QWidget(this);
    loading->setObjectName(QStringLiteral("startupLoading"));
    auto* layout = new QVBoxLayout(loading);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(m_pluginManager.theme().metric(ThemeMetric::ControlVerticalPadding));
    auto* indicator = new BusyIndicator(m_pluginManager.theme(), loading);
    indicator->setObjectName(QStringLiteral("startupIndicator"));
    auto* caption = new QLabel(m_pluginManager.translate(QStringLiteral("slotdeck.window.loading")), loading);
    caption->setObjectName(QStringLiteral("startupCaption"));
    caption->setAlignment(Qt::AlignCenter);
    layout->addWidget(indicator, 0, Qt::AlignHCenter);
    layout->addWidget(caption, 0, Qt::AlignHCenter);
    setCentralWidget(loading);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // A window that never finished loading has nothing to keep and nothing to confirm.
    if (!m_ready) {
        event->accept();
        return;
    }

    const bool confirmed = ConfirmationDialog::confirm(this, m_pluginManager.translate(QStringLiteral("slotdeck.window.title")), m_pluginManager.translate(QStringLiteral("slotdeck.exit.title")), m_pluginManager.translate(QStringLiteral("slotdeck.exit.message")), m_pluginManager.translate(QStringLiteral("slotdeck.actions.cancel")), m_pluginManager.translate(QStringLiteral("slotdeck.exit.action")), true);
    if (!confirmed) {
        event->ignore();
        return;
    }

    persistWindowGeometry();
    event->accept();
}

void MainWindow::requestApplicationQuit() {
    QApplication::closeAllWindows();
}

void MainWindow::reloadTranslations() {
    const QString preferredModeId = m_currentModeId;
    auto* previousInterface = takeCentralWidget();
    auto* previousModeBar = m_modeBar;
    auto* previousContentStack = m_contentStack;
    const auto previousViews = m_views;
    m_views.clear();
    resetInterfacePointers();

    setWindowTitle(m_pluginManager.translate(QStringLiteral("slotdeck.window.title")));
    m_quitAction->setText(m_pluginManager.translate(QStringLiteral("slotdeck.exit.action")));
    const auto result = createInterface(preferredModeId);
    if (result.hasValue()) {
        previousInterface->deleteLater();
        m_toasts->raise();
        return;
    }

    // The interface that was showing is kept, because a language nobody could build is not worth an empty window.
    m_modeBar = previousModeBar;
    m_contentStack = previousContentStack;
    m_views = previousViews;
    setCentralWidget(previousInterface);
    showNotification(m_pluginManager.translate(QStringLiteral("slotdeck.window.title")), result.error().message, plugins::AlertSeverity::Error);
}

void MainWindow::reloadTheme() {
    setStyleSheet(applicationStyleSheet(m_pluginManager.theme()) + m_pluginManager.styleSheet());
    m_toasts->applyTheme(m_pluginManager.theme());
    reloadTranslations();
}

void MainWindow::moveEvent(QMoveEvent* event) {
    QMainWindow::moveEvent(event);
    scheduleWindowGeometrySave();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    scheduleWindowGeometrySave();
}

void MainWindow::selectMode(const QString& selectedModeId) {
    auto* view = m_views.value(selectedModeId);
    if (view == nullptr) {
        return;
    }
    m_contentStack->setCurrentWidget(view);
    m_modeBar->setCurrentMode(selectedModeId);
    m_currentModeId = selectedModeId;
}

void MainWindow::showNotification(const QString& title, const QString& message, plugins::AlertSeverity severity) {
    m_toasts->showNotification(title, message, severity);
}

void MainWindow::persistWindowGeometry() {
    m_windowGeometryTimer.stop();
    m_settings.setWindowGeometry(saveGeometry());
}

void MainWindow::createActions() {
    m_quitAction = new QAction(m_pluginManager.translate(QStringLiteral("slotdeck.exit.action")), this);
    m_quitAction->setObjectName(QStringLiteral("applicationQuitAction"));
    m_quitAction->setShortcut(shortcuts::quit());
    m_quitAction->setShortcutContext(Qt::ApplicationShortcut);
    m_quitAction->setMenuRole(QAction::QuitRole);
    addAction(m_quitAction);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::requestApplicationQuit);

    const QVector<QPair<QKeySequence, QString>> fontActions{{shortcuts::increaseContentFont(), QStringLiteral("increase")}, {shortcuts::decreaseContentFont(), QStringLiteral("decrease")}, {shortcuts::resetContentFont(), QStringLiteral("reset")}};
    for (const auto& fontAction : fontActions) {
        auto* action = new QAction(this);
        action->setShortcut(fontAction.first);
        action->setShortcutContext(Qt::ApplicationShortcut);
        addAction(action);
        // clang-format off
        connect(action, &QAction::triggered, this, [this, step = fontAction.second]() { m_pluginManager.publishCoreEvent(QString::fromLatin1(plugins::contentFontStepTopic), {{QStringLiteral("step"), step}}); });
        // clang-format on
    }
}

utils::Result<void> MainWindow::createInterface(const QString& preferredModeId) {
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_modeBar = new ModeBar(central);
    root->addWidget(m_modeBar);
    root->addWidget(verticalDivider(central));

    m_contentStack = new QStackedWidget(central);
    root->addWidget(m_contentStack, 1);

    QString initialMode;
    for (const auto& contribution : m_pluginManager.navigationItems()) {
        const QString id = MainWindowHelper::modeId(contribution.pluginId, contribution.item.id);
        auto* view = m_pluginManager.createNavigationView(contribution.pluginId, contribution.item.id, m_contentStack);
        if (view == nullptr) {
            delete central;
            resetInterfacePointers();
            m_views.clear();
            return utils::Result<void>::failure({"plugin_navigation_view_failed", "A plugin navigation view could not be created", id});
        }
        m_contentStack->addWidget(view);
        m_views.insert(id, view);
        m_modeBar->addMode(id, contribution.item.icon, m_pluginManager.translate(contribution.item.titleKey), contribution.item.placement);
        if (initialMode.isEmpty() && contribution.item.placement == plugins::NavigationPlacement::Primary) {
            initialMode = id;
        }
    }

    const QString settingsMode = QStringLiteral("slotdeck/settings");
    auto* settingsView = new SettingsView(m_pluginManager, m_coreSettings, m_contentStack);
    if (!settingsView->isValid()) {
        delete central;
        resetInterfacePointers();
        m_views.clear();
        return utils::Result<void>::failure({"plugin_settings_view_failed", "The settings interface could not be created", {}});
    }
    m_contentStack->addWidget(settingsView);
    m_views.insert(settingsMode, settingsView);
    m_modeBar->addMode(settingsMode, icon(IconName::Settings, m_pluginManager.theme()), m_pluginManager.translate(QStringLiteral("slotdeck.settings.title")), plugins::NavigationPlacement::Secondary);

    connect(m_modeBar, &ModeBar::modeRequested, this, &MainWindow::selectMode);
    // clang-format off
    connect(&m_pluginManager, &plugins::PluginManager::navigationRequested, this, [this](const QString& pluginId, const QString& navigationId) { selectMode(MainWindowHelper::modeId(pluginId, navigationId)); });
    // clang-format on
    if (initialMode.isEmpty()) {
        delete central;
        resetInterfacePointers();
        m_views.clear();
        return utils::Result<void>::failure({"plugin_navigation_primary_missing", "A primary plugin navigation view is required", {}});
    }
    setCentralWidget(central);
    m_toasts->raise();
    selectMode(m_views.contains(preferredModeId) ? preferredModeId : initialMode);
    return utils::Result<void>::success();
}

void MainWindow::resetInterfacePointers() {
    m_modeBar = nullptr;
    m_contentStack = nullptr;
}

void MainWindow::scheduleWindowGeometrySave() {
    m_windowGeometryTimer.start();
}

} // namespace slotdeck::ui
