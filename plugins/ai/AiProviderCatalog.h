#pragma once

#include "plugins/PluginInterface.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace slotdeck::plugins::ai {

enum class WireProtocol { OpenAiCompatible, Anthropic, CommandLine };

inline constexpr auto commandLinePromptPlaceholder = "{prompt}";
inline constexpr auto commandLineWorkdirPlaceholder = "{workdir}";

// A command line agent runs its own tools and answers plain text, so it is invoked rather than requested.
struct CommandLineDescriptor final {
    QString program;
    QStringList arguments;
    // A credential in the environment makes these agents bill the key instead of the subscription the reader pays for, so the run starts without them.
    QStringList clearedVariables;
};

enum class ModelTrait { Sampling, Reasoning, FunctionCalling, Vision, SystemPrompt };

enum class ParameterType { Integer, Number, Enumeration };

struct ParameterOption final {
    QString id;
    QString titleKey;
};

struct ParameterDescriptor final {
    QString id;
    QString titleKey;
    // The field the provider names on the wire, written as a dotted path when the protocol nests it.
    QString field;
    ParameterType type{ParameterType::Number};
    std::optional<ModelTrait> requiredTrait;
    // The parameter that carries the answer budget is bounded by what the selected model accepts.
    bool boundByModelOutput{false};
    // A zero means the maximum the selected model declares, which is what reaches the service.
    bool modelMaximumWhenZero{false};
    double minimum{0.0};
    double maximum{0.0};
    QJsonValue defaultValue;
    QVector<ParameterOption> options;
};

struct ModelDescriptor final {
    QString id;
    QString displayName;
    QSet<ModelTrait> traits;
    int contextWindow{0};
    int maximumOutputTokens{0};
    // The price a service publishes per token, absent for a model nobody published one for.
    std::optional<double> inputCostPerToken;
    std::optional<double> outputCostPerToken;
};

struct ProviderDescriptor final {
    QString id;
    // The models a provider opens with, resolved from the catalog file that owns what each one is.
    QStringList preferredModels;
    QString titleKey;
    int requestMaxRetries{2};
    int streamIdleTimeoutMs{60000};
    QMap<QString, QString> httpHeaders;
    QMap<QString, QString> queryParameters;
    WireProtocol protocol{WireProtocol::OpenAiCompatible};
    QString baseUrl;
    // A self-hosted service is the only one whose address the user owns.
    bool addressConfigurable{false};
    QString apiKeyVariable;
    bool requiresApiKey{true};
    CommandLineDescriptor commandLine;
    QSet<ModelTrait> userDefinedModelTraits;
    QVector<ModelDescriptor> models;
    QVector<ParameterDescriptor> parameters;
};

// What the agent may be tuned with lives with the catalog, while the caps that keep a payload from filling memory stay in the code that enforces them.
struct AiLimits final {
    int repeatedToolCallLimit{0};
    int summaryMaximumTokens{0};
    int toolDeadlineMs{0};
    int requestTimeoutMs{0};
    int discoveryTimeoutMs{0};
    int serverStartTimeoutMs{0};
    int scheduleWakeupMs{0};
    int maximumAgentIterations{0};
    int maximumCommandTimeoutSeconds{0};
    int maximumParallelExecutions{0};
    int maximumSamplingTokens{0};
    int maximumRequestDelayMs{0};
    int maximumRequestsPerMinute{0};
    int maximumConcurrentRequests{0};
    int retryBackoffMs{0};
    int maximumRetryBackoffMs{0};
};

// The catalog is the parsed form of the two files the plugin carries, and loading it from text is what makes it testable.
struct AiCatalog final {
    QVector<ProviderDescriptor> providers;
    AiLimits limits;
};

[[nodiscard]] utils::Result<AiCatalog> loadAiCatalog(const QByteArray& providersDocument, const QByteArray& modelsDocument);
[[nodiscard]] const QVector<ProviderDescriptor>& providerCatalog();
[[nodiscard]] const AiLimits& aiLimits();
[[nodiscard]] const utils::Result<void>& aiCatalogError();
[[nodiscard]] const ProviderDescriptor* findProvider(const QString& providerId);
[[nodiscard]] const ModelDescriptor* findModel(const ProviderDescriptor& provider, const QString& modelId);

// What a run of that many tokens cost, absent when the model or its price is not declared.
[[nodiscard]] std::optional<double> runCost(const QString& providerId, const QString& modelId, qint64 inputTokens, qint64 outputTokens);
[[nodiscard]] QSet<ModelTrait> modelTraits(const ProviderDescriptor& provider, const QString& modelId);
// The trait is named as the catalog file spells it, because that is the name the reader of a prompt already knows.
[[nodiscard]] QString modelTraitIdentifier(ModelTrait trait);
[[nodiscard]] QVector<ParameterDescriptor> applicableParameters(const ProviderDescriptor& provider, const QString& modelId);
[[nodiscard]] QJsonObject defaultParameters(const ProviderDescriptor& provider, const QString& modelId);
[[nodiscard]] std::optional<ParameterDescriptor> outputBudgetParameter(const ProviderDescriptor& provider, const QString& modelId);

} // namespace slotdeck::plugins::ai
