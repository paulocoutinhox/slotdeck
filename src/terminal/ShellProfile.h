#pragma once

#include <QString>
#include <QStringList>

namespace slotdeck::terminalcore {

struct ShellProfile final {
    QString id;
    QString name;
    QString executable;
    QStringList arguments;
};

[[nodiscard]] QString formatLocalPathsForShell(const ShellProfile& profile, const QStringList& paths);

class ShellProfileResolver final {
  public:
    [[nodiscard]] static ShellProfile systemDefault();
    [[nodiscard]] static QList<ShellProfile> availableProfiles();
};

} // namespace slotdeck::terminalcore
