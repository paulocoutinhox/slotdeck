#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace slotdeck::domain {

struct SlotLayoutState final {
    QString presetId{"1-single"};
    int slotCount{1};
    QVector<std::optional<QString>> slotAssignments{1};
    QVector<QString> shelf;
};

struct LayoutPreset final {
    QString id;
    QString name;
    int slotCount{};
    int columns{};
    int rows{};
};

} // namespace slotdeck::domain
