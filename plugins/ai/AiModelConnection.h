#pragma once

#include "AiProviderCatalog.h"
#include "AiSecret.h"
#include "utils/Result.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace slotdeck::plugins::ai {

// A connection is one configured provider and model pair, identified by the key a task stores.
struct ModelConnection final {
    QString providerId;
    QString modelId;
    QString displayName;
    QString apiKey;
    QString address;
    QJsonObject parameters;
    // What the user declares beyond the parameters this project knows, merged over them when the request is built.
    QJsonObject extraParameters;
};

[[nodiscard]] QString connectionKey(const QString& providerId, const QString& modelId);
[[nodiscard]] QString connectionKey(const ModelConnection& connection);
[[nodiscard]] QString connectionLabel(const ModelConnection& connection);
[[nodiscard]] QString connectionAddress(const ModelConnection& connection);
[[nodiscard]] const ModelConnection* findConnection(const QVector<ModelConnection>& connections, const QString& key);
[[nodiscard]] utils::Result<QJsonObject> validateParameters(const ProviderDescriptor& provider, const QString& modelId, const QJsonObject& parameters);
[[nodiscard]] utils::Result<QJsonObject> validateExtraParameters(const QJsonObject& parameters);
[[nodiscard]] utils::Result<ModelConnection> validateConnection(const ModelConnection& connection);
[[nodiscard]] utils::Result<void> validateConnectionSet(const QVector<ModelConnection>& connections);
[[nodiscard]] ModelConnection declaredConnection(const ProviderDescriptor& provider, const QString& modelId);
[[nodiscard]] WireProtocol connectionProtocol(const ModelConnection& connection);
[[nodiscard]] qint64 outputBudget(const ModelConnection& connection);
void setOutputBudget(ModelConnection& connection, qint64 tokens);

} // namespace slotdeck::plugins::ai
