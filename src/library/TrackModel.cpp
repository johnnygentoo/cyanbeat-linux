// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/TrackModel.h"

#include <QFont>

TrackModel::TrackModel(QObject *parent)
    : QAbstractTableModel(parent), m_speaker(QStringLiteral(":/icons/speaker.svg"))
{
}

void TrackModel::setSource(const TrackList *tracks, QVector<int> rows)
{
    beginResetModel();
    m_tracks = tracks;
    m_rows = std::move(rows);
    endResetModel();
}

void TrackModel::setPlaying(int libraryIndex, bool paused)
{
    if (m_playing == libraryIndex && m_paused == paused)
        return;
    const int old = m_rows.indexOf(m_playing);
    m_playing = libraryIndex;
    m_paused = paused;
    for (int r : {old, int(m_rows.indexOf(libraryIndex))})
        if (r >= 0)
            Q_EMIT dataChanged(index(r, 0), index(r, ColCount - 1));
}

int TrackModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int TrackModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant TrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_tracks || index.row() >= m_rows.size())
        return {};
    const int li = m_rows[index.row()];
    const Track &t = (*m_tracks)[li];
    const bool isPlaying = li == m_playing;

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColTitle: return t.title;
        case ColTime: return formatDuration(t.durationMs);
        case ColArtist: return t.artist;
        case ColAlbum: return t.album;
        case ColGenre: return t.genre;
        case ColYear: return t.year > 0 ? QVariant(t.year) : QVariant();
        default: return {};
        }
    case SortRole:
        switch (index.column()) {
        case ColPlaying: return isPlaying ? 0 : 1;
        case ColTitle: return t.title.toLower();
        case ColTime: return t.durationMs;
        // Artist/album sort keeps albums in track order, like a record shelf.
        case ColArtist: return QStringLiteral("%1\x1f%2\x1f%3\x1f%4")
                .arg(t.artist.toLower(), t.album.toLower())
                .arg(t.discNo, 3, 10, QLatin1Char('0')).arg(t.trackNo, 4, 10, QLatin1Char('0'));
        case ColAlbum: return QStringLiteral("%1\x1f%2\x1f%3")
                .arg(t.album.toLower()).arg(t.discNo, 3, 10, QLatin1Char('0')).arg(t.trackNo, 4, 10, QLatin1Char('0'));
        case ColGenre: return t.genre.toLower();
        case ColYear: return t.year;
        default: return {};
        }
    case IndexRole:
        return li;
    case Qt::DecorationRole:
        if (index.column() == ColPlaying && isPlaying)
            return m_speaker;
        return {};
    case Qt::FontRole:
        if (isPlaying) {
            QFont f;
            f.setBold(true);
            return f;
        }
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == ColTime || index.column() == ColYear)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::ToolTipRole:
        if (index.column() == ColTitle)
            return t.path;
        return {};
    default:
        return {};
    }
}

QVariant TrackModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColPlaying: return QString();
    case ColTitle: return tr("Name");
    case ColTime: return tr("Time");
    case ColArtist: return tr("Artist");
    case ColAlbum: return tr("Album");
    case ColGenre: return tr("Genre");
    case ColYear: return tr("Year");
    default: return {};
    }
}
