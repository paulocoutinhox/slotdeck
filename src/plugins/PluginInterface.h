#pragma once

#include "filesystem/FileSystemService.h"
#include "persistence/PluginDatabase.h"
#include "utils/Result.h"

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtPlugin>

#include <cmath>
#include <functional>
#include <limits>
#include <tuple>
#include <utility>

class QWidget;

namespace slotdeck::ui {
class Theme;
}

namespace slotdeck::plugins {

// Consumed payloads accept an integer only when the JSON value represents it exactly.
inline bool readJsonInteger(const QJsonValue& value, qint64& output) {
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return false;
    }

    constexpr qint64 invalid = std::numeric_limits<qint64>::min();
    output = value.toInteger(invalid);
    return output != invalid && value.toDouble() == static_cast<double>(output);
}

// Consumed message payloads are rejected unless they carry exactly the fields their topic defines.
// A settings document accepts a missing key as the declared default and rejects a key nobody declared, because a value the owner does not know cannot be honoured.
inline bool hasKnownKeys(const QJsonObject& document, const QSet<QString>& known) {
    for (auto entry = document.constBegin(); entry != document.constEnd(); ++entry) {
        if (!known.contains(entry.key())) {
            return false;
        }
    }
    return true;
}

// A value the document carries must have the declared type, because an invalid value is a failure and never a silent default.
inline bool readSettingsBool(const QJsonObject& document, const QString& key, bool& value) {
    if (!document.contains(key)) {
        return true;
    }
    if (!document.value(key).isBool()) {
        return false;
    }
    value = document.value(key).toBool();
    return true;
}

inline bool readSettingsInteger(const QJsonObject& document, const QString& key, int& value) {
    if (!document.contains(key)) {
        return true;
    }
    qint64 stored = 0;
    if (!readJsonInteger(document.value(key), stored) || stored < std::numeric_limits<int>::min() || stored > std::numeric_limits<int>::max()) {
        return false;
    }
    value = static_cast<int>(stored);
    return true;
}

inline bool readSettingsText(const QJsonObject& document, const QString& key, QString& value) {
    if (!document.contains(key)) {
        return true;
    }
    if (!document.value(key).isString()) {
        return false;
    }
    value = document.value(key).toString();
    return true;
}

inline bool readSettingsObject(const QJsonObject& document, const QString& key, QJsonObject& value) {
    if (!document.contains(key)) {
        return true;
    }
    if (!document.value(key).isObject()) {
        return false;
    }
    value = document.value(key).toObject();
    return true;
}

inline bool readSettingsObjectList(const QJsonObject& document, const QString& key, QVector<QJsonObject>& value) {
    if (!document.contains(key)) {
        return true;
    }
    if (!document.value(key).isArray()) {
        return false;
    }

    QVector<QJsonObject> entries;
    for (const auto& entry : document.value(key).toArray()) {
        if (!entry.isObject()) {
            return false;
        }
        entries.append(entry.toObject());
    }
    value = entries;
    return true;
}

inline bool readSettingsTextList(const QJsonObject& document, const QString& key, QStringList& value) {
    if (!document.contains(key)) {
        return true;
    }
    if (!document.value(key).isArray()) {
        return false;
    }

    QStringList entries;
    for (const auto& entry : document.value(key).toArray()) {
        if (!entry.isString()) {
            return false;
        }
        entries.append(entry.toString());
    }
    value = entries;
    return true;
}

// A settings document is read whole and every value it does not carry, or carries in a shape the owner cannot use, stays the declared default.
class SettingsReader final {
  public:
    explicit SettingsReader(QJsonObject document) : m_document(std::move(document)) {}

    void readBool(const QString& key, bool& value) {
        std::ignore = readSettingsBool(m_document, key, value);
    }
    void readInteger(const QString& key, int& value) {
        std::ignore = readSettingsInteger(m_document, key, value);
    }
    void readText(const QString& key, QString& value) {
        std::ignore = readSettingsText(m_document, key, value);
    }
    void readObject(const QString& key, QJsonObject& value) {
        std::ignore = readSettingsObject(m_document, key, value);
    }
    void readObjectList(const QString& key, QVector<QJsonObject>& value) {
        std::ignore = readSettingsObjectList(m_document, key, value);
    }
    void readTextList(const QString& key, QStringList& value) {
        std::ignore = readSettingsTextList(m_document, key, value);
    }

  private:
    QJsonObject m_document;
};

inline bool hasExactKeys(const QJsonObject& object, const QSet<QString>& expected) {
    QSet<QString> actual;
    for (auto entry = object.constBegin(); entry != object.constEnd(); ++entry) {
        actual.insert(entry.key());
    }
    return actual == expected;
}

// The core broadcasts this topic so every surface steps its own content font size.
inline constexpr auto contentFontStepTopic = "slotdeck.appearance.font-step";

enum class ContentFontStep { Increase, Decrease, Reset };

inline bool parseContentFontStep(const QJsonObject& payload, ContentFontStep& output) {
    if (!hasExactKeys(payload, {QStringLiteral("step")}) || !payload.value(QStringLiteral("step")).isString()) {
        return false;
    }

    const QString step = payload.value(QStringLiteral("step")).toString();
    if (step == QStringLiteral("increase")) {
        output = ContentFontStep::Increase;
        return true;
    }
    if (step == QStringLiteral("decrease")) {
        output = ContentFontStep::Decrease;
        return true;
    }
    if (step == QStringLiteral("reset")) {
        output = ContentFontStep::Reset;
        return true;
    }
    return false;
}

enum class NavigationPlacement { Primary, Secondary };
// A destination declares where it belongs in the bar, because discovering the plugin libraries in the order the filesystem lists them is not an order anyone chose.
enum class NavigationOrder { Board = 0, Terminal = 100, Browser = 200, Editor = 300, Server = 400, Logs = 500, System = 600, Support = 1000 };
enum class LogLevel { Debug, Info, Warning, Error };
enum class AlertSeverity { Information, Success, Warning, Error };

struct NavigationItem final {
    QString id;
    QString titleKey;
    QIcon icon;
    NavigationPlacement placement{NavigationPlacement::Primary};
    NavigationOrder order{NavigationOrder::Board};
};

struct SettingsSection final {
    QString id;
    QString titleKey;
    QStringList searchKeys;
};

struct SettingsGroup final {
    QString id;
    QString titleKey;
    QVector<SettingsSection> sections;
};

using TranslationEntries = QHash<QString, QString>;
using TranslationCatalog = QHash<QString, TranslationEntries>;
using PluginReply = std::function<void(utils::Result<QJsonObject>)>;

class PluginHost {
  public:
    virtual ~PluginHost() = default;

    [[nodiscard]] virtual QString translate(const QString& key) const = 0;
    [[nodiscard]] virtual const ui::Theme& theme() const = 0;
    [[nodiscard]] virtual bool pluginAvailable(const QString& pluginId) const = 0;
    [[nodiscard]] virtual const QString& applicationDataPath() const = 0;
    [[nodiscard]] virtual QJsonObject settings() const = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> saveSettings(const QJsonObject& document) = 0;
    [[nodiscard]] virtual utils::Result<void> migrateDatabase(const QVector<persistence::DatabaseMigration>& migrations) = 0;
    [[nodiscard]] virtual utils::Result<void> executeBootstrapDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) = 0;
    [[nodiscard]] virtual utils::Result<persistence::DatabaseRows> queryBootstrapDatabase(const QString& statement, const QVariantList& bindings = {}) const = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> executeDatabase(const QString& statement, const QVariantList& bindings = {}) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> executeDatabaseTransaction(const QVector<persistence::DatabaseStatement>& statements) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<persistence::DatabaseRows>> queryDatabase(const QString& statement, const QVariantList& bindings = {}) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<QVector<filesystem::DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> writeFile(const QString& path, const QByteArray& content) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> createFile(const QString& path) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> createDirectory(const QString& path) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> movePath(const QString& sourcePath, const QString& destinationPath) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> removeFile(const QString& path) = 0;
    [[nodiscard]] virtual QFuture<utils::Result<void>> removeDirectory(const QString& path) = 0;
    [[nodiscard]] virtual bool confirm(QWidget* parent, const QString& title, const QString& message, const QString& detail, const QString& action, bool destructive) const = 0;
    virtual void request(const QString& targetPluginId, const QString& topic, const QJsonObject& payload, QObject& callbackContext, PluginReply reply) = 0;
    virtual void publish(const QString& topic, const QJsonObject& payload) = 0;
    virtual void log(LogLevel level, const QString& category, const QString& message, const QJsonObject& details = {}) = 0;
    virtual void notify(const QString& title, const QString& message, AlertSeverity severity) = 0;
    // A plugin reveals only a destination it declares itself, so the shell shows the view that answered the request.
    virtual void showNavigation(const QString& navigationId) = 0;
};

class PluginInterface {
  public:
    virtual ~PluginInterface() = default;

    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual QString titleKey() const = 0;
    [[nodiscard]] virtual QStringList dependencies() const = 0;
    [[nodiscard]] virtual int databaseSchemaVersion() const = 0;
    [[nodiscard]] virtual TranslationCatalog translations() const = 0;
    [[nodiscard]] virtual QString styleSheet(const ui::Theme& theme) const = 0;
    [[nodiscard]] virtual QVector<NavigationItem> navigationItems(const ui::Theme& theme) const = 0;
    [[nodiscard]] virtual QVector<SettingsGroup> settingsGroups() const = 0;
    [[nodiscard]] virtual utils::Result<void> initialize(PluginHost& host) = 0;
    [[nodiscard]] virtual QWidget* createNavigationView(const QString& itemId, QWidget* parent) = 0;
    [[nodiscard]] virtual QWidget* createSettingsSection(const QString& groupId, const QString& sectionId, QWidget* parent) = 0;
    virtual void handleRequest(const QString& senderPluginId, const QString& topic, const QJsonObject& payload, PluginReply reply) = 0;
    virtual void handleEvent(const QString& senderPluginId, const QString& topic, const QJsonObject& payload) = 0;
    virtual void shutdown() = 0;
};

} // namespace slotdeck::plugins

#define SlotDeckPluginInterface_iid "dev.slotdeck.PluginInterface/6.0"
Q_DECLARE_INTERFACE(slotdeck::plugins::PluginInterface, SlotDeckPluginInterface_iid)
