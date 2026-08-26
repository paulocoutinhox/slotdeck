#include "LanguageRegistry.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <optional>

namespace slotdeck::plugins::codeeditor {

class LanguageRegistryHelper final {
  public:
    static LanguageDefinition plainText();
    static QJsonObject readCatalog(utils::Result<void>& outcome);
    static QVector<LanguageDefinition> createLanguages();
    static QVector<LanguageServerDefinition> createLanguageServers();
    static QStringList textList(const QJsonObject& entry, const QString& key, bool& valid);
    static std::optional<HighlightRole> roleFromIdentifier(const QString& identifier);
    static QVector<HighlightPattern> patternList(const QJsonArray& entries, bool& valid);
};

LanguageDefinition LanguageRegistryHelper::plainText() {
    return {QStringLiteral("plaintext"), QStringLiteral("Plain Text"), {}, {}, {}, {}, {}, {}, {}};
}

// The catalog is data, so a language or a server is added by one entry in the file and never by interface code.
QJsonObject LanguageRegistryHelper::readCatalog(utils::Result<void>& outcome) {
    QFile file(QStringLiteral(":/slotdeck/code-editor/assets/languages.json"));

    if (!file.open(QIODevice::ReadOnly)) {
        outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "The language catalog is unavailable", {}});
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);

    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value(QStringLiteral("languages")).isArray() || !document.object().value(QStringLiteral("servers")).isArray()) {
        outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "The language catalog is not a catalog", error.errorString()});
        return {};
    }

    return document.object();
}

QStringList LanguageRegistryHelper::textList(const QJsonObject& entry, const QString& key, bool& valid) {
    if (!entry.contains(key)) {
        return {};
    }

    if (!entry.value(key).isArray()) {
        valid = false;
        return {};
    }

    QStringList values;

    for (const auto& value : entry.value(key).toArray()) {
        if (!value.isString() || value.toString().isEmpty()) {
            valid = false;
            return {};
        }
        values.append(value.toString());
    }

    return values;
}

std::optional<HighlightRole> LanguageRegistryHelper::roleFromIdentifier(const QString& identifier) {
    static const QMap<QString, HighlightRole> roles{{QStringLiteral("keyword"), HighlightRole::Keyword}, {QStringLiteral("number"), HighlightRole::Number}, {QStringLiteral("string"), HighlightRole::String}, {QStringLiteral("identifier"), HighlightRole::Identifier}, {QStringLiteral("declaration"), HighlightRole::Declaration}, {QStringLiteral("function"), HighlightRole::Function}, {QStringLiteral("comment"), HighlightRole::Comment}, {QStringLiteral("heading"), HighlightRole::Heading}, {QStringLiteral("emphasis"), HighlightRole::Emphasis}, {QStringLiteral("markup"), HighlightRole::Markup}};
    return roles.contains(identifier) ? std::optional<HighlightRole>{roles.value(identifier)} : std::nullopt;
}

QVector<HighlightPattern> LanguageRegistryHelper::patternList(const QJsonArray& entries, bool& valid) {
    QVector<HighlightPattern> patterns;

    for (const auto& value : entries) {
        const QJsonObject entry = value.toObject();
        const auto role = roleFromIdentifier(entry.value(QStringLiteral("role")).toString());
        const QString expression = entry.value(QStringLiteral("pattern")).toString();
        if (!role.has_value() || expression.isEmpty() || !QRegularExpression(expression).isValid()) {
            valid = false;
            return {};
        }
        patterns.append({expression, *role});
    }

    return patterns;
}

QVector<LanguageDefinition> LanguageRegistryHelper::createLanguages() {
    utils::Result<void>& outcome = LanguageRegistry::mutableCatalogError();
    const QJsonObject catalog = readCatalog(outcome);

    if (!outcome.hasValue()) {
        return {plainText()};
    }

    QVector<LanguageDefinition> languages;
    QStringList claimed;

    for (const auto& value : catalog.value(QStringLiteral("languages")).toArray()) {
        const QJsonObject entry = value.toObject();
        bool valid = entry.value(QStringLiteral("id")).isString() && entry.value(QStringLiteral("name")).isString();
        LanguageDefinition language;
        language.id = entry.value(QStringLiteral("id")).toString();
        language.name = entry.value(QStringLiteral("name")).toString();
        language.extensions = textList(entry, QStringLiteral("extensions"), valid);
        language.patterns = patternList(entry.value(QStringLiteral("patterns")).toArray(), valid);
        language.fileNames = textList(entry, QStringLiteral("fileNames"), valid);
        language.lineComment = entry.value(QStringLiteral("lineComment")).toString();
        language.blockCommentStart = entry.value(QStringLiteral("blockCommentStart")).toString();
        language.blockCommentEnd = entry.value(QStringLiteral("blockCommentEnd")).toString();
        if (entry.contains(QStringLiteral("keywords"))) {
            valid = valid && entry.value(QStringLiteral("keywords")).isString();
            language.keywords = entry.value(QStringLiteral("keywords")).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        if (!valid || language.id.isEmpty() || language.name.isEmpty() || language.blockCommentStart.isEmpty() != language.blockCommentEnd.isEmpty()) {
            outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "A catalog language is invalid", language.id});
            return {plainText()};
        }

        // The first language claiming an extension is the one that answers for it, so a second claim would never be reached.
        for (const auto& extension : language.extensions) {
            if (claimed.contains(extension, Qt::CaseInsensitive)) {
                outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "Two catalog languages claim one extension", extension});
                return {plainText()};
            }
            claimed.append(extension);
        }
        languages.append(language);
    }

    if (languages.isEmpty() || languages.last().id != QStringLiteral("plaintext")) {
        outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "The language catalog does not end with plain text", {}});
        return {plainText()};
    }

    return languages;
}

QVector<LanguageServerDefinition> LanguageRegistryHelper::createLanguageServers() {
    utils::Result<void>& outcome = LanguageRegistry::mutableCatalogError();
    const QJsonObject catalog = readCatalog(outcome);

    if (!outcome.hasValue()) {
        return {};
    }

    QVector<LanguageServerDefinition> servers;

    for (const auto& value : catalog.value(QStringLiteral("servers")).toArray()) {
        const QJsonObject entry = value.toObject();
        LanguageServerDefinition definition;
        definition.languageId = entry.value(QStringLiteral("language")).toString();
        bool valid = !definition.languageId.isEmpty() && entry.value(QStringLiteral("candidates")).isArray();
        for (const auto& candidateValue : entry.value(QStringLiteral("candidates")).toArray()) {
            const QJsonObject candidateEntry = candidateValue.toObject();
            LanguageServerCandidate candidate;
            candidate.executableName = candidateEntry.value(QStringLiteral("executable")).toString();
            candidate.arguments = textList(candidateEntry, QStringLiteral("arguments"), valid);
            valid = valid && !candidate.executableName.isEmpty();
            definition.candidates.append(candidate);
        }
        if (!valid || definition.candidates.isEmpty() || LanguageRegistry::languageForId(definition.languageId) == nullptr) {
            outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "A catalog language server is invalid", definition.languageId});
            return {};
        }
        servers.append(definition);
    }

    return servers;
}

utils::Result<void>& LanguageRegistry::mutableCatalogError() {
    static utils::Result<void> outcome = utils::Result<void>::success();
    return outcome;
}

// Both lists are read from the same file, so asking for the outcome builds whichever of them has not been built yet.
const utils::Result<void>& LanguageRegistry::catalogError() {
    static const bool built = [] { return !languages().isEmpty() && languageServers().size() >= 0 && !commonPatterns().isEmpty() && !semanticRoles().isEmpty() && limits().maximumFileBytes > 0; }();
    Q_UNUSED(built);
    return mutableCatalogError();
}

const QVector<HighlightPattern>& LanguageRegistry::commonPatterns() {
    static const QVector<HighlightPattern> patterns = [] {
        utils::Result<void>& outcome = mutableCatalogError();
        const QJsonObject catalog = LanguageRegistryHelper::readCatalog(outcome);
        bool valid = true;
        QVector<HighlightPattern> values = LanguageRegistryHelper::patternList(catalog.value(QStringLiteral("highlighting")).toObject().value(QStringLiteral("common")).toArray(), valid);

        if (!valid || values.isEmpty()) {
            outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "The catalog highlighting patterns are invalid", {}});
            return QVector<HighlightPattern>{};
        }

        return values;
    }();
    return patterns;
}

const QMap<QString, HighlightRole>& LanguageRegistry::semanticRoles() {
    static const QMap<QString, HighlightRole> roles = [] {
        utils::Result<void>& outcome = mutableCatalogError();
        const QJsonObject catalog = LanguageRegistryHelper::readCatalog(outcome);
        const QJsonObject declared = catalog.value(QStringLiteral("highlighting")).toObject().value(QStringLiteral("semantic")).toObject();
        QMap<QString, HighlightRole> values;

        for (auto entry = declared.constBegin(); entry != declared.constEnd(); ++entry) {
            const auto role = LanguageRegistryHelper::roleFromIdentifier(entry.value().toString());
            if (!role.has_value()) {
                outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "A catalog semantic token declares an unknown role", entry.key()});
                return QMap<QString, HighlightRole>{};
            }
            values.insert(entry.key(), *role);
        }

        if (values.isEmpty()) {
            outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "The catalog declares no semantic token", {}});
        }

        return values;
    }();
    return roles;
}

const EditorLimits& LanguageRegistry::limits() {
    static const EditorLimits values = [] {
        utils::Result<void>& outcome = mutableCatalogError();
        const QJsonObject declared = LanguageRegistryHelper::readCatalog(outcome).value(QStringLiteral("limits")).toObject();
        EditorLimits limits;
        bool valid = true;
        const auto read = [&declared, &valid](const QString& key, int minimum, int maximum) {
            const QJsonValue value = declared.value(key);

            if (!value.isDouble() || value.toInteger() < minimum || value.toInteger() > maximum) {
                valid = false;
                return 0LL;
            }

            return value.toInteger();
        };

        limits.maximumFileBytes = read(QStringLiteral("maximumFileBytes"), 1024, 64 * 1024 * 1024);
        limits.maximumHighlightedLineLength = static_cast<int>(read(QStringLiteral("maximumHighlightedLineLength"), 80, 100000));
        limits.maximumSemanticTokenLines = static_cast<int>(read(QStringLiteral("maximumSemanticTokenLines"), 100, 1000000));
        limits.maximumSearchMatches = static_cast<int>(read(QStringLiteral("maximumSearchMatches"), 100, 1000000));
        limits.partialRepaintDivisor = static_cast<int>(read(QStringLiteral("partialRepaintDivisor"), 1, 100));
        limits.changeDebounceMs = static_cast<int>(read(QStringLiteral("changeDebounceMs"), 10, 5000));
        limits.analysisDebounceMs = static_cast<int>(read(QStringLiteral("analysisDebounceMs"), 10, 5000));
        limits.highlightDebounceMs = static_cast<int>(read(QStringLiteral("highlightDebounceMs"), 10, 5000));
        limits.externalChangeDebounceMs = static_cast<int>(read(QStringLiteral("externalChangeDebounceMs"), 10, 5000));
        limits.maximumRestarts = static_cast<int>(read(QStringLiteral("maximumRestarts"), 0, 100));
        limits.restartWindowMs = static_cast<int>(read(QStringLiteral("restartWindowMs"), 1000, 3600000));
        limits.initializeTimeoutMs = static_cast<int>(read(QStringLiteral("initializeTimeoutMs"), 1000, 600000));
        limits.maximumReferences = static_cast<int>(read(QStringLiteral("maximumReferences"), 10, 100000));
        limits.maximumWorkspaceFiles = static_cast<int>(read(QStringLiteral("maximumWorkspaceFiles"), 100, 1000000));
        limits.maximumProblems = static_cast<int>(read(QStringLiteral("maximumProblems"), 10, 100000));
        limits.bottomPanelMinimumHeight = static_cast<int>(read(QStringLiteral("bottomPanelMinimumHeight"), 40, 2000));
        limits.bottomPanelInitialHeight = static_cast<int>(read(QStringLiteral("bottomPanelInitialHeight"), 40, 2000));

        if (!valid || limits.bottomPanelInitialHeight < limits.bottomPanelMinimumHeight) {
            outcome = utils::Result<void>::failure({"code_editor_catalog_invalid", "The catalog limits are invalid", {}});
            return EditorLimits{};
        }

        return limits;
    }();
    return values;
}

const LanguageDefinition* LanguageRegistry::languageForId(const QString& languageId) {
    for (const auto& language : languages()) {
        if (language.id == languageId) {
            return &language;
        }
    }

    return nullptr;
}

const QVector<LanguageDefinition>& LanguageRegistry::languages() {
    static const QVector<LanguageDefinition> values = LanguageRegistryHelper::createLanguages();
    return values;
}

const QVector<LanguageServerDefinition>& LanguageRegistry::languageServers() {
    static const QVector<LanguageServerDefinition> values = LanguageRegistryHelper::createLanguageServers();
    return values;
}

const LanguageDefinition& LanguageRegistry::languageForPath(const QString& path) {
    const QFileInfo information(path);

    for (const auto& language : languages()) {
        if (language.fileNames.contains(information.fileName(), Qt::CaseInsensitive) || language.extensions.contains(information.suffix(), Qt::CaseInsensitive)) {
            return language;
        }
    }

    static const LanguageDefinition fallback = LanguageRegistryHelper::plainText();
    return fallback;
}

// The protocol names C and C++ separately even though one server answers for both, so the identifier follows the file rather than the server.
QString LanguageRegistry::protocolLanguageId(const QString& path) {
    const LanguageDefinition& language = languageForPath(path);

    if (language.id == QStringLiteral("cpp") && QFileInfo(path).suffix() == QStringLiteral("c")) {
        return QStringLiteral("c");
    }

    return language.id;
}

std::optional<ResolvedLanguageServer> LanguageRegistry::resolveServer(const LanguageServerDefinition& definition) {
    for (const auto& candidate : definition.candidates) {
        const QString path = QStandardPaths::findExecutable(candidate.executableName);
        if (!path.isEmpty()) {
            return ResolvedLanguageServer{definition.languageId, path, candidate.arguments};
        }
    }

    return std::nullopt;
}

} // namespace slotdeck::plugins::codeeditor
