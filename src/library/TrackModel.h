// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Track.h"

#include <QAbstractTableModel>
#include <QIcon>

// Table model over a subset (the current browse/search filter) of the library.
// Rows are indices into the library's TrackList.
class TrackModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColPlaying, ColTitle, ColTime, ColArtist, ColAlbum, ColGenre, ColYear, ColCount };
    static constexpr int SortRole = Qt::UserRole + 1;
    static constexpr int IndexRole = Qt::UserRole + 2;  // library index (int)

    explicit TrackModel(QObject *parent = nullptr);

    void setSource(const TrackList *tracks, QVector<int> rows);
    int libraryIndex(int row) const { return m_rows.value(row, -1); }
    const QVector<int> &rows() const { return m_rows; }

    // Library index of the playing track (-1 for none); drives the speaker icon.
    void setPlaying(int libraryIndex, bool paused);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    const TrackList *m_tracks = nullptr;
    QVector<int> m_rows;
    int m_playing = -1;
    bool m_paused = false;
    QIcon m_speaker;
};
