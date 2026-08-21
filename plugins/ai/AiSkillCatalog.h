#pragma once

#include "plugins/PluginInterface.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>

namespace slotdeck::plugins::ai {

// A skill is discovered where the published conventions put it, and the first root that declares a name owns it.
struct SkillDescriptor final {
    QString name;
    QString description;
    QString path;
    QString root;

    [[nodiscard]] bool operator==(const SkillDescriptor& other) const = default;
};

class AiSkillCatalog final : public QObject {
    Q_OBJECT

  public:
    using Completion = std::function<void(const QVector<SkillDescriptor>&)>;

    explicit AiSkillCatalog(PluginHost& host, QObject* parent = nullptr);

    void discover(const QString& workdir, const Completion& completion);

    // The roots are the published ones, in the order that decides which of them owns a repeated name.
    [[nodiscard]] static QStringList roots(const QString& workdir);
    [[nodiscard]] static SkillDescriptor describe(const QString& name, const QString& path, const QString& root, const QString& content);

  private:
    struct Scan;

    void scanRoot(const std::shared_ptr<Scan>& scan);
    void scanEntry(const std::shared_ptr<Scan>& scan);
    void finish(const std::shared_ptr<Scan>& scan);

    PluginHost& m_host;
    QString m_scannedWorkdir;
    QVector<SkillDescriptor> m_cached;
    bool m_cacheValid{false};
};

} // namespace slotdeck::plugins::ai
