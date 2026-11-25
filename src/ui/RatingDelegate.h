// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStyledItemDelegate>
#include <functional>

// Five stars, click to rate (click the current rating's last star again to clear).
// Unrated rows show faint dots only on hover/selection, like the classic jukeboxes.
class RatingDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using Setter = std::function<void(const QModelIndex &, int stars)>;
    explicit RatingDelegate(Setter setter, QObject *parent = nullptr);

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    bool editorEvent(QEvent *e, QAbstractItemModel *model, const QStyleOptionViewItem &opt,
                     const QModelIndex &index) override;

    static int starsAt(const QRect &cell, int x);

private:
    Setter m_setter;
};
