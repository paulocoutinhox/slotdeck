#include "AiModelConnection.h"

#include <QSet>
#include <QUrl>

#include <algorithm>
#include <limits>

namespace slotdeck::plugins::ai {

class AiModelConnectionHelper final {
  public:
    static bool integerValue(const QJsonValue& value, qint64& output);
    static utils::Result<void> validateParameter(const ParameterDescriptor& descriptor, const QJsonValue& value);
};

bool AiModelConnectionHelper::integerValue(const QJsonValue& value, qint64& output) {
    if (!value.isDouble()) {
        return false;
    }
    output = value.toInteger(std::numeric_limits<qint64>::min());
    return output != std::numeric_limits<qint64>::min() && value.toDouble() == static_cast<double>(output);
}

utils::Result<void> AiModelConnectionHelper::validateParameter(const ParameterDescriptor& descriptor, const QJsonValue& value) {
    const utils::Error invalid{"ai_parameter_invalid", "The provider parameter value is invalid", descriptor.id};

    if (descriptor.type == ParameterType::Enumeration) {
        if (!value.isString()) {
            return utils::Result<void>::failure(invalid);
        }
        for (const auto& option : descriptor.options) {
            if (option.id == value.toString()) {
                return utils::Result<void>::success();
            }
        }
        return utils::Result<void>::failure(invalid);
    }
    if (descriptor.type == ParameterType::Integer) {
        qint64 parsed = 0;
        if (!integerValue(value, parsed) || static_cast<double>(parsed) < descriptor.minimum || static_cast<double>(parsed) > descriptor.maximum) {
            return utils::Result<void>::failure(invalid);
        }
        return utils::Result<void>::success();
    }
    if (!value.isDouble() || value.toDouble() < descriptor.minimum || value.toDouble() > descriptor.maximum) {
        return utils::Result<void>::failure(invalid);
    }
    return utils::Result<void>::success();
}

QString connectionKey(const QString& providerId, const QString& modelId) {
    return providerId + QLatin1Char('/') + modelId;
}

QString connectionKey(const ModelConnection& connection) {
    return connectionKey(connection.providerId, connection.modelId);
}

// The display name is what the user reads, and the key is what they see when they named nothing.
QString connectionLabel(const ModelConnection& connection) {
    return connection.displayName.trimmed().isEmpty() ? connectionKey(connection) : connection.displayName.trimmed();
}

// Only a self-hosted service carries its own address, so every other connection answers with the address its provider publishes.
QString connectionAddress(const ModelConnection& connection) {
    const ProviderDescriptor* provider = findProvider(connection.providerId);
    if (provider == nullptr) {
        return {};
    }
    return provider->addressConfigurable && !connection.address.isEmpty() ? connection.address : provider->baseUrl;
}

const ModelConnection* findConnection(const QVector<ModelConnection>& connections, const QString& key) {
    for (const auto& connection : connections) {
        if (connectionKey(connection) == key) {
            return &connection;
        }
    }
    return nullptr;
}

utils::Result<QJsonObject> validateParameters(const ProviderDescriptor& provider, const QString& modelId, const QJsonObject& parameters) {
    const QVector<ParameterDescriptor> applicable = applicableParameters(provider, modelId);

    QJsonObject validated;
    for (const auto& descriptor : applicable) {
        if (!parameters.contains(descriptor.id)) {
            return utils::Result<QJsonObject>::failure({"ai_parameter_missing", "The provider parameter is missing", descriptor.id});
        }
        const auto result = AiModelConnectionHelper::validateParameter(descriptor, parameters.value(descriptor.id));
        if (!result.hasValue()) {
            return utils::Result<QJsonObject>::failure(result.error());
        }
        validated.insert(descriptor.id, parameters.value(descriptor.id));
    }

    for (auto entry = parameters.constBegin(); entry != parameters.constEnd(); ++entry) {
        if (!validated.contains(entry.key())) {
            return utils::Result<QJsonObject>::failure({"ai_parameter_unknown", "The provider parameter is not declared for this model", entry.key()});
        }
    }

    return utils::Result<QJsonObject>::success(validated);
}

// An extra parameter is whatever the service accepts and this project does not know, so only its shape is checked.
utils::Result<QJsonObject> validateExtraParameters(const QJsonObject& parameters) {
    for (auto entry = parameters.constBegin(); entry != parameters.constEnd(); ++entry) {
        if (entry.key().trimmed().isEmpty() || entry.value().isUndefined()) {
            return utils::Result<QJsonObject>::failure({"ai_extra_parameter_invalid", "The extra provider parameter is invalid", entry.key()});
        }
    }
    return utils::Result<QJsonObject>::success(parameters);
}

// Every provider exists with the configuration its own descriptor declares, and storage only holds what the user changed.
ModelConnection declaredConnection(const ProviderDescriptor& provider, const QString& modelId) {
    const QString model = modelId.isEmpty() && !provider.models.isEmpty() ? provider.models.first().id : modelId;
    ModelConnection connection;
    connection.providerId = provider.id;
    connection.modelId = model;
    connection.apiKey = defaultSecretReference(provider.apiKeyVariable);
    connection.address = provider.addressConfigurable ? provider.baseUrl : QString{};
    connection.parameters = defaultParameters(provider, model);
    return connection;
}

// A run speaks the protocol of the connection it declares, which is not necessarily the one the default connection speaks.
WireProtocol connectionProtocol(const ModelConnection& connection) {
    const ProviderDescriptor* provider = findProvider(connection.providerId);
    return provider == nullptr ? WireProtocol::OpenAiCompatible : provider->protocol;
}

// A zero budget lets the service answer with everything the model allows, so what it really is worth is that model own maximum.
qint64 outputBudget(const ModelConnection& connection) {
    const ProviderDescriptor* provider = findProvider(connection.providerId);
    if (provider == nullptr) {
        return 0;
    }

    const auto budget = outputBudgetParameter(*provider, connection.modelId);
    const qint64 declared = budget.has_value() ? connection.parameters.value(budget->id).toInteger(0) : 0;
    if (declared > 0) {
        return declared;
    }
    const ModelDescriptor* model = findModel(*provider, connection.modelId);
    return model == nullptr ? 0 : model->maximumOutputTokens;
}

// A borrowed budget still has to fit what the model accepts, so it is clamped instead of making the connection invalid.
void setOutputBudget(ModelConnection& connection, qint64 tokens) {
    const ProviderDescriptor* provider = findProvider(connection.providerId);
    if (provider == nullptr) {
        return;
    }

    const auto budget = outputBudgetParameter(*provider, connection.modelId);
    if (!budget.has_value()) {
        return;
    }
    connection.parameters.insert(budget->id, std::clamp<qint64>(tokens, static_cast<qint64>(budget->minimum), static_cast<qint64>(budget->maximum)));
}

utils::Result<ModelConnection> validateConnection(const ModelConnection& connection) {
    const ProviderDescriptor* provider = findProvider(connection.providerId);
    if (provider == nullptr) {
        return utils::Result<ModelConnection>::failure({"ai_provider_unknown", "The selected AI provider is not supported", connection.providerId});
    }

    ModelConnection validated = connection;
    validated.modelId = connection.modelId.trimmed();
    validated.displayName = connection.displayName.trimmed();
    validated.address = connection.address.trimmed();
    if (validated.modelId.isEmpty()) {
        return utils::Result<ModelConnection>::failure({"ai_model_invalid", "The AI model is required", provider->id});
    }
    if (provider->requiresApiKey && validated.apiKey.isEmpty()) {
        return utils::Result<ModelConnection>::failure({"ai_api_key_missing", "The provider requires an API key", provider->id});
    }
    if (!provider->addressConfigurable && !validated.address.isEmpty()) {
        return utils::Result<ModelConnection>::failure({"ai_address_not_configurable", "The provider publishes its own address", provider->id});
    }

    const QUrl address(validated.address);
    if (provider->addressConfigurable && (validated.address.isEmpty() || !address.isValid() || (address.scheme() != QStringLiteral("http") && address.scheme() != QStringLiteral("https")))) {
        return utils::Result<ModelConnection>::failure({"ai_address_invalid", "The service address is invalid", provider->id});
    }

    const auto parameters = validateParameters(*provider, validated.modelId, validated.parameters);
    if (!parameters.hasValue()) {
        return utils::Result<ModelConnection>::failure(parameters.error());
    }
    // Asking for the maximum of a model the catalog does not declare has no answer, so it is refused where it is typed.
    const auto budget = outputBudgetParameter(*provider, validated.modelId);
    if (budget.has_value() && budget->modelMaximumWhenZero && validated.parameters.value(budget->id).toInteger(0) == 0 && findModel(*provider, validated.modelId) == nullptr) {
        return utils::Result<ModelConnection>::failure({"ai_output_budget_unknown", "The catalog does not declare this model, so its answer budget has to be a number", validated.modelId});
    }
    const auto extra = validateExtraParameters(validated.extraParameters);
    if (!extra.hasValue()) {
        return utils::Result<ModelConnection>::failure(extra.error());
    }

    validated.parameters = parameters.value();
    validated.extraParameters = extra.value();
    return utils::Result<ModelConnection>::success(validated);
}

// One key names one configuration, so the same provider and model pair is configured once.
utils::Result<void> validateConnectionSet(const QVector<ModelConnection>& connections) {
    QSet<QString> keys;
    for (const auto& connection : connections) {
        const auto validated = validateConnection(connection);
        if (!validated.hasValue()) {
            return utils::Result<void>::failure(validated.error());
        }
        const QString key = connectionKey(validated.value());
        if (keys.contains(key)) {
            return utils::Result<void>::failure({"ai_connection_duplicate", "The provider and model pair is already configured", key});
        }
        keys.insert(key);
    }
    return utils::Result<void>::success();
}

} // namespace slotdeck::plugins::ai
