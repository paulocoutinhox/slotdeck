#include "FileSystemService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtConcurrentRun>

#include <utility>

namespace slotdeck::filesystem {

class FileSystemServiceHelper final {
  public:
    static utils::Result<QString> normalizedPath(const QString& path);
    static utils::Result<void> validateNewEntry(const QString& path);
    static utils::Result<QString> preparedNewEntry(const QString& path);
    static utils::Result<void> writeFileNow(const QString& path, const QByteArray& content);
};

utils::Result<QString> FileSystemServiceHelper::normalizedPath(const QString& path) {
    if (!QDir::isAbsolutePath(path)) {
        return utils::Result<QString>::failure({"filesystem_path_invalid", "The filesystem path must be absolute", path});
    }

    const QString normalized = QDir::cleanPath(path);

    if (normalized == QDir::rootPath()) {
        return utils::Result<QString>::failure({"filesystem_path_unsafe", "The filesystem root cannot be modified", normalized});
    }

    return utils::Result<QString>::success(normalized);
}

utils::Result<void> FileSystemServiceHelper::validateNewEntry(const QString& path) {
    const QFileInfo entry(path);

    if (entry.exists() || entry.isSymLink()) {
        return utils::Result<void>::failure({"filesystem_destination_exists", "The destination already exists", path});
    }

    const QFileInfo parent(entry.absolutePath());

    if (!parent.isDir() || !parent.isWritable()) {
        return utils::Result<void>::failure({"filesystem_parent_unavailable", "The destination directory is unavailable", parent.absoluteFilePath()});
    }

    return utils::Result<void>::success();
}

utils::Result<QString> FileSystemServiceHelper::preparedNewEntry(const QString& path) {
    const auto normalized = normalizedPath(path);

    if (!normalized.hasValue()) {
        return normalized;
    }

    const auto validation = validateNewEntry(normalized.value());

    if (!validation.hasValue()) {
        return utils::Result<QString>::failure(validation.error());
    }

    return normalized;
}

utils::Result<void> FileSystemServiceHelper::writeFileNow(const QString& path, const QByteArray& content) {
    const auto normalized = normalizedPath(path);

    if (!normalized.hasValue()) {
        return utils::Result<void>::failure(normalized.error());
    }
    // A file that is not there yet is created, because refusing it would mean nothing could ever write a new one.
    const QFileInfo file(normalized.value());

    if (file.exists() && (!file.isFile() || file.isSymLink() || !file.isWritable())) {
        return utils::Result<void>::failure({"filesystem_file_unavailable", "The file is unavailable for writing", normalized.value()});
    }
    if (!file.exists() && !QFileInfo(file.absolutePath()).isDir() && !QDir().mkpath(file.absolutePath())) {
        return utils::Result<void>::failure({"filesystem_directory_missing", "The directory holding the file could not be created", file.absolutePath()});
    }

    QSaveFile output(normalized.value());
    output.setDirectWriteFallback(false);

    if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit()) {
        return utils::Result<void>::failure({"filesystem_write_failed", "The file could not be written atomically", output.errorString()});
    }

    return utils::Result<void>::success();
}

FileSystemService::FileSystemService() {
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

FileSystemService::~FileSystemService() {
    drain();
}

void FileSystemService::drain() {
    m_pool.waitForDone();
}

QFuture<utils::Result<QVector<DirectoryEntry>>> FileSystemService::listDirectory(const QString& path, int maximumEntries) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [path, maximumEntries]() {
        const auto normalized = FileSystemServiceHelper::normalizedPath(path);
        if (!normalized.hasValue()) {
            return utils::Result<QVector<DirectoryEntry>>::failure(normalized.error());
        }
        if (maximumEntries <= 0) {
            return utils::Result<QVector<DirectoryEntry>>::failure({"filesystem_list_limit_invalid", "The directory listing limit is invalid", QString::number(maximumEntries)});
        }

        const QFileInfo information(normalized.value());
        if (!information.exists()) {
            return utils::Result<QVector<DirectoryEntry>>::failure({"filesystem_directory_missing", "The directory does not exist", normalized.value()});
        }
        if (!information.isDir() || !information.isReadable()) {
            return utils::Result<QVector<DirectoryEntry>>::failure({"filesystem_directory_unavailable", "The directory is unavailable for reading", normalized.value()});
        }

        QVector<DirectoryEntry> entries;
        const QDir directory(normalized.value());
        for (const auto& entry : directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name)) {
            if (entries.size() >= maximumEntries) {
                break;
            }
            entries.append({entry.fileName(), entry.isDir(), entry.isDir() ? 0 : entry.size()});
        }
        return utils::Result<QVector<DirectoryEntry>>::success(std::move(entries));
    });
    // clang-format on
}

QFuture<utils::Result<QByteArray>> FileSystemService::readFile(const QString& path, qint64 maximumBytes) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [path, maximumBytes]() {
        const auto normalized = FileSystemServiceHelper::normalizedPath(path);
        if (!normalized.hasValue()) {
            return utils::Result<QByteArray>::failure(normalized.error());
        }
        if (maximumBytes <= 0) {
            return utils::Result<QByteArray>::failure({"filesystem_read_limit_invalid", "The file read limit is invalid", QString::number(maximumBytes)});
        }

        QFile file(normalized.value());
        const QFileInfo information(file);
        // A path that is simply not there is said to be missing, because whoever asked for it can act on that and not on a word like unavailable.
        if (!information.exists()) {
            return utils::Result<QByteArray>::failure({"filesystem_file_missing", "The file does not exist", normalized.value()});
        }
        if (!information.isFile() || information.isSymLink() || !information.isReadable()) {
            return utils::Result<QByteArray>::failure({"filesystem_file_unavailable", "The file is unavailable for reading", normalized.value()});
        }
        if (information.size() > maximumBytes) {
            return utils::Result<QByteArray>::failure({"filesystem_file_too_large", "The file exceeds the permitted read size", normalized.value()});
        }
        if (!file.open(QIODevice::ReadOnly)) {
            return utils::Result<QByteArray>::failure({"filesystem_read_failed", "The file could not be opened", file.errorString()});
        }
        const QByteArray content = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            return utils::Result<QByteArray>::failure({"filesystem_read_failed", "The file could not be read", file.errorString()});
        }
        return utils::Result<QByteArray>::success(content);
    });
    // clang-format on
}

QFuture<utils::Result<void>> FileSystemService::writeFile(const QString& path, const QByteArray& content) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [path, content]() { return FileSystemServiceHelper::writeFileNow(path, content); });
    // clang-format on
}

QFuture<utils::Result<void>> FileSystemService::createFile(const QString& path) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [path]() {
        const auto prepared = FileSystemServiceHelper::preparedNewEntry(path);
        if (!prepared.hasValue()) {
            return utils::Result<void>::failure(prepared.error());
        }

        QSaveFile output(prepared.value());
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) || !output.commit()) {
            return utils::Result<void>::failure({"filesystem_create_file_failed", "The file could not be created atomically", output.errorString()});
        }
        return utils::Result<void>::success();
    });
    // clang-format on
}

// Every missing level is created, because asking for one level at a time turns a nested directory into as many failed calls as it has levels.
QFuture<utils::Result<void>> FileSystemService::createDirectory(const QString& path) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [path]() {
        const auto normalized = FileSystemServiceHelper::normalizedPath(path);
        if (!normalized.hasValue()) {
            return utils::Result<void>::failure(normalized.error());
        }
        const QFileInfo entry(normalized.value());
        if (entry.exists() || entry.isSymLink()) {
            return utils::Result<void>::failure({"filesystem_destination_exists", "The destination already exists", normalized.value()});
        }
        if (!QDir().mkpath(normalized.value())) {
            return utils::Result<void>::failure({"filesystem_create_directory_failed", "The directory could not be created", normalized.value()});
        }
        return utils::Result<void>::success();
    });
    // clang-format on
}

QFuture<utils::Result<void>> FileSystemService::movePath(const QString& sourcePath, const QString& destinationPath) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [sourcePath, destinationPath]() {
        const auto source = FileSystemServiceHelper::normalizedPath(sourcePath);
        const auto destination = FileSystemServiceHelper::normalizedPath(destinationPath);
        if (!source.hasValue()) {
            return utils::Result<void>::failure(source.error());
        }
        if (!destination.hasValue()) {
            return utils::Result<void>::failure(destination.error());
        }
        const QFileInfo sourceInformation(source.value());
        if ((!sourceInformation.exists() && !sourceInformation.isSymLink()) || source.value() == destination.value()) {
            return utils::Result<void>::failure({"filesystem_source_unavailable", "The source path is unavailable", source.value()});
        }
        const auto validation = FileSystemServiceHelper::validateNewEntry(destination.value());
        if (!validation.hasValue()) {
            return validation;
        }
        if (sourceInformation.isDir() && !sourceInformation.isSymLink() && destination.value().startsWith(source.value() + QLatin1Char('/'))) {
            return utils::Result<void>::failure({"filesystem_move_invalid", "A directory cannot be moved inside itself", destination.value()});
        }
        if (!QDir().rename(source.value(), destination.value())) {
            return utils::Result<void>::failure({"filesystem_move_failed", "The path could not be moved", QStringLiteral("%1 -> %2").arg(source.value(), destination.value())});
        }
        return utils::Result<void>::success();
    });
    // clang-format on
}

QFuture<utils::Result<void>> FileSystemService::copyFile(const QString& sourcePath, const QString& destinationPath) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [sourcePath, destinationPath]() {
        const auto source = FileSystemServiceHelper::normalizedPath(sourcePath);
        const auto destination = FileSystemServiceHelper::normalizedPath(destinationPath);
        if (!source.hasValue()) {
            return utils::Result<void>::failure(source.error());
        }
        if (!destination.hasValue()) {
            return utils::Result<void>::failure(destination.error());
        }
        if (!QFileInfo(source.value()).isFile()) {
            return utils::Result<void>::failure({"filesystem_source_unavailable", "The source file is unavailable", source.value()});
        }
        const auto validation = FileSystemServiceHelper::validateNewEntry(destination.value());
        if (!validation.hasValue()) {
            return validation;
        }
        if (!QFile::copy(source.value(), destination.value())) {
            return utils::Result<void>::failure({"filesystem_copy_failed", "The file could not be copied", QStringLiteral("%1 -> %2").arg(source.value(), destination.value())});
        }
        return utils::Result<void>::success();
    });
    // clang-format on
}

QFuture<utils::Result<void>> FileSystemService::removeFile(const QString& path) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [path]() {
        const auto normalized = FileSystemServiceHelper::normalizedPath(path);
        if (!normalized.hasValue()) {
            return utils::Result<void>::failure(normalized.error());
        }
        const QFileInfo information(normalized.value());
        if ((!information.isFile() && !information.isSymLink()) || !QFile::remove(normalized.value())) {
            return utils::Result<void>::failure({"filesystem_remove_file_failed", "The file could not be removed", normalized.value()});
        }
        return utils::Result<void>::success();
    });
    // clang-format on
}

QFuture<utils::Result<void>> FileSystemService::removeDirectory(const QString& path) {
    // clang-format off
    return QtConcurrent::run(&m_pool, [path]() {
        const auto normalized = FileSystemServiceHelper::normalizedPath(path);
        if (!normalized.hasValue()) {
            return utils::Result<void>::failure(normalized.error());
        }
        const QFileInfo information(normalized.value());
        if (!information.isDir() || information.isSymLink() || !QDir(normalized.value()).removeRecursively()) {
            return utils::Result<void>::failure({"filesystem_remove_directory_failed", "The directory could not be removed", normalized.value()});
        }
        return utils::Result<void>::success();
    });
    // clang-format on
}

} // namespace slotdeck::filesystem
