// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QWidget>

namespace Theme {

namespace {
bool g_vapor = false;
QString g_qss;  // as shipped (cyan)
QHash<QString, QIcon> g_icons;
QList<QPair<QPointer<QAbstractButton>, QString>> g_buttons;
QList<QPair<QPointer<QAction>, QString>> g_actions;
struct LabelIcon {
    QPointer<QLabel> label;
    QString name;
    int px;
};
QList<LabelIcon> g_labels;

QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb).toUpper();
}

QString rgb(const QColor &c)
{
    return QStringLiteral("%1, %2, %3").arg(c.red()).arg(c.green()).arg(c.blue());
}

QPalette palette()
{
    QPalette p;
    p.setColor(QPalette::Window, HappyNavy);
    p.setColor(QPalette::WindowText, accent());
    p.setColor(QPalette::Base, LiftedNavy);
    p.setColor(QPalette::AlternateBase, QColor(0x20, 0x30, 0x40));
    p.setColor(QPalette::Text, accent());
    p.setColor(QPalette::Button, DarkAnchor);
    p.setColor(QPalette::ButtonText, accent());
    p.setColor(QPalette::Highlight, accent(90));
    p.setColor(QPalette::HighlightedText, accent());
    p.setColor(QPalette::ToolTipBase, DarkAnchor);
    p.setColor(QPalette::ToolTipText, accent());
    p.setColor(QPalette::PlaceholderText, accent(110));
    p.setColor(QPalette::Link, accent());
    p.setColor(QPalette::Disabled, QPalette::Text, accent(100));
    p.setColor(QPalette::Disabled, QPalette::WindowText, accent(100));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, accent(100));
    return p;
}

QString styleSheet()
{
    if (!g_vapor)
        return g_qss;
    // Swap the poles through placeholders so cyan→magenta and violet→cyan don't collide.
    QString s = g_qss;
    s.replace(hex(Cyan), QStringLiteral("@A1@"), Qt::CaseInsensitive);
    s.replace(rgb(Cyan), QStringLiteral("@A1RGB@"));
    s.replace(hex(Violet), QStringLiteral("@A2@"), Qt::CaseInsensitive);
    s.replace(rgb(Violet), QStringLiteral("@A2RGB@"));
    s.replace(QStringLiteral("@A1@"), hex(Magenta));
    s.replace(QStringLiteral("@A1RGB@"), rgb(Magenta));
    s.replace(QStringLiteral("@A2@"), hex(Cyan));
    s.replace(QStringLiteral("@A2RGB@"), rgb(Cyan));
    return s;
}

void refreshBound()
{
    for (const auto &[b, name] : std::as_const(g_buttons))
        if (b)
            b->setIcon(icon(name));
    for (const auto &[a, name] : std::as_const(g_actions))
        if (a)
            a->setIcon(icon(name));
    for (const LabelIcon &l : std::as_const(g_labels))
        if (l.label)
            l.label->setPixmap(icon(l.name).pixmap(l.px, l.px));
}
} // namespace

QColor accent(int alpha)
{
    QColor c = g_vapor ? Magenta : Cyan;
    c.setAlpha(alpha);
    return c;
}

QColor accent2(int alpha)
{
    QColor c = g_vapor ? Cyan : Violet;
    c.setAlpha(alpha);
    return c;
}

bool vapor()
{
    return g_vapor;
}

Notifier *notifier()
{
    static Notifier n;
    return &n;
}

QIcon icon(const QString &name)
{
    const QString key = name + (g_vapor ? QStringLiteral("@v") : QString());
    if (auto it = g_icons.constFind(key); it != g_icons.cend())
        return *it;
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    QIcon out;
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray svg = f.readAll();
        svg.replace(hex(Cyan).toLatin1(), hex(accent()).toLatin1());
        svg.replace(hex(Cyan).toLower().toLatin1(), hex(accent()).toLatin1());
        QSvgRenderer r(svg);
        for (int px : {16, 24, 32, 48, 64}) {
            QPixmap pm(px, px);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            r.render(&p);
            out.addPixmap(pm);
        }
    }
    g_icons.insert(key, out);
    return out;
}

void bindIcon(QAbstractButton *button, const QString &name)
{
    button->setIcon(icon(name));
    g_buttons.append({button, name});
}

void bindIcon(QAction *action, const QString &name)
{
    action->setIcon(icon(name));
    g_actions.append({action, name});
}

void bindIcon(QLabel *label, const QString &name, int px)
{
    label->setPixmap(icon(name).pixmap(px, px));
    g_labels.append({label, name, px});
}

void apply(QApplication &app)
{
    for (const char *face : {"regular", "bold", "italic", "bolditalic"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/konso_%1.ttf").arg(QLatin1String(face)));

    QFont font(QStringLiteral("Konso"), 9);  // = the 12px the JohnnyCyan qt5ct theme uses
    font.setStyleHint(QFont::Monospace, QFont::PreferDefault);
    app.setFont(font);
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(palette());
    QFile qss(QStringLiteral(":/style/johnnycyan.qss"));
    if (qss.open(QIODevice::ReadOnly))
        g_qss = QString::fromUtf8(qss.readAll());
    app.setStyleSheet(styleSheet());
}

void setVapor(bool on)
{
    if (g_vapor == on)
        return;
    g_vapor = on;
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        app->setPalette(palette());
        app->setStyleSheet(styleSheet());
    }
    refreshBound();
    Q_EMIT notifier()->changed();
    for (QWidget *w : QApplication::allWidgets())
        w->update();
}

} // namespace Theme
