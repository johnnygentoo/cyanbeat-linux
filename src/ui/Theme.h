// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QIcon>
#include <QObject>

class QAbstractButton;
class QAction;
class QApplication;
class QLabel;

namespace Theme {

// JohnnyCyan palette.
inline const QColor Cyan{0x15, 0xF4, 0xEE};
inline const QColor HappyNavy{0x1E, 0x2E, 0x3E};
inline const QColor AvionTeal{0x2A, 0x4A, 0x5A};
inline const QColor DarkAnchor{0x09, 0x10, 0x18};
inline const QColor LiftedNavy{0x26, 0x3A, 0x4D};
inline const QColor Magenta{0xFF, 0x2E, 0xD1};
inline const QColor Violet{0xA0, 0x6B, 0xFF};

// The live accents. Vaporwave swaps the poles like the Android app:
// cyan → magenta, and the second colour (violet) → cyan.
QColor accent(int alpha = 255);
QColor accent2(int alpha = 255);
bool vapor();

// Registers the bundled Konso faces, sets Fusion + palette + stylesheet.
void apply(QApplication &app);
void setVapor(bool on);

// Icons from :/icons/<name>.svg drawn in the current accent. The bind* helpers
// keep a widget's icon in step when the accent changes.
QIcon icon(const QString &name);
void bindIcon(QAbstractButton *button, const QString &name);
void bindIcon(QAction *action, const QString &name);
void bindIcon(QLabel *label, const QString &name, int px);

// Emits changed() after a reskin.
class Notifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
Q_SIGNALS:
    void changed();
};
Notifier *notifier();

} // namespace Theme
