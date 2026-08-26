#include "CodeSyntaxHighlighter.h"

#include <QRegularExpression>
#include <QTextBlock>

#include <utility>

namespace slotdeck::plugins::codeeditor {

class CodeSyntaxHighlighterHelper final {
  public:
    static QString keywordPattern(const QStringList& keywords);
    static QStringList keywordsIn(const QStringList& keywords, const QStringList& set);
    static QStringList keywordsOutside(const QStringList& keywords, const QStringList& first, const QStringList& second);
};

QString CodeSyntaxHighlighterHelper::keywordPattern(const QStringList& keywords) {
    QStringList escaped;
    escaped.reserve(keywords.size());

    for (const auto& keyword : keywords) {
        escaped.append(QRegularExpression::escape(keyword));
    }

    return QStringLiteral("\\b(?:%1)\\b").arg(escaped.join(QLatin1Char('|')));
}

QStringList CodeSyntaxHighlighterHelper::keywordsIn(const QStringList& keywords, const QStringList& set) {
    QStringList values;

    for (const auto& keyword : keywords) {
        if (set.contains(keyword)) {
            values.append(keyword);
        }
    }

    return values;
}

QStringList CodeSyntaxHighlighterHelper::keywordsOutside(const QStringList& keywords, const QStringList& first, const QStringList& second) {
    QStringList values;

    for (const auto& keyword : keywords) {
        if (!first.contains(keyword) && !second.contains(keyword)) {
            values.append(keyword);
        }
    }

    return values;
}

// The order a rule is added in decides which one wins, so a keyword beats the shape that only guessed at it and a string beats them both.
CodeSyntaxHighlighter::CodeSyntaxHighlighter(QTextDocument* document, LanguageDefinition definition, const CodeColorScheme& scheme) : QSyntaxHighlighter(document), m_definition(std::move(definition)) {
    m_commentFormat = scheme.format(HighlightRole::Comment);

    for (const auto& pattern : LanguageRegistry::patternsBeforeKeywords()) {
        m_rules.append({QRegularExpression(pattern.pattern), scheme.format(pattern.role)});
    }

    const QStringList controlFlow = CodeSyntaxHighlighterHelper::keywordsIn(m_definition.keywords, LanguageRegistry::controlFlowKeywords());
    const QStringList primitiveTypes = CodeSyntaxHighlighterHelper::keywordsIn(m_definition.keywords, LanguageRegistry::primitiveTypeKeywords());
    const QStringList plain = CodeSyntaxHighlighterHelper::keywordsOutside(m_definition.keywords, LanguageRegistry::controlFlowKeywords(), LanguageRegistry::primitiveTypeKeywords());
    const QVector<QPair<QStringList, HighlightRole>> keywordGroups{{plain, HighlightRole::Keyword}, {controlFlow, HighlightRole::ControlFlow}, {primitiveTypes, HighlightRole::PrimitiveType}};

    for (const auto& group : keywordGroups) {
        if (!group.first.isEmpty()) {
            m_rules.append({QRegularExpression(CodeSyntaxHighlighterHelper::keywordPattern(group.first)), scheme.format(group.second)});
        }
    }

    for (const auto& pattern : LanguageRegistry::patternsAfterKeywords()) {
        m_rules.append({QRegularExpression(pattern.pattern), scheme.format(pattern.role)});
    }

    for (const auto& pattern : m_definition.patterns) {
        m_rules.append({QRegularExpression(pattern.pattern), scheme.format(pattern.role)});
    }

    const QMap<QString, HighlightRole>& roles = LanguageRegistry::semanticRoles();

    for (auto entry = roles.constBegin(); entry != roles.constEnd(); ++entry) {
        m_semanticFormats.insert(entry.key(), scheme.format(entry.value()));
    }

    if (!m_definition.lineComment.isEmpty()) {
        m_rules.append({QRegularExpression(QStringLiteral("%1.*$").arg(QRegularExpression::escape(m_definition.lineComment))), m_commentFormat});
    }
}

// The server knows what a name really is, so its token wins over the pattern that only guessed from the shape of the text.
// Repainting the whole document on every answer costs the file, so only the lines whose tokens really changed are invalidated.
void CodeSyntaxHighlighter::setSemanticTokens(const QVector<SemanticToken>& tokens) {
    QHash<int, QVector<SemanticToken>> updated;

    for (const auto& token : tokens) {
        if (m_semanticFormats.contains(token.type)) {
            updated[token.line].append(token);
        }
    }

    QSet<int> changed;

    for (auto entry = updated.constBegin(); entry != updated.constEnd(); ++entry) {
        if (m_semanticTokens.value(entry.key()) != entry.value()) {
            changed.insert(entry.key());
        }
    }

    for (auto entry = m_semanticTokens.constBegin(); entry != m_semanticTokens.constEnd(); ++entry) {
        if (!updated.contains(entry.key())) {
            changed.insert(entry.key());
        }
    }

    if (changed.isEmpty()) {
        return;
    }

    m_semanticTokens = std::move(updated);

    // Invalidating one line at a time stops paying off once most of them changed, which is what the first answer for a file does.
    if (changed.size() * LanguageRegistry::limits().partialRepaintDivisor >= document()->blockCount()) {
        rehighlight();
        return;
    }

    for (const int line : changed) {
        const QTextBlock block = document()->findBlockByNumber(line);
        if (block.isValid()) {
            rehighlightBlock(block);
        }
    }
}

// A single line longer than the declared bound is generated content, and running every pattern over it costs more than the colors are worth.
void CodeSyntaxHighlighter::highlightBlock(const QString& text) {
    if (text.size() <= LanguageRegistry::limits().maximumHighlightedLineLength) {
        for (const auto& rule : m_rules) {
            applyRule(text, rule);
        }
        applyBlockComments(text);
    }

    for (const auto& token : m_semanticTokens.value(currentBlock().blockNumber())) {
        if (token.startCharacter >= 0 && token.startCharacter + token.length <= text.size()) {
            setFormat(token.startCharacter, token.length, m_semanticFormats.value(token.type));
        }
    }
}

void CodeSyntaxHighlighter::applyRule(const QString& text, const Rule& rule) {
    auto match = rule.expression.globalMatch(text);

    while (match.hasNext()) {
        const auto current = match.next();
        setFormat(static_cast<int>(current.capturedStart()), static_cast<int>(current.capturedLength()), rule.format);
    }
}

void CodeSyntaxHighlighter::applyBlockComments(const QString& text) {
    if (m_definition.blockCommentStart.isEmpty() || m_definition.blockCommentEnd.isEmpty()) {
        return;
    }

    setCurrentBlockState(0);
    qsizetype start = previousBlockState() == 1 ? 0 : text.indexOf(m_definition.blockCommentStart);

    while (start >= 0) {
        const qsizetype end = text.indexOf(m_definition.blockCommentEnd, start + m_definition.blockCommentStart.size());
        if (end < 0) {
            setCurrentBlockState(1);
            setFormat(static_cast<int>(start), static_cast<int>(text.size() - start), m_commentFormat);
            return;
        }
        const qsizetype length = end - start + m_definition.blockCommentEnd.size();
        setFormat(static_cast<int>(start), static_cast<int>(length), m_commentFormat);
        start = text.indexOf(m_definition.blockCommentStart, start + length);
    }
}

} // namespace slotdeck::plugins::codeeditor
