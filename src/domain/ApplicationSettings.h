#pragma once

#include <QByteArray>
#include <QString>

namespace slotdeck::domain {

struct ApplicationSettings final {
    QByteArray windowGeometry;
    QString language;
    QString themeId{QStringLiteral("green")};
};

} // namespace slotdeck::domain
