#pragma once

#include <QString>

namespace slotdeck::ui {

inline constexpr auto sessionDragMimeType = "application/x-slotdeck-terminal-session-id";

enum class SessionDropTarget { None, Slot, Shelf };

struct SessionDropDestination final {
    SessionDropTarget target{SessionDropTarget::None};
    int slotIndex{-1};
};

class SessionDragSource {
  public:
    virtual ~SessionDragSource() = default;

    [[nodiscard]] virtual QString draggedSessionId() const = 0;
    virtual void setDropDestination(SessionDropDestination destination) = 0;
};

} // namespace slotdeck::ui
