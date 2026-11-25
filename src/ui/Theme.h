// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>

class QApplication;

namespace Theme {

// JohnnyCyan palette.
inline const QColor Cyan{0x15, 0xF4, 0xEE};
inline const QColor HappyNavy{0x1E, 0x2E, 0x3E};
inline const QColor AvionTeal{0x2A, 0x4A, 0x5A};
inline const QColor DarkAnchor{0x09, 0x10, 0x18};
inline const QColor LiftedNavy{0x26, 0x3A, 0x4D};
inline const QColor Magenta{0xFF, 0x2E, 0xD1};

// Registers the bundled Konso faces, sets Fusion + palette + stylesheet.
void apply(QApplication &app);

} // namespace Theme
