#pragma once

#include <QString>
#include <QStringList>

namespace slotdeck::domain {

[[nodiscard]] const QStringList& supportedApplicationLanguages();
[[nodiscard]] bool isSupportedApplicationLanguage(const QString& language);
[[nodiscard]] QString resolveApplicationLanguage(QString localeName);

} // namespace slotdeck::domain
