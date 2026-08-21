#pragma once

#include <QFileSystemWatcher>
#include <QString>

namespace slotdeck::plugins::codeeditor {

class FileWatch final {
  public:
    static void rearm(QFileSystemWatcher& watcher, const QString& path);
};

} // namespace slotdeck::plugins::codeeditor
