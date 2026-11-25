// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Track.h"

#include <QAbstractTableModel>
#include <QIcon>

class QMimeData;

// Table model over a subset (the current source + browse/search filter) of
// the library. Rows are indices into the library's TrackList; in a playlist
// a track can appear more than once, and each row also carries its position.
class TrackModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColPos, ColPlaying, ColTitle, ColTime, ColArtist, ColAlbum, ColGenre, ColYear,
        ColRating, ColPlays, ColAdded, ColLastPlayed, ColCount
    };
    static constexpr int SortRole = Qt::UserRole + 1;
    static constexpr int IndexRole = Qt::UserRole + 2;     // library index (int)
    static constexpr int PositionRole = Qt::UserRole + 3;  // playlist position (int, -1 outside playlists)

    // Drag payload between the track table and the source list.
    static const QString MimeType;
    struct DragPayload {
        QVector<int> tracks;     // library indices, in view order
        QVector<int> positions;  // playlist positions of the dragged rows (empty outside playlists)
    };
    static DragPayload decode(const QMimeData *mime);

    explicit TrackModel(QObject *parent = nullptr);

    // `positions` (same length as rows) = playlist positions, or empty.
    void setSource(const TrackList *tracks, QVector<int> rows, QVector<int> positions = {});
    int libraryIndex(int row) const { return m_rows.value(row, -1); }
    const QVector<int> &rows() const { return m_rows; }

    // Library index of the playing track (-1 for none); drives the speaker icon.
    void setPlaying(int libraryIndex, bool paused);
    void refreshTrack(int libraryIndex);  // stats changed
    // Only a playlist shown in position order, unfiltered, accepts reordering drops.
    void setReorderable(bool on) { m_reorderable = on; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction | Qt::MoveAction; }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

Q_SIGNALS:
    // Rows (model rows) dragged to before `targetRow` (== rowCount for the end).
    void reorderRequested(QVector<int> rows, int targetRow);

private:
    const TrackList *m_tracks = nullptr;
    QVector<int> m_rows;
    QVector<int> m_positions;
    int m_playing = -1;
    bool m_paused = false;
    bool m_reorderable = false;
    QIcon m_speaker;
};
