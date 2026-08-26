#pragma once

#include "CodeColorScheme.h"
#include "LanguageRegistry.h"
#include "LanguageServerClient.h"

#include <QHash>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace slotdeck::plugins::codeeditor {

class CodeSyntaxHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT

  public:
    CodeSyntaxHighlighter(QTextDocument* document, LanguageDefinition definition, const CodeColorScheme& scheme);

    void setSemanticTokens(const QVector<SemanticToken>& tokens);

  protected:
    void highlightBlock(const QString& text) override;

  private:
    struct Rule final {
        QRegularExpression expression;
        QTextCharFormat format;
    };

    void applyRule(const QString& text, const Rule& rule);
    void applyBlockComments(const QString& text);

    LanguageDefinition m_definition;
    QVector<Rule> m_rules;
    QTextCharFormat m_commentFormat;
    QHash<QString, QTextCharFormat> m_semanticFormats;
    QHash<int, QVector<SemanticToken>> m_semanticTokens;
};

} // namespace slotdeck::plugins::codeeditor
