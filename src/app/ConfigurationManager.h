#pragma once

#include "utils/Result.h"

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QString>

namespace slotdeck::persistence {
class DatabaseExecutor;
}

namespace slotdeck::app {

class ConfigurationManager final : public QObject {
    Q_OBJECT

  public:
    ConfigurationManager(persistence::DatabaseExecutor& databaseExecutor, QString pendingImportPath, QHash<QString, int> pluginSchemaVersions, QObject* parent = nullptr);

    [[nodiscard]] QFuture<utils::Result<void>> exportConfiguration(const QString& destinationPath);
    [[nodiscard]] QFuture<utils::Result<void>> importConfiguration(const QString& sourcePath);
    void requestRestart();

  signals:
    void restartRequested();
    void transferStateChanged(bool active);

  private:
    persistence::DatabaseExecutor& m_databaseExecutor;
    QString m_pendingImportPath;
    QHash<QString, int> m_pluginSchemaVersions;
    bool m_transferActive{false};
};

} // namespace slotdeck::app
