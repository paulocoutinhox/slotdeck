#pragma once

#include "ui/SettingsView.h"

class QComboBox;
class QSpinBox;

namespace slotdeck::app {
class ApplicationSettingsStore;
class ConfigurationManager;
} // namespace slotdeck::app

namespace slotdeck::ui {

class ApplicationSettingsView final : public QWidget {
    Q_OBJECT

  public:
    ApplicationSettingsView(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager, QString sectionId, QWidget* parent = nullptr);

  private slots:
    void selectLanguage(int index);
    void selectTheme(int index);
    void exportConfiguration();
    void importConfiguration();

  private:
    app::ApplicationSettingsStore& m_settings;
    app::ConfigurationManager& m_configurationManager;
    plugins::PluginManager& m_pluginManager;
    QComboBox* m_language{nullptr};
    QComboBox* m_theme{nullptr};
};

[[nodiscard]] QVector<CoreSettingsContribution> applicationSettingsContributions(plugins::PluginManager& pluginManager, app::ApplicationSettingsStore& settings, app::ConfigurationManager& configurationManager);

} // namespace slotdeck::ui
