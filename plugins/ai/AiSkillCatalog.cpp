#include "AiSkillCatalog.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

#include <utility>

namespace slotdeck::plugins::ai {

constexpr int maximumSkillCount = 100;
constexpr int maximumRootEntries = 500;
constexpr qint64 maximumSkillBytes = 1 << 16;

// One scan walks the roots in order and the entries of each root in order, so a repeated name always resolves the same way.
struct AiSkillCatalog::Scan final {
    QString workdir;
    QStringList roots;
    int rootIndex{0};
    QVector<filesystem::DirectoryEntry> entries;
    int entryIndex{0};
    QVector<SkillDescriptor> found;
    Completion completion;
};

class AiSkillCatalogHelper final {
  public:
    static QString frontMatterValue(const QString& content, const QString& key);
    static bool claimed(const QVector<SkillDescriptor>& found, const QString& name);
};

QString AiSkillCatalogHelper::frontMatterValue(const QString& content, const QString& key) {
    if (!content.startsWith(QStringLiteral("---"))) {
        return {};
    }

    const qsizetype closing = content.indexOf(QStringLiteral("\n---"), 3);

    if (closing < 0) {
        return {};
    }

    const QString frontMatter = content.mid(3, closing - 3);

    for (const auto& line : frontMatter.split(QLatin1Char('\n'))) {
        const qsizetype separator = line.indexOf(QLatin1Char(':'));
        if (separator < 0 || line.left(separator).trimmed() != key) {
            continue;
        }

        return line.mid(separator + 1).trimmed();
    }

    return {};
}

bool AiSkillCatalogHelper::claimed(const QVector<SkillDescriptor>& found, const QString& name) {
    // clang-format off
    return std::any_of(found.constBegin(), found.constEnd(), [&name](const SkillDescriptor& skill) { return skill.name.compare(name, Qt::CaseInsensitive) == 0; });
    // clang-format on
}

AiSkillCatalog::AiSkillCatalog(PluginHost& host, QObject* parent) : QObject(parent), m_host(host) {}

// The project roots come first because a workspace overrides what the machine offers, and the published names are all accepted.
QStringList AiSkillCatalog::roots(const QString& workdir) {
    QStringList roots;

    if (!workdir.isEmpty()) {
        const QDir project(workdir);
        for (const auto& location : {QStringLiteral(".claude/skills"), QStringLiteral(".agents/skills"), QStringLiteral(".skills"), QStringLiteral("skills")}) {
            roots.append(QDir::cleanPath(project.filePath(location)));
        }
    }

    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    if (!home.isEmpty()) {
        const QDir user(home);
        for (const auto& location : {QStringLiteral(".claude/skills"), QStringLiteral(".agents/skills")}) {
            roots.append(QDir::cleanPath(user.filePath(location)));
        }
    }

    return roots;
}

SkillDescriptor AiSkillCatalog::describe(const QString& name, const QString& path, const QString& root, const QString& content) {
    SkillDescriptor skill{name, {}, path, root};
    skill.name = AiSkillCatalogHelper::frontMatterValue(content, QStringLiteral("name"));
    skill.description = AiSkillCatalogHelper::frontMatterValue(content, QStringLiteral("description"));

    if (skill.name.isEmpty()) {
        skill.name = name;
    }

    return skill;
}

// The catalog is read once per run, because the prompt and the tools ask for it on every turn and a disk is not answered that often.
void AiSkillCatalog::discover(const QString& workdir, const Completion& completion) {
    if (m_cacheValid && m_scannedWorkdir == workdir) {
        completion(m_cached);
        return;
    }

    auto scan = std::make_shared<Scan>();
    scan->workdir = workdir;
    scan->roots = roots(workdir);
    scan->completion = completion;
    scanRoot(scan);
}

void AiSkillCatalog::scanRoot(const std::shared_ptr<Scan>& scan) {
    if (scan->rootIndex >= scan->roots.size() || scan->found.size() >= maximumSkillCount) {
        finish(scan);
        return;
    }

    const QString root = scan->roots.at(scan->rootIndex);
    auto future = m_host.listDirectory(root, maximumRootEntries);
    // clang-format off
    future.then(this, [this, scan](utils::Result<QVector<filesystem::DirectoryEntry>> listed) {
        scan->entries = listed.hasValue() ? listed.value() : QVector<filesystem::DirectoryEntry>{};
        scan->entryIndex = 0;
        scanEntry(scan);
    });
    // clang-format on
}

void AiSkillCatalog::scanEntry(const std::shared_ptr<Scan>& scan) {
    if (scan->entryIndex >= scan->entries.size() || scan->found.size() >= maximumSkillCount) {
        ++scan->rootIndex;
        scanRoot(scan);
        return;
    }

    const QString root = scan->roots.at(scan->rootIndex);
    const filesystem::DirectoryEntry entry = scan->entries.at(scan->entryIndex);
    ++scan->entryIndex;

    // Both published layouts are accepted, the directory holding its instructions and the single file that is the skill.
    const bool flat = !entry.directory && entry.name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive);

    if (!entry.directory && !flat) {
        scanEntry(scan);
        return;
    }

    const QString name = flat ? entry.name.chopped(3) : entry.name;
    const QString path = flat ? QDir(root).filePath(entry.name) : QDir(root).filePath(entry.name + QStringLiteral("/SKILL.md"));

    if (AiSkillCatalogHelper::claimed(scan->found, name)) {
        scanEntry(scan);
        return;
    }

    auto future = m_host.readFile(path, maximumSkillBytes);
    // clang-format off
    future.then(this, [this, scan, name, path, root](utils::Result<QByteArray> content) {
        if (!content.hasValue()) {
            scanEntry(scan);
            return;
        }
        const SkillDescriptor skill = describe(name, path, root, QString::fromUtf8(content.value()));
        if (skill.description.isEmpty()) {
            // A front matter that declares nothing about the skill is skipped by name, because the catalog it belongs to still answers.
            m_host.log(LogLevel::Warning, QStringLiteral("ai.skills"), QStringLiteral("A skill declares no description and was skipped"), {{QStringLiteral("detail"), path}});
            scanEntry(scan);
            return;
        }
        if (!AiSkillCatalogHelper::claimed(scan->found, skill.name)) {
            scan->found.append(skill);
        }
        scanEntry(scan);
    });
    // clang-format on
}

void AiSkillCatalog::finish(const std::shared_ptr<Scan>& scan) {
    m_cached = scan->found;
    m_scannedWorkdir = scan->workdir;
    m_cacheValid = true;
    scan->completion(m_cached);
}

} // namespace slotdeck::plugins::ai
