#include "CodeSyntaxHighlighter.h"

#include "ui/Theme.h"

#include <QRegularExpression>
#include <QTextBlock>

#include <utility>

namespace slotdeck::plugins::codeeditor {

class CodeSyntaxHighlighterHelper final {
  public:
    static QTextCharFormat roleFormat(HighlightRole role, const ui::Theme& theme);
    static QTextCharFormat makeFormat(const QColor& color, QFont::Weight weight = QFont::Normal, bool italic = false);
    static QString keywordPattern(const QStringList& keywords);
};

QTextCharFormat CodeSyntaxHighlighterHelper::makeFormat(const QColor& color, QFont::Weight weight, bool italic) {
    QTextCharFormat value;
    value.setForeground(color);
    value.setFontWeight(weight);
    value.setFontItalic(italic);
    return value;
}

QString CodeSyntaxHighlighterHelper::keywordPattern(const QStringList& keywords) {
    QStringList escaped;
    escaped.reserve(keywords.size());

    for (const auto& keyword : keywords) {
        escaped.append(QRegularExpression::escape(keyword));
    }

    return QStringLiteral("\\b(?:%1)\\b").arg(escaped.join(QLatin1Char('|')));
}

QTextCharFormat CodeSyntaxHighlighterHelper::roleFormat(HighlightRole role, const ui::Theme& theme) {
    switch (role) {
    case HighlightRole::Keyword:
        return makeFormat(theme.color(ui::ThemeColor::Accent), QFont::DemiBold);
    case HighlightRole::Number:
        return makeFormat(theme.color(ui::ThemeColor::Warning));
    case HighlightRole::String:
        return makeFormat(theme.color(ui::ThemeColor::Success));
    case HighlightRole::Identifier:
        return makeFormat(theme.color(ui::ThemeColor::Text));
    case HighlightRole::Declaration:
        return makeFormat(theme.color(ui::ThemeColor::Text), QFont::DemiBold);
    case HighlightRole::Function:
        return makeFormat(theme.color(ui::ThemeColor::AccentHover), QFont::DemiBold);
    case HighlightRole::Comment:
        return makeFormat(theme.color(ui::ThemeColor::TextMuted), QFont::Normal, true);
    case HighlightRole::Heading:
        return makeFormat(theme.color(ui::ThemeColor::Accent), QFont::Bold);
    case HighlightRole::Emphasis:
        return makeFormat(theme.color(ui::ThemeColor::Text), QFont::Bold);
    case HighlightRole::Markup:
        return makeFormat(theme.color(ui::ThemeColor::Accent));
    }

    Q_UNREACHABLE_RETURN({});
}

CodeSyntaxHighlighter::CodeSyntaxHighlighter(QTextDocument* document, LanguageDefinition definition, const ui::Theme& theme) : QSyntaxHighlighter(document), m_definition(std::move(definition)) {
    m_commentFormat = CodeSyntaxHighlighterHelper::roleFormat(HighlightRole::Comment, theme);

    if (!m_definition.keywords.isEmpty()) {
        m_rules.append({QRegularExpression(CodeSyntaxHighlighterHelper::keywordPattern(m_definition.keywords)), CodeSyntaxHighlighterHelper::roleFormat(HighlightRole::Keyword, theme)});
    }

    for (const auto& pattern : LanguageRegistry::commonPatterns()) {
        m_rules.append({QRegularExpression(pattern.pattern), CodeSyntaxHighlighterHelper::roleFormat(pattern.role, theme)});
    }

    for (const auto& pattern : m_definition.patterns) {
        m_rules.append({QRegularExpression(pattern.pattern), CodeSyntaxHighlighterHelper::roleFormat(pattern.role, theme)});
    }

    const QMap<QString, HighlightRole>& roles = LanguageRegistry::semanticRoles();

    for (auto entry = roles.constBegin(); entry != roles.constEnd(); ++entry) {
        m_semanticFormats.insert(entry.key(), CodeSyntaxHighlighterHelper::roleFormat(entry.value(), theme));
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
