#pragma once

#include <QKeySequence>

namespace slotdeck::ui::shortcuts {

// Qt answers the Quit standard key with the Exit media key outside macOS and with nothing at all under some platform themes, so the combination is declared like every other one here and Qt maps the control modifier to the native key.
inline QKeySequence quit() {
    return QKeySequence(Qt::CTRL | Qt::Key_Q);
}

// Zooming is one combination of the same shape for every direction, the plain equal, minus and zero keys, and Qt maps the control modifier to the native one.
inline QKeySequence increaseContentFont() {
    return QKeySequence(Qt::CTRL | Qt::Key_Equal);
}

inline QKeySequence decreaseContentFont() {
    return QKeySequence(Qt::CTRL | Qt::Key_Minus);
}

inline QKeySequence resetContentFont() {
    return QKeySequence(Qt::CTRL | Qt::Key_0);
}

} // namespace slotdeck::ui::shortcuts
