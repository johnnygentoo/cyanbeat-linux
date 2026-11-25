// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/Library.h"

#include "library/Scanner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>

Library::Library(const QString &dbPath, QObject *parent) : QObject(parent), m_dbPath(dbPath)
{
    QSettings s;
    if (s.contains(QStringLiteral("library/folders"))) {
        m_folders = s.value(QStringLiteral("library/folders")).toStringList();
    } else {
        // First run: start with the standard Music folder, like a classic jukebox app.
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!music.isEmpty() && QDir(music).exists())
            m_folders << music;
        saveFolders();
    }

    connect(&m_watcher, &QFutureWatcher<TrackList>::finished, this, [this] {
        if (m_rescanQueued) {
            m_rescanQueued = false;
            rescan();
            return;
        }
        if (m_cancel && m_cancel->load())
            return;
        applyScan(m_watcher.result());
        Q_EMIT scanFinished();
    });
}

Library::~Library()
{
    if (m_cancel)
        m_cancel->store(true);
    m_watcher.waitForFinished();
}

void Library::start()
{
    QString path = m_dbPath;
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/collection.db");
        // Phase-0 binary cache, superseded by the database.
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/library.bin"));
    }
    if (!m_db.open(path))
        qWarning("CyanBeat: cannot open collection %s: %s", qPrintable(path), qPrintable(m_db.lastError()));
    setTracks(m_db.loadTracks());
    Q_EMIT playlistsChanged();
    rescan();
}

void Library::setTracks(TrackList tracks)
{
    std::sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b) {
        const int c = QString::localeAwareCompare(a.browseArtist(), b.browseArtist());
        if (c != 0)
            return c < 0;
        if (a.album != b.album)
            return QString::localeAwareCompare(a.album, b.album) < 0;
        if (a.discNo != b.discNo)
            return a.discNo < b.discNo;
        if (a.trackNo != b.trackNo)
            return a.trackNo < b.trackNo;
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    Q_EMIT tracksAboutToChange();
    m_tracks = std::move(tracks);
    m_byPath.clear();
    m_byPath.reserve(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i)
        m_byPath.insert(m_tracks[i].path, i);
    Q_EMIT tracksChanged();
}

void Library::applyScan(TrackList scanned)
{
    // Diff against what we have: only new/changed rows hit the database, and an
    // unchanged rescan leaves the view alone.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    TrackList changed;
    QSet<QString> seen;
    seen.reserve(scanned.size());
    for (Track &t : scanned) {
        seen.insert(t.path);
        const int old = m_byPath.value(t.path, -1);
        if (old < 0) {
            if (t.addedAt <= 0 || t.addedAt > now)
                t.addedAt = now;
            changed.push_back(t);
        } else {
            const Track &o = m_tracks[old];
            if (o.sizeBytes != t.sizeBytes || o.mtime != t.mtime)
                changed.push_back(t);
        }
    }
    QStringList removed;
    for (const Track &t : std::as_const(m_tracks))
        if (!seen.contains(t.path))
            removed << t.path;

    if (changed.isEmpty() && removed.isEmpty())
        return;
    m_db.saveTracks(changed, removed);
    setTracks(std::move(scanned));
}

void Library::addFolder(const QString &folder)
{
    const QString clean = QDir::cleanPath(folder);
    if (clean.isEmpty() || m_folders.contains(clean))
        return;
    m_folders << clean;
    saveFolders();
    rescan();
}

void Library::removeFolder(const QString &folder)
{
    if (m_folders.removeAll(QDir::cleanPath(folder)) == 0)
        return;
    saveFolders();
    rescan();
}

void Library::rescan()
{
    if (m_watcher.isRunning()) {
        m_rescanQueued = true;
        return;
    }
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const QStringList folders = m_folders;
    auto cancel = m_cancel;
    auto known = std::make_shared<QHash<QString, Track>>();
    known->reserve(m_tracks.size());
    for (const Track &t : std::as_const(m_tracks))
        known->insert(t.path, t);
    Q_EMIT scanStarted();
    m_watcher.setFuture(QtConcurrent::run([folders, cancel, known] {
        return Scanner::scanFolders(folders, cancel.get(), known.get());
    }));
}

void Library::saveFolders() const
{
    QSettings().setValue(QStringLiteral("library/folders"), m_folders);
}

// ---------------------------------------------------------------- stats

void Library::recordPlay(int index)
{
    if (index < 0 || index >= m_tracks.size())
        return;
    Track &t = m_tracks[index];
    ++t.playCount;
    t.lastPlayed = QDateTime::currentSecsSinceEpoch();
    m_db.saveStats(t);
    Q_EMIT trackUpdated(index);
}

void Library::setRating(int index, int stars)
{
    if (index < 0 || index >= m_tracks.size())
        return;
    Track &t = m_tracks[index];
    t.rating = qBound(0, stars, 5);
    m_db.saveStats(t);
    Q_EMIT trackUpdated(index);
}

// ---------------------------------------------------------------- playlists

qint64 Library::createPlaylist(const QString &name, const QVector<int> &tracks)
{
    const qint64 id = m_db.createPlaylist(name);
    if (id && !tracks.isEmpty())
        appendToPlaylist(id, tracks);
    Q_EMIT playlistsChanged();
    return id;
}

void Library::renamePlaylist(qint64 id, const QString &name)
{
    m_db.renamePlaylist(id, name);
    Q_EMIT playlistsChanged();
}

void Library::deletePlaylist(qint64 id)
{
    m_db.deletePlaylist(id);
    Q_EMIT playlistsChanged();
}

QVector<int> Library::playlistTracks(qint64 id)
{
    QVector<int> out;
    for (const QString &p : m_db.playlistItems(id)) {
        const int i = indexOfPath(p);
        if (i >= 0)
            out << i;
    }
    return out;
}

void Library::appendToPlaylist(qint64 id, const QVector<int> &tracks)
{
    QStringList paths;
    for (int i : tracks)
        if (i >= 0 && i < m_tracks.size())
            paths << m_tracks[i].path;
    m_db.appendToPlaylist(id, paths);
    Q_EMIT playlistContentsChanged(id);
}

void Library::reorderPlaylist(qint64 id, const QVector<int> &presentInOrder)
{
    // Present entries fill the slots that present entries occupied; missing
    // entries (file not on disk right now) stay where they were.
    QStringList all = m_db.playlistItems(id);
    int k = 0;
    for (QString &p : all) {
        if (indexOfPath(p) < 0)
            continue;
        if (k < presentInOrder.size())
            p = m_tracks[presentInOrder[k++]].path;
    }
    m_db.setPlaylistItems(id, all);
    Q_EMIT playlistContentsChanged(id);
}

void Library::removeFromPlaylist(qint64 id, QVector<int> presentPositions)
{
    std::sort(presentPositions.begin(), presentPositions.end());
    const QStringList all = m_db.playlistItems(id);
    QStringList kept;
    int present = 0;
    for (const QString &p : all) {
        if (indexOfPath(p) >= 0) {
            const bool drop = std::binary_search(presentPositions.cbegin(), presentPositions.cend(), present);
            ++present;
            if (drop)
                continue;
        }
        kept << p;
    }
    m_db.setPlaylistItems(id, kept);
    Q_EMIT playlistContentsChanged(id);
}

// ---------------------------------------------------------------- duplicates

QStringList Library::removeDuplicates(const QVector<QPair<QString, QString>> &removeKeep)
{
    QStringList failed;
    bool any = false;
    for (const auto &[gone, kept] : removeKeep) {
        const int gi = indexOfPath(gone), ki = indexOfPath(kept);
        if (gi < 0 || ki < 0 || gi == ki) {
            failed << gone;
            continue;
        }
        const Track old = m_tracks[gi];
        if (!QFile::moveToTrash(gone)) {
            failed << gone;
            continue;
        }
        any = true;
        Track &keep = m_tracks[ki];
        keep.playCount += old.playCount;
        keep.lastPlayed = qMax(keep.lastPlayed, old.lastPlayed);
        keep.rating = qMax(keep.rating, old.rating);
        if (old.addedAt > 0 && (keep.addedAt <= 0 || old.addedAt < keep.addedAt))
            keep.addedAt = old.addedAt;  // you've had the song since the older copy arrived
        m_db.saveStats(keep);
        m_db.saveAddedAt(keep);
        m_db.repointPlaylists(gone, kept);
        m_db.addTombstone(gone, old.sizeBytes, QStringLiteral("duplicate"), kept);
        Q_EMIT trackUpdated(ki);
    }
    if (any) {
        Q_EMIT playlistsChanged();
        rescan();
    }
    return failed;
}
