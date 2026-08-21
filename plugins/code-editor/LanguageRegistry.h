#pragma once

#include "utils/Result.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace slotdeck::plugins::codeeditor {

enum class HighlightRole { Keyword, Number, String, Identifier, Declaration, Function, Comment, Heading, Emphasis, Markup };

struct HighlightPattern final {
    QString pattern;
    HighlightRole role{HighlightRole::Identifier};
};

struct LanguageDefinition final {
    QString id;
    QString name;
    QStringList extensions;
    QStringList fileNames;
    QStringList keywords;
    QString lineComment;
    QString blockCommentStart;
    QString blockCommentEnd;
    QVector<HighlightPattern> patterns;
};

struct LanguageServerCandidate final {
    QString executableName;
    QStringList arguments;
};

struct LanguageServerDefinition final {
    QString languageId;
    QVector<LanguageServerCandidate> candidates;
};

struct ResolvedLanguageServer final {
    QString languageId;
    QString executablePath;
    QStringList arguments;
};

// What the editor may be tuned with lives with the catalog, while the caps that protect the process from a payload stay in the code that enforces them.
struct EditorLimits final {
    qint64 maximumFileBytes{0};
    int maximumHighlightedLineLength{0};
    int maximumSemanticTokenLines{0};
    int maximumSearchMatches{0};
    int partialRepaintDivisor{0};
    int changeDebounceMs{0};
    int analysisDebounceMs{0};
    int highlightDebounceMs{0};
    int externalChangeDebounceMs{0};
    int maximumRestarts{0};
    int restartWindowMs{0};
    int initializeTimeoutMs{0};
    int maximumReferences{0};
    int maximumWorkspaceFiles{0};
    int maximumProblems{0};
    int bottomPanelMinimumHeight{0};
    int bottomPanelInitialHeight{0};
};

class LanguageRegistry final {
  public:
    [[nodiscard]] static const QVector<LanguageDefinition>& languages();
    // The catalog is read once, so the plugin asks here whether the file it was built from was well formed.
    [[nodiscard]] static const utils::Result<void>& catalogError();
    [[nodiscard]] static utils::Result<void>& mutableCatalogError();
    [[nodiscard]] static const LanguageDefinition* languageForId(const QString& languageId);
    [[nodiscard]] static const QVector<HighlightPattern>& commonPatterns();
    [[nodiscard]] static const QMap<QString, HighlightRole>& semanticRoles();
    [[nodiscard]] static const EditorLimits& limits();
    [[nodiscard]] static const QVector<LanguageServerDefinition>& languageServers();
    [[nodiscard]] static const LanguageDefinition& languageForPath(const QString& path);
    [[nodiscard]] static QString protocolLanguageId(const QString& path);
    [[nodiscard]] static std::optional<ResolvedLanguageServer> resolveServer(const LanguageServerDefinition& definition);
};

} // namespace slotdeck::plugins::codeeditor
