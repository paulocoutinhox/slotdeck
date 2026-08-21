#include "AiSecret.h"

#include <QRegularExpression>

namespace slotdeck::plugins::ai {

class AiSecretHelper final {
  public:
    static const QRegularExpression& environmentReferencePattern();
};

const QRegularExpression& AiSecretHelper::environmentReferencePattern() {
    static const QRegularExpression pattern(QStringLiteral("^\\{env\\.([A-Za-z_][A-Za-z0-9_]*)\\}$"));
    return pattern;
}

bool isEnvironmentReference(const QString& secret) {
    return AiSecretHelper::environmentReferencePattern().match(secret).hasMatch();
}

QString environmentReferenceName(const QString& secret) {
    const auto match = AiSecretHelper::environmentReferencePattern().match(secret);
    return match.hasMatch() ? match.captured(1) : QString{};
}

utils::Result<QString> resolveSecret(const QString& secret) {
    if (!isEnvironmentReference(secret)) {
        return utils::Result<QString>::success(secret);
    }

    const QString name = environmentReferenceName(secret);
    const QString value = qEnvironmentVariable(name.toLatin1().constData());
    if (value.isEmpty()) {
        return utils::Result<QString>::failure({"ai_secret_environment_missing", "The referenced environment variable is not set", name});
    }
    return utils::Result<QString>::success(value);
}

// A credential normally lives in the environment, so a form starts from the reference the service officially documents.
QString defaultSecretReference(const QString& variableName) {
    return variableName.isEmpty() ? QString{} : QStringLiteral("{env.%1}").arg(variableName);
}

} // namespace slotdeck::plugins::ai
