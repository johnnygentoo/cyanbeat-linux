// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Collection.h"
#include "library/Track.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <atomic>
#include <memory>

// The music collection: watched folders, the tracks found in them (kept in
// memory for speed) and their SQLite backing (stats, playlists). Launch shows
// the stored collection at once; rescans only re-read files whose size or
// mtime changed.
class Library : public QObject {
    Q_OBJECT
public:
    // `dbPath` empty = the default under the app data dir.
    explicit Library(const QString &dbPath = QString(), QObject *parent = nullptr);
    ~Library() override;

    void start();

    const TrackList &tracks() const { return m_tracks; }
    int indexOfPath(const QString &path) const { return m_byPath.value(path, -1); }
    QStringList folders() const { return m_folders; }
    bool isScanning() const { return m_watcher.isRunning(); }

    void addFolder(const QString &folder);
    void removeFolder(const QString &folder);
    void rescan();

    // Stats. Both persist immediately and emit trackUpdated.
    void recordPlay(int index);
    void setRating(int index, int stars);  // 0..5

    // Playlists (entries are library indices; missing files are skipped).
    QVector<Playlist> playlists() { return m_db.playlists(); }
    qint64 createPlaylist(const QString &name, const QVector<int> &tracks = {});
    void renamePlaylist(qint64 id, const QString &name);
    void deletePlaylist(qint64 id);
    QVector<int> playlistTracks(qint64 id);
    void appendToPlaylist(qint64 id, const QVector<int> &tracks);
    // Replace the order of a playlist's *present* entries, keeping missing ones in place.
    void reorderPlaylist(qint64 id, const QVector<int> &presentInOrder);
    // Remove entries by their position among the playlist's present entries.
    void removeFromPlaylist(qint64 id, QVector<int> presentPositions);

    // Duplicates: each pair is (path to remove, path to keep). The removed file
    // goes to the Trash; its plays/rating merge into the keeper, playlists point
    // at the keeper, and a tombstone is left for sync. Returns paths that could
    // not be trashed (left untouched). Triggers a rescan.
    QStringList removeDuplicates(const QVector<QPair<QString, QString>> &removeKeep);
    QVector<Tombstone> tombstones() { return m_db.tombstones(); }

Q_SIGNALS:
    void scanStarted();
    void tracksAboutToChange();  // last chance to read old indices
    void tracksChanged();
    void scanFinished();         // always, even when nothing changed
    void trackUpdated(int index);
    void playlistsChanged();
    void playlistContentsChanged(qint64 id);

private:
    void saveFolders() const;
    void setTracks(TrackList tracks);
    void applyScan(TrackList scanned);

    QString m_dbPath;
    Collection m_db;
    QStringList m_folders;
    TrackList m_tracks;
    QHash<QString, int> m_byPath;
    QFutureWatcher<TrackList> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_rescanQueued = false;
};
