#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace slotdeck::plugins::ai {

// A tag the system prompt of an agent may carry, replaced by what the run knows when the turn is built.
struct PromptTagDescriptor final {
    QString name;
    QString descriptionKey;
};

[[nodiscard]] const QVector<PromptTagDescriptor>& promptTags();
[[nodiscard]] QStringList unknownPromptTags(const QString& prompt);
[[nodiscard]] QString renderPrompt(const QString& prompt, const QHash<QString, QString>& values);

} // namespace slotdeck::plugins::ai
