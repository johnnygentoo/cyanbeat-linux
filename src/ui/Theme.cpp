// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace Theme {

void apply(QApplication &app)
{
    for (const char *face : {"regular", "bold", "italic", "bolditalic"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/konso_%1.ttf").arg(QLatin1String(face)));

    QFont font(QStringLiteral("Konso"), 9);  // = the 12px the JohnnyCyan qt5ct theme uses
    font.setStyleHint(QFont::Monospace, QFont::PreferDefault);
    app.setFont(font);

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, HappyNavy);
    p.setColor(QPalette::WindowText, Cyan);
    p.setColor(QPalette::Base, LiftedNavy);
    p.setColor(QPalette::AlternateBase, QColor(0x20, 0x30, 0x40));
    p.setColor(QPalette::Text, Cyan);
    p.setColor(QPalette::Button, DarkAnchor);
    p.setColor(QPalette::ButtonText, Cyan);
    p.setColor(QPalette::Highlight, QColor(21, 244, 238, 90));
    p.setColor(QPalette::HighlightedText, Cyan);
    p.setColor(QPalette::ToolTipBase, DarkAnchor);
    p.setColor(QPalette::ToolTipText, Cyan);
    p.setColor(QPalette::PlaceholderText, QColor(21, 244, 238, 110));
    p.setColor(QPalette::Link, Cyan);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(21, 244, 238, 100));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(21, 244, 238, 100));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(21, 244, 238, 100));
    app.setPalette(p);

    QFile qss(QStringLiteral(":/style/johnnycyan.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

} // namespace Theme
