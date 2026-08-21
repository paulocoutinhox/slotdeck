#pragma once

#include "domain/ApplicationSettings.h"
#include "utils/Result.h"

#include <QObject>

namespace slotdeck::persistence {
class DatabaseExecutor;
class StateStore;
} // namespace slotdeck::persistence

namespace slotdeck::app {

class ApplicationSettingsStore final : public QObject {
    Q_OBJECT

  public:
    explicit ApplicationSettingsStore(persistence::StateStore& stateStore, persistence::DatabaseExecutor& databaseExecutor, QObject* parent = nullptr);

    [[nodiscard]] utils::Result<void> initialize();
    [[nodiscard]] const QByteArray& windowGeometry() const;
    [[nodiscard]] const QString& language() const;
    [[nodiscard]] const QString& themeId() const;
    void setWindowGeometry(const QByteArray& geometry);
    [[nodiscard]] utils::Result<void> setLanguage(const QString& language);
    [[nodiscard]] utils::Result<void> setTheme(const QString& themeId);

  signals:
    void languageChanged(const QString& language);
    void themeChanged(const QString& themeId);
    void saveFailed(const QString& message);

  private:
    void save(domain::ApplicationSettings settings);

    persistence::StateStore& m_stateStore;
    persistence::DatabaseExecutor& m_databaseExecutor;
    domain::ApplicationSettings m_settings;
    domain::ApplicationSettings m_committedSettings;
    quint64 m_revision{0};
    quint64 m_committedRevision{0};
};

} // namespace slotdeck::app
