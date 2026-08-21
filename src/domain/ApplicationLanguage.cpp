#include "domain/ApplicationLanguage.h"

namespace slotdeck::domain {

const QStringList& supportedApplicationLanguages() {
    static const QStringList languages{QStringLiteral("en"), QStringLiteral("pt")};
    return languages;
}

bool isSupportedApplicationLanguage(const QString& language) {
    return supportedApplicationLanguages().contains(language);
}

QString resolveApplicationLanguage(QString localeName) {
    localeName.replace(QLatin1Char('_'), QLatin1Char('-'));
    localeName = localeName.toLower();
    if (isSupportedApplicationLanguage(localeName)) {
        return localeName;
    }

    const qsizetype separator = localeName.indexOf(QLatin1Char('-'));
    QString baseLanguage = separator > 0 ? localeName.first(separator) : localeName;
    if (isSupportedApplicationLanguage(baseLanguage)) {
        return baseLanguage;
    }

    return QStringLiteral("en");
}

} // namespace slotdeck::domain
