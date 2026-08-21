#include "FileWatch.h"

#include <QFileInfo>

namespace slotdeck::plugins::codeeditor {

// macOS reports a watched path only once, so every notification rearms the watch it came from and a second external change is still seen.
void FileWatch::rearm(QFileSystemWatcher& watcher, const QString& path) {
    watcher.removePath(path);
    if (QFileInfo::exists(path)) {
        watcher.addPath(path);
    }
}

} // namespace slotdeck::plugins::codeeditor
