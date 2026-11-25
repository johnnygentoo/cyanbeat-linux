// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Track.h"

#include <QWidget>
#include <optional>

class QListWidget;

// Genre | Artist | Album column browser. Each pane narrows the next one;
// "All (…)" at the top of a pane means no filter at that level.
class BrowserPanes : public QWidget {
    Q_OBJECT
public:
    explicit BrowserPanes(QWidget *parent = nullptr);

    // `base` = library indices still in play (after the search filter).
    void setSource(const TrackList *tracks, QVector<int> base);
    const QVector<int> &filtered() const { return m_filtered; }

    // Reset all three panes to "All".
    void clearSelection();

Q_SIGNALS:
    void filterChanged();

private:
    enum Level { Genre, Artist, Album, Levels };
    QString keyOf(int level, const Track &t) const;
    void rebuild(int fromLevel);

    const TrackList *m_tracks = nullptr;
    QVector<int> m_base;
    QVector<int> m_filtered;
    QListWidget *m_lists[Levels] = {};
    std::optional<QString> m_sel[Levels];  // nullopt = All
};
