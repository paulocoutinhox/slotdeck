#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVariant>

namespace slotdeck::persistence {

// A text column declared NOT NULL rejects a null QString, which Qt binds as a SQL NULL rather than an empty value.
inline QString storedText(const QString& value) {
    return value.isNull() ? QString::fromLatin1("") : value;
}

inline QString storedTimestamp(const QDateTime& value) {
    return value.toUTC().toString(Qt::ISODateWithMs);
}

inline QDateTime parseStoredTimestamp(const QVariant& value) {
    const QString text = value.toString();
    return text.isEmpty() ? QDateTime{} : QDateTime::fromString(text, Qt::ISODateWithMs);
}

inline bool validStoredTimestamp(const QDateTime& value) {
    return value.isValid() && value.timeSpec() == Qt::UTC;
}

inline bool readStoredInteger(const QVariant& value, qint64& output) {
    const int type = value.metaType().id();

    if (type != QMetaType::Int && type != QMetaType::UInt && type != QMetaType::LongLong && type != QMetaType::ULongLong) {
        return false;
    }

    bool valid = false;
    output = value.toLongLong(&valid);
    return valid;
}

} // namespace slotdeck::persistence
