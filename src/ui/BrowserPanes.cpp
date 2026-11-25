// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/BrowserPanes.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSet>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>

namespace {
constexpr int KeyRole = Qt::UserRole + 1;
constexpr int IsAllRole = Qt::UserRole + 2;

const char *const kTitles[] = {QT_TRANSLATE_NOOP("BrowserPanes", "Genre"),
                               QT_TRANSLATE_NOOP("BrowserPanes", "Artist"),
                               QT_TRANSLATE_NOOP("BrowserPanes", "Album")};
const char *const kPlural[] = {QT_TRANSLATE_NOOP("BrowserPanes", "Genres"),
                               QT_TRANSLATE_NOOP("BrowserPanes", "Artists"),
                               QT_TRANSLATE_NOOP("BrowserPanes", "Albums")};
const char *const kUnknown[] = {QT_TRANSLATE_NOOP("BrowserPanes", "Unknown Genre"),
                                QT_TRANSLATE_NOOP("BrowserPanes", "Unknown Artist"),
                                QT_TRANSLATE_NOOP("BrowserPanes", "Unknown Album")};
}

BrowserPanes::BrowserPanes(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    outer->addWidget(split);

    for (int level = 0; level < Levels; ++level) {
        auto *col = new QWidget(split);
        auto *v = new QVBoxLayout(col);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);

        // Styled like a table header so the panes read as one column browser.
        auto *label = new QLabel(tr(kTitles[level]), col);
        label->setAlignment(Qt::AlignCenter);
        label->setObjectName(QStringLiteral("PaneHeader"));
        v->addWidget(label);

        auto *list = new QListWidget(col);
        list->setObjectName(QStringLiteral("BrowserList"));
        list->setUniformItemSizes(true);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        v->addWidget(list);
        m_lists[level] = list;

        connect(list, &QListWidget::currentItemChanged, this, [this, level](QListWidgetItem *item) {
            if (!item)
                return;
            if (item->data(IsAllRole).toBool())
                m_sel[level].reset();
            else
                m_sel[level] = item->data(KeyRole).toString();
            rebuild(level + 1);
        });
    }
}

QString BrowserPanes::keyOf(int level, const Track &t) const
{
    switch (level) {
    case Genre: return t.genre;
    case Artist: return t.browseArtist();
    default: return t.album;
    }
}

void BrowserPanes::setSource(const TrackList *tracks, QVector<int> base)
{
    m_tracks = tracks;
    m_base = std::move(base);
    rebuild(0);
}

void BrowserPanes::clearSelection()
{
    for (auto &s : m_sel)
        s.reset();
    rebuild(0);
}

void BrowserPanes::rebuild(int fromLevel)
{
    QVector<int> pool(m_base.size());
    std::iota(pool.begin(), pool.end(), 0);  // positions into m_base
    for (int level = 0; level < Levels; ++level) {
        if (level >= fromLevel && m_tracks) {
            QStringList keys;
            {
                QSet<QString> seen;
                for (int i : pool) {
                    const QString k = keyOf(level, (*m_tracks)[m_base[i]]);
                    if (!seen.contains(k)) {
                        seen.insert(k);
                        keys << k;
                    }
                }
            }
            std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
                if (a.isEmpty() != b.isEmpty())
                    return b.isEmpty();  // "Unknown" sinks to the bottom
                return QString::localeAwareCompare(a, b) < 0;
            });

            QListWidget *list = m_lists[level];
            const QSignalBlocker block(list);
            list->clear();
            const int realCount = int(keys.size()) - (keys.contains(QString()) ? 1 : 0);
            auto *all = new QListWidgetItem(
                tr("All (%1 %2)").arg(realCount).arg(tr(kPlural[level])), list);
            all->setData(IsAllRole, true);
            QFont bold = all->font();
            bold.setBold(true);
            all->setFont(bold);

            QListWidgetItem *current = all;
            for (const QString &k : keys) {
                auto *item = new QListWidgetItem(k.isEmpty() ? tr(kUnknown[level]) : k, list);
                item->setData(KeyRole, k);
                if (m_sel[level] && *m_sel[level] == k)
                    current = item;
            }
            if (current == all)
                m_sel[level].reset();
            list->setCurrentItem(current);
        }

        if (m_sel[level] && m_tracks) {
            QVector<int> next;
            next.reserve(pool.size());
            for (int i : pool)
                if (keyOf(level, (*m_tracks)[m_base[i]]) == *m_sel[level])
                    next << i;
            pool = std::move(next);
        }
    }
    m_filtered = std::move(pool);
    Q_EMIT filterChanged();
}
