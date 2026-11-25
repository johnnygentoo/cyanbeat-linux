// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/TrackModel.h"

#include "ui/Theme.h"

#include <QDataStream>
#include <QDateTime>
#include <QFont>
#include <QIODevice>
#include <QLocale>
#include <QMimeData>
#include <QUrl>
#include <algorithm>

const QString TrackModel::MimeType = QStringLiteral("application/x-cyanbeat-tracks");

namespace {
QString shortDate(qint64 secs)
{
    if (secs <= 0)
        return {};
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(secs), QLocale::ShortFormat);
}
}

TrackModel::TrackModel(QObject *parent)
    : QAbstractTableModel(parent), m_speaker(QStringLiteral(":/icons/speaker.svg"))
{
}

void TrackModel::setSource(const TrackList *tracks, QVector<int> rows, QVector<int> positions)
{
    beginResetModel();
    m_tracks = tracks;
    m_rows = std::move(rows);
    m_positions = positions.size() == m_rows.size() ? std::move(positions) : QVector<int>();
    endResetModel();
}

void TrackModel::setPlaying(int libraryIndex, bool paused)
{
    if (m_playing == libraryIndex && m_paused == paused)
        return;
    const int old = m_playing;
    m_playing = libraryIndex;
    m_paused = paused;
    refreshTrack(old);
    refreshTrack(libraryIndex);
}

void TrackModel::refreshTrack(int libraryIndex)
{
    if (libraryIndex < 0)
        return;
    for (int r = 0; r < m_rows.size(); ++r)
        if (m_rows[r] == libraryIndex)
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
    const int pos = m_positions.isEmpty() ? -1 : m_positions[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColPos: return pos >= 0 ? QVariant(pos + 1) : QVariant();
        case ColTitle: return t.title;
        case ColTime: return formatDuration(t.durationMs);
        case ColArtist: return t.artist;
        case ColAlbum: return t.album;
        case ColGenre: return t.genre;
        case ColYear: return t.year > 0 ? QVariant(t.year) : QVariant();
        case ColRating: return t.rating;
        case ColPlays: return t.playCount > 0 ? QVariant(t.playCount) : QVariant();
        case ColAdded: return shortDate(t.addedAt);
        case ColLastPlayed: return shortDate(t.lastPlayed);
        default: return {};
        }
    case SortRole:
        switch (index.column()) {
        case ColPos: return pos;
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
        case ColRating: return t.rating;
        case ColPlays: return t.playCount;
        case ColAdded: return t.addedAt;
        case ColLastPlayed: return t.lastPlayed;
        default: return {};
        }
    case IndexRole:
        return li;
    case PositionRole:
        return pos;
    case Qt::DecorationRole:
        if (index.column() == ColPlaying && isPlaying)
            return Theme::icon(QStringLiteral("speaker"));
        return {};
    case Qt::FontRole:
        if (isPlaying) {
            QFont f;
            f.setBold(true);
            return f;
        }
        return {};
    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case ColPos: case ColTime: case ColYear: case ColPlays:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
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
    case ColPos: return QStringLiteral("#");
    case ColPlaying: return QString();
    case ColTitle: return tr("Name");
    case ColTime: return tr("Time");
    case ColArtist: return tr("Artist");
    case ColAlbum: return tr("Album");
    case ColGenre: return tr("Genre");
    case ColYear: return tr("Year");
    case ColRating: return tr("Rating");
    case ColPlays: return tr("Plays");
    case ColAdded: return tr("Date Added");
    case ColLastPlayed: return tr("Last Played");
    default: return {};
    }
}

Qt::ItemFlags TrackModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_reorderable ? Qt::ItemIsDropEnabled : Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (m_reorderable)
        f |= Qt::ItemIsDropEnabled;
    return f;
}

QStringList TrackModel::mimeTypes() const
{
    return {MimeType, QStringLiteral("text/uri-list")};
}

QMimeData *TrackModel::mimeData(const QModelIndexList &indexes) const
{
    QVector<int> rows;
    for (const QModelIndex &i : indexes)
        if (!rows.contains(i.row()))
            rows << i.row();
    // `indexes` arrive in selection order; the view hands us source rows, so
    // keep them in table order.
    std::sort(rows.begin(), rows.end());

    DragPayload p;
    QList<QUrl> urls;
    for (int r : rows) {
        p.tracks << m_rows[r];
        if (!m_positions.isEmpty())
            p.positions << m_positions[r];
        urls << QUrl::fromLocalFile((*m_tracks)[m_rows[r]].path);
    }
    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out << p.tracks << p.positions << rows;
    }
    auto *mime = new QMimeData;
    mime->setData(MimeType, bytes);
    mime->setUrls(urls);  // drag songs straight out to a file manager
    return mime;
}

TrackModel::DragPayload TrackModel::decode(const QMimeData *mime)
{
    DragPayload p;
    if (!mime || !mime->hasFormat(MimeType))
        return p;
    QDataStream in(mime->data(MimeType));
    in >> p.tracks >> p.positions;
    return p;
}

bool TrackModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &) const
{
    return m_reorderable && data && data->hasFormat(MimeType);
}

bool TrackModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                              const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
        return false;
    QVector<int> tracks, positions, rows;
    {
        QDataStream in(data->data(MimeType));
        in >> tracks >> positions >> rows;
    }
    int target = row;
    if (target < 0)
        target = parent.isValid() ? parent.row() : int(m_rows.size());
    // Applied after the drag finishes; the model gets rebuilt by the owner.
    QMetaObject::invokeMethod(this, [this, rows, target] { Q_EMIT reorderRequested(rows, target); },
                              Qt::QueuedConnection);
    // Returning false stops the view from also deleting the "moved" rows itself.
    return false;
}
