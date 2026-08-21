#pragma once

#include "plugins/PluginInterface.h"
#include "utils/Result.h"

#include <QString>

namespace slotdeck::plugins {

class LocalizationService final {
  public:
    explicit LocalizationService(QString localeName);

    [[nodiscard]] utils::Result<void> registerCatalog(const QString& pluginId, const TranslationCatalog& catalog);
    void unregisterCatalog(const QString& pluginId);
    [[nodiscard]] utils::Result<void> setLocale(QString localeName);
    [[nodiscard]] QString translate(const QString& key) const;
    [[nodiscard]] const QString& localeName() const;

  private:
    [[nodiscard]] QStringList localeCandidates() const;

    QString m_localeName;
    QHash<QString, TranslationCatalog> m_catalogs;
};

} // namespace slotdeck::plugins
