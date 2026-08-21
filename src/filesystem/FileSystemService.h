#pragma once

#include "utils/Result.h"

#include <QByteArray>
#include <QFuture>
#include <QThreadPool>
#include <QVector>

namespace slotdeck::filesystem {

// What a directory holds is decided by whoever filled it, so a listing is answered away from the thread that draws and bounded by what the caller asked for.
struct DirectoryEntry final {
    QString name;
    bool directory{false};
    qint64 size{0};

    [[nodiscard]] bool operator==(const DirectoryEntry& other) const = default;
};

class FileSystemService final {
  public:
    FileSystemService();
    ~FileSystemService();

    [[nodiscard]] QFuture<utils::Result<QByteArray>> readFile(const QString& path, qint64 maximumBytes);
    [[nodiscard]] QFuture<utils::Result<QVector<DirectoryEntry>>> listDirectory(const QString& path, int maximumEntries);
    [[nodiscard]] QFuture<utils::Result<void>> writeFile(const QString& path, const QByteArray& content);
    [[nodiscard]] QFuture<utils::Result<void>> createFile(const QString& path);
    [[nodiscard]] QFuture<utils::Result<void>> createDirectory(const QString& path);
    [[nodiscard]] QFuture<utils::Result<void>> movePath(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<utils::Result<void>> copyFile(const QString& sourcePath, const QString& destinationPath);
    [[nodiscard]] QFuture<utils::Result<void>> removeFile(const QString& path);
    [[nodiscard]] QFuture<utils::Result<void>> removeDirectory(const QString& path);
    void drain();

  private:
    QThreadPool m_pool;
};

} // namespace slotdeck::filesystem
