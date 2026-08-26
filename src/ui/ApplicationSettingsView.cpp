#include "ui/ApplicationSettingsView.h"

#include "app/ApplicationSettingsStore.h"
#include "app/ConfigurationManager.h"
#include "domain/ApplicationLanguage.h"
#include "persistence/CoreDatabaseSchema.h"
#include "ui/Components.h"
#include "ui/ConfirmationDialog.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace slotdeck::ui {

class ApplicationSettingsFactory final {
  public:
    ApplicationSettingsFactory(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager) : m_pluginManager(pluginManager), m_settings(settings), m_configurationManager(configurationManager) {}

    QWidget* operator()(const QString& groupId, const QString& sectionId, QWidget* parent) const {
        if (groupId != QStringLiteral("application") || (sectionId != QStringLiteral("general") && sectionId != QStringLiteral("configuration"))) {
            return nullptr;
        }

        return new ApplicationSettingsView(m_pluginManager, m_settings, m_configurationManager, sectionId, parent);
    }

  private:
    plugins::PluginManager& m_pluginManager;
    app::ApplicationSettingsStore& m_settings;
    app::ConfigurationManager& m_configurationManager;
};

class ApplicationSettingsViewHelper final {
  public:
    static QString languageTitleKey(const QString& language);
};

QString ApplicationSettingsViewHelper::languageTitleKey(const QString& language) {
    if (language == QStringLiteral("en")) {
        return QStringLiteral("slotdeck.application.english");
    }
    if (language == QStringLiteral("pt")) {
        return QStringLiteral("slotdeck.application.portuguese");
    }

    Q_UNREACHABLE_RETURN({});
}

ApplicationSettingsView::ApplicationSettingsView(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager, QString sectionId, QWidget* parent) : QWidget(parent), m_settings(settings), m_configurationManager(configurationManager), m_pluginManager(pluginManager) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    if (sectionId == QStringLiteral("general")) {
        auto* form = settingsForm();
        m_language = new ui::ComboBox(pluginManager.theme(), this);
        m_language->setObjectName(QStringLiteral("applicationLanguage"));
        for (const auto& language : domain::supportedApplicationLanguages()) {
            m_language->addItem(pluginManager.translate(ApplicationSettingsViewHelper::languageTitleKey(language)), language);
        }
        sortComboBoxItems(m_language);
        m_language->setCurrentIndex(std::max(0, m_language->findData(m_settings.language())));
        addSettingsRow(form, pluginManager.translate(QStringLiteral("slotdeck.application.language")), m_language);
        m_theme = new ui::ComboBox(pluginManager.theme(), this);
        m_theme->setObjectName(QStringLiteral("applicationTheme"));
        for (const auto& theme : ui::themeManager().catalog().themes()) {
            m_theme->addItem(pluginManager.translate(theme->titleKey()), theme->id());
        }
        sortComboBoxItems(m_theme);
        m_theme->setCurrentIndex(std::max(0, m_theme->findData(m_settings.themeId())));
        addSettingsRow(form, pluginManager.translate(QStringLiteral("slotdeck.application.theme")), m_theme);
        auto* version = new QLabel(QCoreApplication::applicationVersion(), this);
        version->setObjectName(QStringLiteral("applicationVersion"));
        addSettingsRow(form, pluginManager.translate(QStringLiteral("slotdeck.application.version")), version);
        layout->addLayout(form);
        layout->addStretch(1);

        connect(m_language, &QComboBox::currentIndexChanged, this, &ApplicationSettingsView::selectLanguage);
        connect(m_theme, &QComboBox::currentIndexChanged, this, &ApplicationSettingsView::selectTheme);
        return;
    }

    auto* actionRow = ui::settingsActionRow(this);
    auto* actions = qobject_cast<QHBoxLayout*>(actionRow->layout());
    auto* importButton = new QPushButton(ui::icon(ui::IconName::Import, pluginManager.theme()), pluginManager.translate(QStringLiteral("slotdeck.configuration.import")), actionRow);
    importButton->setObjectName(QStringLiteral("importConfiguration"));
    auto* exportButton = new QPushButton(ui::icon(ui::IconName::Export, pluginManager.theme()), pluginManager.translate(QStringLiteral("slotdeck.configuration.export")), actionRow);
    exportButton->setObjectName(QStringLiteral("exportConfiguration"));
    actions->addWidget(importButton);
    actions->addWidget(exportButton);
    actions->addStretch(1);
    layout->addWidget(actionRow);
    layout->addStretch(1);

    connect(importButton, &QPushButton::clicked, this, &ApplicationSettingsView::importConfiguration);
    connect(exportButton, &QPushButton::clicked, this, &ApplicationSettingsView::exportConfiguration);
    // clang-format off
    connect(&m_configurationManager, &app::ConfigurationManager::transferStateChanged, this, [importButton, exportButton](bool active) {
        importButton->setEnabled(!active);
        exportButton->setEnabled(!active);
    });
    // clang-format on
}

void ApplicationSettingsView::selectLanguage(int index) {
    if (index < 0 || index >= m_language->count()) {
        return;
    }

    const auto result = m_settings.setLanguage(m_language->itemData(index).toString());

    if (!result.hasValue()) {
        m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("slotdeck.application.title")), m_pluginManager.translate(QStringLiteral("slotdeck.application.language-save-error")), plugins::AlertSeverity::Error);
    }
}

void ApplicationSettingsView::selectTheme(int index) {
    if (index < 0 || index >= m_theme->count()) {
        return;
    }

    const auto result = m_settings.setTheme(m_theme->itemData(index).toString());

    if (!result.hasValue()) {
        m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("slotdeck.application.title")), m_pluginManager.translate(QStringLiteral("slotdeck.application.theme-save-error")), plugins::AlertSeverity::Error);
    }
}

void ApplicationSettingsView::exportConfiguration() {
    const QString path = QFileDialog::getSaveFileName(this, m_pluginManager.translate(QStringLiteral("slotdeck.configuration.export-title")), QStringLiteral("slotdeck.sqlite3"), m_pluginManager.translate(QStringLiteral("slotdeck.configuration.file-filter")));

    if (path.isEmpty()) {
        return;
    }

    auto future = m_configurationManager.exportConfiguration(path);
    // clang-format off
    future.then(this, [this](utils::Result<void> result) {
        m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("slotdeck.configuration.title")), result.hasValue() ? m_pluginManager.translate(QStringLiteral("slotdeck.configuration.export-success")) : result.error().message, result.hasValue() ? plugins::AlertSeverity::Success : plugins::AlertSeverity::Error);
    });
    // clang-format on
}

void ApplicationSettingsView::importConfiguration() {
    const QString path = QFileDialog::getOpenFileName(this, m_pluginManager.translate(QStringLiteral("slotdeck.configuration.import-title")), {}, m_pluginManager.translate(QStringLiteral("slotdeck.configuration.file-filter")));

    if (path.isEmpty()) {
        return;
    }

    const bool confirmed = ConfirmationDialog::confirm(this, m_pluginManager.translate(QStringLiteral("slotdeck.window.title")), m_pluginManager.translate(QStringLiteral("slotdeck.configuration.confirm-title")), m_pluginManager.translate(QStringLiteral("slotdeck.configuration.confirm-message")), m_pluginManager.translate(QStringLiteral("slotdeck.configuration.confirm-detail")), m_pluginManager.translate(QStringLiteral("slotdeck.configuration.import")), true);

    if (!confirmed) {
        return;
    }

    auto future = m_configurationManager.importConfiguration(path);
    // clang-format off
    future.then(this, [this](utils::Result<void> result) {
        if (!result.hasValue()) {
            m_pluginManager.notify(m_pluginManager.translate(QStringLiteral("slotdeck.configuration.title")), m_pluginManager.translate(QStringLiteral("slotdeck.configuration.import-error")), plugins::AlertSeverity::Error);
            return;
        }
        m_configurationManager.requestRestart();
    });
    // clang-format on
}

QVector<CoreSettingsContribution> applicationSettingsContributions(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager) {
    const plugins::SettingsSection general{QStringLiteral("general"), QStringLiteral("slotdeck.application.general"), {QStringLiteral("slotdeck.application.language"), QStringLiteral("slotdeck.application.theme"), QStringLiteral("slotdeck.application.version"), QStringLiteral("slotdeck.application.english"), QStringLiteral("slotdeck.application.portuguese"), QStringLiteral("slotdeck.application.theme-green"), QStringLiteral("slotdeck.application.theme-blue"), QStringLiteral("slotdeck.application.theme-red")}};
    const plugins::SettingsSection configuration{QStringLiteral("configuration"), QStringLiteral("slotdeck.configuration.title"), {QStringLiteral("slotdeck.configuration.import"), QStringLiteral("slotdeck.configuration.export")}};
    const plugins::SettingsGroup application{QStringLiteral("application"), QStringLiteral("slotdeck.application.title"), {general, configuration}};
    return {{application, ApplicationSettingsFactory(pluginManager, settings, configurationManager)}};
}

} // namespace slotdeck::ui
