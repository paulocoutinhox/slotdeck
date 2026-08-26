#pragma once

#include "app/ApplicationSettingsStore.h"
#include "persistence/DatabaseExecutor.h"
#include "persistence/StateStore.h"
#include "plugins/PluginManager.h"

#include <QLockFile>
#include <QObject>

#include <memory>

namespace slotdeck::ui {
class MainWindow;
}

namespace slotdeck::app {

class ConfigurationManager;

class Application final : public QObject {
    Q_OBJECT

  public:
    explicit Application(QObject* parent = nullptr);
    Application(QString dataPath, QObject* parent);
    ~Application() override;

    [[nodiscard]] utils::Result<void> initialize();
    [[nodiscard]] utils::Result<void> loadInterface();
    [[nodiscard]] utils::Result<void> completeStartup();
    void shutdown();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private slots:
    void restartAfterImport();

  private:
    void applyLanguage(const QString& language);
    void applyTheme(const QString& themeId);
    void replaceProcess();
    [[nodiscard]] utils::Result<void> initializePlugins();

    plugins::PluginManager m_pluginManager;
    std::unique_ptr<QLockFile> m_instanceLock;
    std::unique_ptr<persistence::StateStore> m_stateStore;
    std::unique_ptr<persistence::DatabaseExecutor> m_databaseExecutor;
    std::unique_ptr<ApplicationSettingsStore> m_settings;
    std::unique_ptr<ConfigurationManager> m_configurationManager;
    std::unique_ptr<ui::MainWindow> m_mainWindow;
    QString m_dataPath;
    QString m_statePath;
    QString m_pendingImportPath;
    QString m_importBackupPath;
    bool m_quitEventFilterInstalled{false};
    bool m_shutdownComplete{false};
    bool m_importInProgress{false};
    bool m_recoveredFromUncleanShutdown{false};
};

} // namespace slotdeck::app
