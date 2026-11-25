// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/RatingDelegate.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
constexpr int kStar = 11;   // star box, px
constexpr int kGap = 2;
constexpr int kLeft = 6;

QPainterPath starPath(QPointF c, double r)
{
    QPainterPath path;
    for (int i = 0; i < 10; ++i) {
        const double a = -M_PI / 2 + i * M_PI / 5;
        const double rr = (i % 2 == 0) ? r : r * 0.45;
        const QPointF pt(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    return path;
}
}

RatingDelegate::RatingDelegate(Setter setter, QObject *parent)
    : QStyledItemDelegate(parent), m_setter(std::move(setter))
{
}

int RatingDelegate::starsAt(const QRect &cell, int x)
{
    const int rel = x - cell.left() - kLeft;
    if (rel < -kLeft / 2)
        return 0;
    return qBound(1, rel / (kStar + kGap) + 1, 5);
}

void RatingDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    // Background/selection via the style with the number stripped, stars on top.
    QStyleOptionViewItem o = opt;
    initStyleOption(&o, index);
    o.text.clear();
    o.features &= ~QStyleOptionViewItem::HasDisplay;
    const QWidget *w = o.widget;
    QStyle *style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &o, p, w);

    const int rating = index.data(Qt::DisplayRole).toInt();
    const bool hot = opt.state & (QStyle::State_MouseOver | QStyle::State_Selected);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const double cy = opt.rect.center().y() + 0.5;
    for (int i = 0; i < 5; ++i) {
        const QPointF c(opt.rect.left() + kLeft + i * (kStar + kGap) + kStar / 2.0, cy);
        if (i < rating) {
            p->fillPath(starPath(c, kStar / 2.0), Theme::accent());
        } else if (hot) {
            QColor dot = Theme::accent();
            dot.setAlpha(110);
            p->setPen(Qt::NoPen);
            p->setBrush(dot);
            p->drawEllipse(c, 1.6, 1.6);
        }
    }
    p->restore();
}

QSize RatingDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    QSize s = QStyledItemDelegate::sizeHint(opt, index);
    s.setWidth(kLeft * 2 + 5 * kStar + 4 * kGap);
    return s;
}

bool RatingDelegate::editorEvent(QEvent *e, QAbstractItemModel *, const QStyleOptionViewItem &opt,
                                 const QModelIndex &index)
{
    if (e->type() != QEvent::MouseButtonRelease)
        return false;
    auto *me = static_cast<QMouseEvent *>(e);
    if (me->button() != Qt::LeftButton || !opt.rect.contains(me->position().toPoint()))
        return false;
    int stars = starsAt(opt.rect, me->position().toPoint().x());
    if (stars == index.data(Qt::DisplayRole).toInt())
        stars = stars - 1;  // click the last lit star again to step down / clear
    if (m_setter)
        m_setter(index, stars);
    return true;
}
