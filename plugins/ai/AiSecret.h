#pragma once

#include "utils/Result.h"

#include <QString>

namespace slotdeck::plugins::ai {

// A secret is either a literal value or a reference to an environment variable written as {env.NAME}.
[[nodiscard]] bool isEnvironmentReference(const QString& secret);
[[nodiscard]] QString environmentReferenceName(const QString& secret);
[[nodiscard]] utils::Result<QString> resolveSecret(const QString& secret);
[[nodiscard]] QString defaultSecretReference(const QString& variableName);

} // namespace slotdeck::plugins::ai
