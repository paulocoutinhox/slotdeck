#include "AiProviderCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <optional>

namespace slotdeck::plugins::ai {

// The catalog is the only source of what a provider is, so every value below arrives from the files and never from this code.
struct LoadedAiCatalog final {
    AiCatalog catalog;
    utils::Result<void> error = utils::Result<void>::success();
};

class AiProviderCatalogHelper final {
  public:
    static utils::Error invalid(const QString& message, const QString& detail);
    static utils::Result<QJsonObject> parseDocument(const QByteArray& contents);
    static QByteArray resourceContents(const QString& path);
    static std::optional<ModelTrait> traitFromIdentifier(const QString& identifier);
    static std::optional<WireProtocol> protocolFromIdentifier(const QString& identifier);
    static utils::Result<CommandLineDescriptor> commandLineDescriptor(const QJsonObject& document, const QString& providerId);
    static std::optional<ParameterType> parameterTypeFromIdentifier(const QString& identifier);
    static utils::Result<QSet<ModelTrait>> traitSet(const QJsonValue& value, const QString& detail);
    static utils::Result<std::optional<double>> readModelCost(const QJsonObject& entry, const QString& key, const QString& modelId);
    static utils::Result<QStringList> stringList(const QJsonValue& value, const QString& detail);
    static utils::Result<QMap<QString, QString>> stringMap(const QJsonValue& value, const QString& detail);
    static utils::Result<QVector<ParameterOption>> parameterOptions(const QJsonValue& value, const QString& detail);
    static utils::Result<void> boundedNumber(const QJsonObject& document, ParameterDescriptor& descriptor, const QString& detail);
    static utils::Result<ParameterDescriptor> parameter(const QJsonObject& document, const QString& providerId);
    static utils::Result<QVector<ParameterDescriptor>> parameterSet(const QJsonValue& value, const QString& providerId);
    static utils::Result<ProviderDescriptor> provider(const QJsonObject& document);
    static utils::Result<AiLimits> limits(const QJsonObject& document);
    static utils::Result<QVector<ModelDescriptor>> importedModels(const QJsonArray& entries, const QString& providerId, bool reachedOverAWire);
    static utils::Result<void> mergeImportedModels(QVector<ProviderDescriptor>& providers, const QByteArray& modelsDocument);
    static LoadedAiCatalog load();
    static const LoadedAiCatalog& loadedCatalog();
};

utils::Error AiProviderCatalogHelper::invalid(const QString& message, const QString& detail) {
    return {"ai_catalog_invalid", message, detail};
}

utils::Result<QJsonObject> AiProviderCatalogHelper::parseDocument(const QByteArray& contents) {
    if (contents.isEmpty()) {
        return utils::Result<QJsonObject>::failure(invalid(QStringLiteral("The AI catalog file is unavailable"), {}));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return utils::Result<QJsonObject>::failure(invalid(QStringLiteral("The AI catalog file is not a catalog"), parseError.errorString()));
    }

    return utils::Result<QJsonObject>::success(document.object());
}

QByteArray AiProviderCatalogHelper::resourceContents(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

std::optional<ModelTrait> AiProviderCatalogHelper::traitFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("sampling")) {
        return ModelTrait::Sampling;
    }
    if (identifier == QStringLiteral("reasoning")) {
        return ModelTrait::Reasoning;
    }
    if (identifier == QStringLiteral("function-calling")) {
        return ModelTrait::FunctionCalling;
    }
    if (identifier == QStringLiteral("vision")) {
        return ModelTrait::Vision;
    }
    if (identifier == QStringLiteral("system-prompt")) {
        return ModelTrait::SystemPrompt;
    }

    return std::nullopt;
}

QString modelTraitIdentifier(ModelTrait trait) {
    switch (trait) {
    case ModelTrait::Sampling:
        return QStringLiteral("sampling");
    case ModelTrait::Reasoning:
        return QStringLiteral("reasoning");
    case ModelTrait::FunctionCalling:
        return QStringLiteral("function-calling");
    case ModelTrait::Vision:
        return QStringLiteral("vision");
    case ModelTrait::SystemPrompt:
        return QStringLiteral("system-prompt");
    }

    Q_UNREACHABLE_RETURN(QStringLiteral("sampling"));
}

std::optional<WireProtocol> AiProviderCatalogHelper::protocolFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("anthropic")) {
        return WireProtocol::Anthropic;
    }
    if (identifier == QStringLiteral("openai-compatible")) {
        return WireProtocol::OpenAiCompatible;
    }
    if (identifier == QStringLiteral("command-line")) {
        return WireProtocol::CommandLine;
    }

    return std::nullopt;
}

// The prompt and the working directory are declared where they go, so neither is spliced into a string a shell would read.
utils::Result<CommandLineDescriptor> AiProviderCatalogHelper::commandLineDescriptor(const QJsonObject& document, const QString& providerId) {
    const QJsonObject declared = document.value(QStringLiteral("command")).toObject();

    if (!document.value(QStringLiteral("command")).isObject() || !hasExactKeys(declared, {QStringLiteral("program"), QStringLiteral("arguments"), QStringLiteral("clearedVariables")})) {
        return utils::Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares no program to run"), providerId));
    }

    CommandLineDescriptor commandLine;

    if (!readSettingsText(declared, QStringLiteral("program"), commandLine.program) || commandLine.program.isEmpty() || !declared.value(QStringLiteral("arguments")).isArray()) {
        return utils::Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares no program to run"), providerId));
    }

    for (const auto& value : declared.value(QStringLiteral("arguments")).toArray()) {
        if (!value.isString() || value.toString().isEmpty()) {
            return utils::Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares an empty argument"), providerId));
        }

        commandLine.arguments.append(value.toString());
    }

    if (!declared.value(QStringLiteral("clearedVariables")).isArray()) {
        return utils::Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares no variables to clear"), providerId));
    }

    for (const auto& value : declared.value(QStringLiteral("clearedVariables")).toArray()) {
        if (!value.isString() || value.toString().trimmed().isEmpty()) {
            return utils::Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider declares an empty variable to clear"), providerId));
        }

        commandLine.clearedVariables.append(value.toString());
    }

    if (!commandLine.arguments.contains(commandLinePromptPlaceholder)) {
        return utils::Result<CommandLineDescriptor>::failure(invalid(QStringLiteral("A command line provider never passes the prompt"), providerId));
    }

    return utils::Result<CommandLineDescriptor>::success(commandLine);
}

std::optional<ParameterType> AiProviderCatalogHelper::parameterTypeFromIdentifier(const QString& identifier) {
    if (identifier == QStringLiteral("integer")) {
        return ParameterType::Integer;
    }
    if (identifier == QStringLiteral("number")) {
        return ParameterType::Number;
    }
    if (identifier == QStringLiteral("enumeration")) {
        return ParameterType::Enumeration;
    }

    return std::nullopt;
}

// A price nobody published is absent rather than free, so a model without one reports no cost at all.
utils::Result<std::optional<double>> AiProviderCatalogHelper::readModelCost(const QJsonObject& entry, const QString& key, const QString& modelId) {
    if (!entry.contains(key)) {
        return utils::Result<std::optional<double>>::success(std::nullopt);
    }

    const QJsonValue value = entry.value(key);

    if (!value.isDouble() || value.toDouble() < 0.0) {
        return utils::Result<std::optional<double>>::failure(invalid(QStringLiteral("A catalog model carries an invalid price"), modelId));
    }

    return utils::Result<std::optional<double>>::success(value.toDouble());
}

utils::Result<QSet<ModelTrait>> AiProviderCatalogHelper::traitSet(const QJsonValue& value, const QString& detail) {
    if (value.isUndefined()) {
        return utils::Result<QSet<ModelTrait>>::success({});
    }
    if (!value.isArray()) {
        return utils::Result<QSet<ModelTrait>>::failure(invalid(QStringLiteral("The declared trait set is not a list"), detail));
    }

    QSet<ModelTrait> traits;

    for (const auto& declared : value.toArray()) {
        const auto trait = traitFromIdentifier(declared.toString());
        if (!trait.has_value()) {
            return utils::Result<QSet<ModelTrait>>::failure(invalid(QStringLiteral("The declared trait is unknown"), detail));
        }
        traits.insert(*trait);
    }
    // A parameter set built for a sampling model excludes the one built for a reasoning model, so no model carries both.
    if (traits.contains(ModelTrait::Sampling) && traits.contains(ModelTrait::Reasoning)) {
        return utils::Result<QSet<ModelTrait>>::failure(invalid(QStringLiteral("The declared traits combine sampling and reasoning"), detail));
    }

    return utils::Result<QSet<ModelTrait>>::success(traits);
}

utils::Result<QStringList> AiProviderCatalogHelper::stringList(const QJsonValue& value, const QString& detail) {
    if (!value.isArray()) {
        return utils::Result<QStringList>::failure(invalid(QStringLiteral("The declared list is not a list"), detail));
    }

    QStringList values;

    for (const auto& entry : value.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty() || values.contains(entry.toString())) {
            return utils::Result<QStringList>::failure(invalid(QStringLiteral("The declared list carries an invalid entry"), detail));
        }
        values.append(entry.toString());
    }

    return utils::Result<QStringList>::success(values);
}

// A field a provider does not carry is the declared default, so a command line agent declares only what applies to it.
utils::Result<QMap<QString, QString>> AiProviderCatalogHelper::stringMap(const QJsonValue& value, const QString& detail) {
    if (value.isUndefined()) {
        return utils::Result<QMap<QString, QString>>::success({});
    }
    if (!value.isObject()) {
        return utils::Result<QMap<QString, QString>>::failure(invalid(QStringLiteral("The declared map is not a map"), detail));
    }

    QMap<QString, QString> values;
    const QJsonObject document = value.toObject();

    for (auto entry = document.constBegin(); entry != document.constEnd(); ++entry) {
        if (entry.key().isEmpty() || !entry.value().isString()) {
            return utils::Result<QMap<QString, QString>>::failure(invalid(QStringLiteral("The declared map carries an invalid entry"), detail));
        }
        values.insert(entry.key(), entry.value().toString());
    }

    return utils::Result<QMap<QString, QString>>::success(values);
}

utils::Result<QVector<ParameterOption>> AiProviderCatalogHelper::parameterOptions(const QJsonValue& value, const QString& detail) {
    if (!value.isArray() || value.toArray().isEmpty()) {
        return utils::Result<QVector<ParameterOption>>::failure(invalid(QStringLiteral("The enumeration declares no option"), detail));
    }

    QVector<ParameterOption> options;

    for (const auto& entry : value.toArray()) {
        const QJsonObject document = entry.toObject();
        ParameterOption option;
        const bool typed = entry.isObject() && hasExactKeys(document, {QStringLiteral("id"), QStringLiteral("title")}) && readSettingsText(document, QStringLiteral("id"), option.id) && readSettingsText(document, QStringLiteral("title"), option.titleKey);
        // clang-format off
        const auto duplicate = std::find_if(options.cbegin(), options.cend(), [&option](const ParameterOption& existing) { return existing.id == option.id; });
        // clang-format on
        if (!typed || option.id.isEmpty() || option.titleKey.isEmpty() || duplicate != options.cend()) {
            return utils::Result<QVector<ParameterOption>>::failure(invalid(QStringLiteral("The enumeration option is invalid"), detail));
        }
        options.append(option);
    }

    return utils::Result<QVector<ParameterOption>>::success(options);
}

utils::Result<void> AiProviderCatalogHelper::boundedNumber(const QJsonObject& document, ParameterDescriptor& descriptor, const QString& detail) {
    if (!document.value(QStringLiteral("minimum")).isDouble() || !document.value(QStringLiteral("maximum")).isDouble()) {
        return utils::Result<void>::failure(invalid(QStringLiteral("The numeric parameter declares no bounds"), detail));
    }

    descriptor.minimum = document.value(QStringLiteral("minimum")).toDouble();
    descriptor.maximum = document.value(QStringLiteral("maximum")).toDouble();
    const double value = descriptor.defaultValue.toDouble(descriptor.minimum - 1.0);

    if (descriptor.minimum > descriptor.maximum || !descriptor.defaultValue.isDouble() || value < descriptor.minimum || value > descriptor.maximum) {
        return utils::Result<void>::failure(invalid(QStringLiteral("The numeric parameter default is outside its bounds"), detail));
    }
    if (descriptor.type == ParameterType::Integer && value != static_cast<double>(descriptor.defaultValue.toInteger())) {
        return utils::Result<void>::failure(invalid(QStringLiteral("The integer parameter default is not whole"), detail));
    }

    return utils::Result<void>::success();
}

utils::Result<ParameterDescriptor> AiProviderCatalogHelper::parameter(const QJsonObject& document, const QString& providerId) {
    const QSet<QString> known{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("type"), QStringLiteral("field"), QStringLiteral("trait"), QStringLiteral("boundByModelOutput"), QStringLiteral("modelMaximumWhenZero"), QStringLiteral("minimum"), QStringLiteral("maximum"), QStringLiteral("default"), QStringLiteral("options")};

    if (!hasKnownKeys(document, known)) {
        return utils::Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A declared parameter carries an unknown value"), providerId));
    }

    ParameterDescriptor descriptor;
    QString type;
    QString trait;
    const bool typed = readSettingsText(document, QStringLiteral("id"), descriptor.id) && readSettingsText(document, QStringLiteral("title"), descriptor.titleKey) && readSettingsText(document, QStringLiteral("field"), descriptor.field) && readSettingsText(document, QStringLiteral("type"), type) && readSettingsText(document, QStringLiteral("trait"), trait) && readSettingsBool(document, QStringLiteral("boundByModelOutput"), descriptor.boundByModelOutput) && readSettingsBool(document, QStringLiteral("modelMaximumWhenZero"), descriptor.modelMaximumWhenZero);
    const auto parsedType = parameterTypeFromIdentifier(type);
    const QString detail = providerId + QLatin1Char('.') + descriptor.id;

    if (!typed || descriptor.id.isEmpty() || descriptor.titleKey.isEmpty() || descriptor.field.isEmpty() || !parsedType.has_value() || descriptor.field.split(QLatin1Char('.')).contains(QString{})) {
        return utils::Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A declared parameter is invalid"), detail));
    }

    if (document.contains(QStringLiteral("trait"))) {
        const auto parsedTrait = traitFromIdentifier(trait);
        if (!parsedTrait.has_value()) {
            return utils::Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A declared parameter names an unknown trait"), detail));
        }
        descriptor.requiredTrait = parsedTrait;
    }

    descriptor.type = *parsedType;
    descriptor.defaultValue = document.value(QStringLiteral("default"));
    const bool numeric = descriptor.type == ParameterType::Integer || descriptor.type == ParameterType::Number;

    if (!numeric && (document.contains(QStringLiteral("minimum")) || document.contains(QStringLiteral("maximum")))) {
        return utils::Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A parameter that is not numeric declares bounds"), detail));
    }
    if (descriptor.type != ParameterType::Enumeration && document.contains(QStringLiteral("options"))) {
        return utils::Result<ParameterDescriptor>::failure(invalid(QStringLiteral("A parameter that is not an enumeration declares options"), detail));
    }

    if (numeric) {
        const auto bounds = boundedNumber(document, descriptor, detail);
        return bounds.hasValue() ? utils::Result<ParameterDescriptor>::success(descriptor) : utils::Result<ParameterDescriptor>::failure(bounds.error());
    }

    const auto options = parameterOptions(document.value(QStringLiteral("options")), detail);

    if (!options.hasValue()) {
        return utils::Result<ParameterDescriptor>::failure(options.error());
    }

    descriptor.options = options.value();
    // clang-format off
    const auto selected = std::find_if(descriptor.options.cbegin(), descriptor.options.cend(), [&descriptor](const ParameterOption& option) { return option.id == descriptor.defaultValue.toString(); });
    // clang-format on

    if (selected == descriptor.options.cend()) {
        return utils::Result<ParameterDescriptor>::failure(invalid(QStringLiteral("The enumeration default is not one of its options"), detail));
    }

    return utils::Result<ParameterDescriptor>::success(descriptor);
}

// Two entries may share one identifier only when a model reaches exactly one of them, which is what distinct required traits guarantee.
utils::Result<QVector<ParameterDescriptor>> AiProviderCatalogHelper::parameterSet(const QJsonValue& value, const QString& providerId) {
    if (!value.isArray() || value.toArray().isEmpty()) {
        return utils::Result<QVector<ParameterDescriptor>>::failure(invalid(QStringLiteral("The provider declares no parameter"), providerId));
    }

    QVector<ParameterDescriptor> parameters;

    for (const auto& entry : value.toArray()) {
        if (!entry.isObject()) {
            return utils::Result<QVector<ParameterDescriptor>>::failure(invalid(QStringLiteral("A declared parameter is not an object"), providerId));
        }
        const auto descriptor = parameter(entry.toObject(), providerId);
        if (!descriptor.hasValue()) {
            return utils::Result<QVector<ParameterDescriptor>>::failure(descriptor.error());
        }
        for (const auto& existing : parameters) {
            if (existing.id != descriptor.value().id) {
                continue;
            }
            if (!existing.requiredTrait.has_value() || !descriptor.value().requiredTrait.has_value() || existing.requiredTrait == descriptor.value().requiredTrait) {
                return utils::Result<QVector<ParameterDescriptor>>::failure(invalid(QStringLiteral("A parameter identifier is declared twice for the same model"), providerId + QLatin1Char('.') + existing.id));
            }
        }
        parameters.append(descriptor.value());
    }

    return utils::Result<QVector<ParameterDescriptor>>::success(parameters);
}

utils::Result<ProviderDescriptor> AiProviderCatalogHelper::provider(const QJsonObject& document) {
    const QSet<QString> known{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("protocol"), QStringLiteral("baseUrl"), QStringLiteral("addressConfigurable"), QStringLiteral("apiKeyVariable"), QStringLiteral("requiresApiKey"), QStringLiteral("userDefinedTraits"), QStringLiteral("preferredModels"), QStringLiteral("requestMaxRetries"), QStringLiteral("streamIdleTimeoutMs"), QStringLiteral("headers"), QStringLiteral("queryParameters"), QStringLiteral("parameters"), QStringLiteral("command")};

    if (!hasKnownKeys(document, known)) {
        return utils::Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A declared provider carries an unknown value"), {}));
    }

    ProviderDescriptor descriptor;
    QString protocol;
    const bool typed = readSettingsText(document, QStringLiteral("id"), descriptor.id) && readSettingsText(document, QStringLiteral("title"), descriptor.titleKey) && readSettingsText(document, QStringLiteral("protocol"), protocol) && readSettingsText(document, QStringLiteral("baseUrl"), descriptor.baseUrl) && readSettingsText(document, QStringLiteral("apiKeyVariable"), descriptor.apiKeyVariable) && readSettingsBool(document, QStringLiteral("addressConfigurable"), descriptor.addressConfigurable) && readSettingsBool(document, QStringLiteral("requiresApiKey"), descriptor.requiresApiKey) && readSettingsInteger(document, QStringLiteral("requestMaxRetries"), descriptor.requestMaxRetries) && readSettingsInteger(document, QStringLiteral("streamIdleTimeoutMs"), descriptor.streamIdleTimeoutMs);
    const auto parsedProtocol = protocolFromIdentifier(protocol);

    if (!typed || descriptor.id.isEmpty() || descriptor.titleKey.isEmpty() || !parsedProtocol.has_value() || descriptor.requestMaxRetries < 0 || descriptor.requestMaxRetries > 10 || descriptor.streamIdleTimeoutMs < 1000 || descriptor.streamIdleTimeoutMs > 600000) {
        return utils::Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A declared provider is invalid"), descriptor.id));
    }
    // A provider that requires a credential names the variable that credential officially lives in.
    if (descriptor.requiresApiKey && descriptor.apiKeyVariable.isEmpty()) {
        return utils::Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider requiring a credential names no environment variable"), descriptor.id));
    }

    descriptor.protocol = *parsedProtocol;

    if (descriptor.protocol == WireProtocol::CommandLine) {
        const auto commandLine = commandLineDescriptor(document, descriptor.id);

        if (!commandLine.hasValue()) {
            return utils::Result<ProviderDescriptor>::failure(commandLine.error());
        }

        descriptor.commandLine = commandLine.value();
    } else {
        const QUrl address(descriptor.baseUrl);

        if (!address.isValid() || address.scheme().isEmpty() || document.contains(QStringLiteral("command"))) {
            return utils::Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider reached over a wire declares no address or declares a program"), descriptor.id));
        }
    }

    const auto traits = traitSet(document.value(QStringLiteral("userDefinedTraits")), descriptor.id);

    if (!traits.hasValue()) {
        return utils::Result<ProviderDescriptor>::failure(traits.error());
    }
    // A command line agent runs its own tools, so it is the one provider that declares none.
    if (descriptor.protocol != WireProtocol::CommandLine && !traits.value().contains(ModelTrait::FunctionCalling)) {
        return utils::Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A provider declares a user-defined model that calls no tool"), descriptor.id));
    }

    descriptor.userDefinedModelTraits = traits.value();

    const auto preferred = stringList(document.value(QStringLiteral("preferredModels")), descriptor.id);
    const auto headers = stringMap(document.value(QStringLiteral("headers")), descriptor.id);
    const auto queries = stringMap(document.value(QStringLiteral("queryParameters")), descriptor.id);
    const auto parameters = descriptor.protocol == WireProtocol::CommandLine ? utils::Result<QVector<ParameterDescriptor>>::success({}) : parameterSet(document.value(QStringLiteral("parameters")), descriptor.id);

    if (!preferred.hasValue() || !headers.hasValue() || !queries.hasValue() || !parameters.hasValue()) {
        return utils::Result<ProviderDescriptor>::failure(!preferred.hasValue() ? preferred.error() : (!headers.hasValue() ? headers.error() : (!queries.hasValue() ? queries.error() : parameters.error())));
    }

    descriptor.preferredModels = preferred.value();
    descriptor.httpHeaders = headers.value();
    descriptor.queryParameters = queries.value();
    descriptor.parameters = parameters.value();

    if (descriptor.protocol == WireProtocol::CommandLine && (!descriptor.parameters.isEmpty() || descriptor.requiresApiKey || descriptor.addressConfigurable || !descriptor.baseUrl.isEmpty())) {
        return utils::Result<ProviderDescriptor>::failure(invalid(QStringLiteral("A command line provider declares an address, a credential or a parameter"), descriptor.id));
    }

    return utils::Result<ProviderDescriptor>::success(descriptor);
}

utils::Result<AiLimits> AiProviderCatalogHelper::limits(const QJsonObject& document) {
    const QSet<QString> known{QStringLiteral("repeatedToolCallLimit"), QStringLiteral("summaryMaximumTokens"), QStringLiteral("toolDeadlineMs"), QStringLiteral("requestTimeoutMs"), QStringLiteral("discoveryTimeoutMs"), QStringLiteral("serverStartTimeoutMs"), QStringLiteral("scheduleWakeupMs"), QStringLiteral("maximumAgentIterations"), QStringLiteral("maximumCommandTimeoutSeconds"), QStringLiteral("maximumParallelExecutions"), QStringLiteral("maximumSamplingTokens"), QStringLiteral("maximumRequestDelayMs"), QStringLiteral("maximumRequestsPerMinute"), QStringLiteral("maximumConcurrentRequests"), QStringLiteral("retryBackoffMs"), QStringLiteral("maximumRetryBackoffMs")};

    if (!hasExactKeys(document, known)) {
        return utils::Result<AiLimits>::failure(invalid(QStringLiteral("The AI catalog limits are incomplete"), {}));
    }

    bool valid = true;
    // clang-format off
    const auto read = [&document, &valid](const QString& key, int minimum, int maximum) {
        const QJsonValue value = document.value(key);
        if (!value.isDouble() || value.toInteger() < minimum || value.toInteger() > maximum) {
            valid = false;
            return 0;
        }
        return static_cast<int>(value.toInteger());
    };
    // clang-format on

    AiLimits values;
    values.repeatedToolCallLimit = read(QStringLiteral("repeatedToolCallLimit"), 1, 100);
    values.summaryMaximumTokens = read(QStringLiteral("summaryMaximumTokens"), 128, 100000);
    values.toolDeadlineMs = read(QStringLiteral("toolDeadlineMs"), 1000, 3600000);
    values.requestTimeoutMs = read(QStringLiteral("requestTimeoutMs"), 1000, 3600000);
    values.discoveryTimeoutMs = read(QStringLiteral("discoveryTimeoutMs"), 1000, 600000);
    values.serverStartTimeoutMs = read(QStringLiteral("serverStartTimeoutMs"), 1000, 600000);
    values.scheduleWakeupMs = read(QStringLiteral("scheduleWakeupMs"), 1000, 3600000);
    values.maximumAgentIterations = read(QStringLiteral("maximumAgentIterations"), 1, 100000);
    values.maximumCommandTimeoutSeconds = read(QStringLiteral("maximumCommandTimeoutSeconds"), 1, 604800);
    values.maximumParallelExecutions = read(QStringLiteral("maximumParallelExecutions"), 1, 1024);
    values.maximumSamplingTokens = read(QStringLiteral("maximumSamplingTokens"), 128, 10000000);
    values.maximumRequestDelayMs = read(QStringLiteral("maximumRequestDelayMs"), 1000, 3600000);
    values.maximumRequestsPerMinute = read(QStringLiteral("maximumRequestsPerMinute"), 1, 1000000);
    values.maximumConcurrentRequests = read(QStringLiteral("maximumConcurrentRequests"), 1, 1024);
    values.retryBackoffMs = read(QStringLiteral("retryBackoffMs"), 100, 60000);
    values.maximumRetryBackoffMs = read(QStringLiteral("maximumRetryBackoffMs"), 1000, 600000);

    if (!valid) {
        return utils::Result<AiLimits>::failure(invalid(QStringLiteral("The AI catalog limits are invalid"), {}));
    }

    return utils::Result<AiLimits>::success(values);
}

// The model list is data, so a model is added by one line in the catalog file and never by interface code.
utils::Result<QVector<ModelDescriptor>> AiProviderCatalogHelper::importedModels(const QJsonArray& entries, const QString& providerId, bool reachedOverAWire) {
    QVector<ModelDescriptor> models;

    for (const auto& value : entries) {
        const QJsonObject entry = value.toObject();
        if (!hasKnownKeys(entry, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("context"), QStringLiteral("output"), QStringLiteral("traits"), QStringLiteral("inputCost"), QStringLiteral("outputCost")})) {
            return utils::Result<QVector<ModelDescriptor>>::failure(invalid(QStringLiteral("A catalog model carries an unknown value"), providerId));
        }

        ModelDescriptor model;
        model.displayName = entry.value(QStringLiteral("id")).toString();
        const bool typed = readSettingsText(entry, QStringLiteral("id"), model.id) && readSettingsText(entry, QStringLiteral("name"), model.displayName) && readSettingsInteger(entry, QStringLiteral("context"), model.contextWindow) && readSettingsInteger(entry, QStringLiteral("output"), model.maximumOutputTokens);
        if (!typed || model.id.isEmpty() || model.contextWindow <= 0 || model.maximumOutputTokens <= 0) {
            return utils::Result<QVector<ModelDescriptor>>::failure(invalid(QStringLiteral("A catalog model is invalid"), model.id.isEmpty() ? providerId : model.id));
        }

        const auto inputCost = readModelCost(entry, QStringLiteral("inputCost"), model.id);
        const auto outputCost = readModelCost(entry, QStringLiteral("outputCost"), model.id);
        if (!inputCost.hasValue()) {
            return utils::Result<QVector<ModelDescriptor>>::failure(inputCost.error());
        }
        if (!outputCost.hasValue()) {
            return utils::Result<QVector<ModelDescriptor>>::failure(outputCost.error());
        }

        model.inputCostPerToken = inputCost.value();
        model.outputCostPerToken = outputCost.value();

        const auto traits = traitSet(entry.value(QStringLiteral("traits")), model.id);
        if (!traits.hasValue()) {
            return utils::Result<QVector<ModelDescriptor>>::failure(traits.error());
        }
        model.traits = traits.value();
        // A model reached over a wire that calls no tool cannot run a task, while a command line agent runs its own.
        if (reachedOverAWire && !model.traits.contains(ModelTrait::FunctionCalling)) {
            return utils::Result<QVector<ModelDescriptor>>::failure(invalid(QStringLiteral("A catalog model declares no tool calling"), model.id));
        }
        models.append(model);
    }

    return utils::Result<QVector<ModelDescriptor>>::success(models);
}

utils::Result<void> AiProviderCatalogHelper::mergeImportedModels(QVector<ProviderDescriptor>& providers, const QByteArray& modelsDocument) {
    const auto document = parseDocument(modelsDocument);

    if (!document.hasValue()) {
        return utils::Result<void>::failure(document.error());
    }
    if (!hasExactKeys(document.value(), {QStringLiteral("providers")}) || !document.value().value(QStringLiteral("providers")).isObject()) {
        return utils::Result<void>::failure(invalid(QStringLiteral("The AI model catalog is not a catalog"), {}));
    }

    const QJsonObject declared = document.value().value(QStringLiteral("providers")).toObject();

    for (auto entry = declared.constBegin(); entry != declared.constEnd(); ++entry) {
        // clang-format off
        const auto position = std::find_if(providers.begin(), providers.end(), [&entry](const ProviderDescriptor& provider) { return provider.id == entry.key(); });
        // clang-format on
        if (position == providers.end()) {
            return utils::Result<void>::failure(invalid(QStringLiteral("The AI model catalog names an unknown provider"), entry.key()));
        }
        if (!entry.value().isArray()) {
            return utils::Result<void>::failure(invalid(QStringLiteral("The AI model catalog entry is not a list"), entry.key()));
        }

        const auto imported = importedModels(entry.value().toArray(), entry.key(), position->protocol != WireProtocol::CommandLine);
        if (!imported.hasValue()) {
            return utils::Result<void>::failure(imported.error());
        }

        // The file owns what every model is, while the provider declares only which of them it opens with.
        const QVector<ModelDescriptor> models = imported.value();
        for (const auto& preferred : position->preferredModels) {
            // clang-format off
            const auto model = std::find_if(models.cbegin(), models.cend(), [&preferred](const ModelDescriptor& candidate) { return candidate.id == preferred; });
            // clang-format on
            if (model == models.cend()) {
                return utils::Result<void>::failure(invalid(QStringLiteral("A provider prefers a model the catalog does not declare"), preferred));
            }
            position->models.append(*model);
        }
        for (const auto& model : models) {
            if (!position->preferredModels.contains(model.id)) {
                position->models.append(model);
            }
        }
    }

    return utils::Result<void>::success();
}

utils::Result<AiCatalog> loadAiCatalog(const QByteArray& providersDocument, const QByteArray& modelsDocument) {
    const auto document = AiProviderCatalogHelper::parseDocument(providersDocument);

    if (!document.hasValue()) {
        return utils::Result<AiCatalog>::failure(document.error());
    }
    if (!hasExactKeys(document.value(), {QStringLiteral("limits"), QStringLiteral("providers")}) || !document.value().value(QStringLiteral("providers")).isArray() || !document.value().value(QStringLiteral("limits")).isObject()) {
        return utils::Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("The AI provider catalog is not a catalog"), {}));
    }

    const auto declaredLimits = AiProviderCatalogHelper::limits(document.value().value(QStringLiteral("limits")).toObject());

    if (!declaredLimits.hasValue()) {
        return utils::Result<AiCatalog>::failure(declaredLimits.error());
    }

    AiCatalog catalog;
    catalog.limits = declaredLimits.value();

    for (const auto& entry : document.value().value(QStringLiteral("providers")).toArray()) {
        if (!entry.isObject()) {
            return utils::Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("A declared provider is not an object"), {}));
        }
        const auto descriptor = AiProviderCatalogHelper::provider(entry.toObject());
        if (!descriptor.hasValue()) {
            return utils::Result<AiCatalog>::failure(descriptor.error());
        }
        // clang-format off
        const auto duplicate = std::find_if(catalog.providers.cbegin(), catalog.providers.cend(), [&descriptor](const ProviderDescriptor& existing) { return existing.id == descriptor.value().id; });
        // clang-format on
        if (duplicate != catalog.providers.cend()) {
            return utils::Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("A provider identifier is declared twice"), descriptor.value().id));
        }
        catalog.providers.append(descriptor.value());
    }

    if (catalog.providers.isEmpty()) {
        return utils::Result<AiCatalog>::failure(AiProviderCatalogHelper::invalid(QStringLiteral("The AI provider catalog declares no provider"), {}));
    }

    const auto merged = AiProviderCatalogHelper::mergeImportedModels(catalog.providers, modelsDocument);
    return merged.hasValue() ? utils::Result<AiCatalog>::success(catalog) : utils::Result<AiCatalog>::failure(merged.error());
}

LoadedAiCatalog AiProviderCatalogHelper::load() {
    const auto loaded = loadAiCatalog(resourceContents(QStringLiteral(":/slotdeck/ai/assets/providers.json")), resourceContents(QStringLiteral(":/slotdeck/ai/assets/models.json")));

    if (!loaded.hasValue()) {
        return {{}, utils::Result<void>::failure(loaded.error())};
    }

    return {loaded.value(), utils::Result<void>::success()};
}

// The catalog is built once, so the plugin asks here whether the files it was built from were well formed.
const LoadedAiCatalog& AiProviderCatalogHelper::loadedCatalog() {
    static const LoadedAiCatalog loaded = AiProviderCatalogHelper::load();
    return loaded;
}

const QVector<ProviderDescriptor>& providerCatalog() {
    return AiProviderCatalogHelper::loadedCatalog().catalog.providers;
}

const AiLimits& aiLimits() {
    return AiProviderCatalogHelper::loadedCatalog().catalog.limits;
}

const utils::Result<void>& aiCatalogError() {
    return AiProviderCatalogHelper::loadedCatalog().error;
}

const ProviderDescriptor* findProvider(const QString& providerId) {
    for (const auto& provider : providerCatalog()) {
        if (provider.id == providerId) {
            return &provider;
        }
    }

    return nullptr;
}

const ModelDescriptor* findModel(const ProviderDescriptor& provider, const QString& modelId) {
    for (const auto& model : provider.models) {
        if (model.id == modelId) {
            return &model;
        }
    }

    return nullptr;
}

std::optional<double> runCost(const QString& providerId, const QString& modelId, qint64 inputTokens, qint64 outputTokens) {
    const ProviderDescriptor* provider = findProvider(providerId);

    if (provider == nullptr || inputTokens < 0 || outputTokens < 0) {
        return std::nullopt;
    }

    const ModelDescriptor* model = findModel(*provider, modelId);

    if (model == nullptr || !model->inputCostPerToken.has_value() || !model->outputCostPerToken.has_value()) {
        return std::nullopt;
    }

    return static_cast<double>(inputTokens) * model->inputCostPerToken.value() + static_cast<double>(outputTokens) * model->outputCostPerToken.value();
}

// A model outside the catalog keeps the trait set the provider declares for its own models instead of an inferred capability.
QSet<ModelTrait> modelTraits(const ProviderDescriptor& provider, const QString& modelId) {
    const ModelDescriptor* model = findModel(provider, modelId);
    return model != nullptr ? model->traits : provider.userDefinedModelTraits;
}

// The output budget a model accepts is its own, so the declared maximum bounds the parameter instead of a shared ceiling.
QVector<ParameterDescriptor> applicableParameters(const ProviderDescriptor& provider, const QString& modelId) {
    const QSet<ModelTrait> traits = modelTraits(provider, modelId);
    const ModelDescriptor* model = findModel(provider, modelId);
    QVector<ParameterDescriptor> applicable;

    for (const auto& parameter : provider.parameters) {
        if (parameter.requiredTrait.has_value() && !traits.contains(parameter.requiredTrait.value())) {
            continue;
        }

        ParameterDescriptor descriptor = parameter;
        if (descriptor.boundByModelOutput && model != nullptr && model->maximumOutputTokens > 0) {
            descriptor.maximum = model->maximumOutputTokens;
            const qint64 declared = descriptor.defaultValue.toInteger();
            descriptor.defaultValue = QJsonValue(declared == 0 ? 0 : std::min<qint64>(declared, model->maximumOutputTokens));
        }
        applicable.append(descriptor);
    }

    return applicable;
}

QJsonObject defaultParameters(const ProviderDescriptor& provider, const QString& modelId) {
    QJsonObject defaults;

    for (const auto& parameter : applicableParameters(provider, modelId)) {
        defaults.insert(parameter.id, parameter.defaultValue);
    }

    return defaults;
}

// The answer budget is the one value the conversation fitter and the summary request have to know by name.
std::optional<ParameterDescriptor> outputBudgetParameter(const ProviderDescriptor& provider, const QString& modelId) {
    for (const auto& parameter : applicableParameters(provider, modelId)) {
        if (parameter.boundByModelOutput) {
            return parameter;
        }
    }

    return std::nullopt;
}

} // namespace slotdeck::plugins::ai
