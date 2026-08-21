#pragma once

#include "domain/SlotLayoutState.h"

#include <QString>
#include <QVector>

namespace slotdeck::workspace {

class LayoutManager final {
  public:
    [[nodiscard]] static QVector<domain::LayoutPreset> presets();
    [[nodiscard]] static domain::LayoutPreset preset(const QString& presetId);
    [[nodiscard]] static bool contains(const domain::SlotLayoutState& layout, const QString& sessionId);
    [[nodiscard]] static int visibleSlotIndex(const domain::SlotLayoutState& layout, const QString& sessionId);

    static void changePreset(domain::SlotLayoutState& layout, const QString& presetId);
    static void assignToSlot(domain::SlotLayoutState& layout, const QString& sessionId, int slotIndex);
    static void moveToShelf(domain::SlotLayoutState& layout, const QString& sessionId, int shelfIndex = -1);
    static void remove(domain::SlotLayoutState& layout, const QString& sessionId);
};

} // namespace slotdeck::workspace
