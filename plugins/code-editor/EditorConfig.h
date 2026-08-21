#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace slotdeck::plugins::codeeditor {

enum class IndentStyle { Tab, Space };

enum class LineEnding { Lf, Crlf, Cr };

enum class TextCharset { Utf8, Utf8Bom, Utf16Le, Utf16Be, Latin1 };

[[nodiscard]] QString textCharsetName(TextCharset charset);
[[nodiscard]] std::optional<TextCharset> parseTextCharset(const QString& name);
[[nodiscard]] const QVector<TextCharset>& textCharsets();

struct EditorConfigProperties final {
    std::optional<IndentStyle> indentStyle;
    std::optional<int> indentSize;
    std::optional<int> tabWidth;
    std::optional<LineEnding> lineEnding;
    std::optional<TextCharset> charset;
    std::optional<bool> trimTrailingWhitespace;
    std::optional<bool> insertFinalNewline;
    std::optional<int> maximumLineLength;
    QStringList unsupportedCharsets;
};

struct EditorConfigFile final {
    QString directoryPath;
    QString content;
};

[[nodiscard]] QStringList editorConfigSearchPaths(const QString& filePath, const QString& rootPath);
[[nodiscard]] EditorConfigProperties resolveEditorConfig(const QString& filePath, const QVector<EditorConfigFile>& files);
[[nodiscard]] bool editorConfigSectionMatches(const QString& pattern, const QString& directoryPath, const QString& filePath);
[[nodiscard]] int resolvedIndentWidth(const EditorConfigProperties& properties);

} // namespace slotdeck::plugins::codeeditor
