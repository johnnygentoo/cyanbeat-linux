// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Track.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

struct Tombstone {
    QString path;
    qint64 sizeBytes = 0;
    qint64 removedAt = 0;
    QString reason;
    QString keptPath;
};

struct Playlist {
    qint64 id = 0;
    QString name;
};

// SQLite persistence for the collection: track rows (tags + play stats),
// and playlists. Playlist entries are stored by path, so a song that vanishes
// (moved off the disk, mid-sync) keeps its place and comes back when the file does.
// GUI-thread only.
class Collection {
public:
    Collection() = default;
    ~Collection();
    Collection(const Collection &) = delete;
    Collection &operator=(const Collection &) = delete;

    // Opens (creating/migrating) the database. ":memory:" works for tests.
    bool open(const QString &path);
    bool isOpen() const { return !m_conn.isEmpty(); }
    QString lastError() const { return m_error; }

    // ---- tracks
    TrackList loadTracks();
    // Writes new/changed rows and deletes rows whose path is in `removed`.
    void saveTracks(const TrackList &changed, const QStringList &removed);
    void saveStats(const Track &t);  // play count, last played, rating
    void saveAddedAt(const Track &t);

    // ---- playlists
    QVector<Playlist> playlists();
    qint64 createPlaylist(const QString &name);
    void renamePlaylist(qint64 id, const QString &name);
    void deletePlaylist(qint64 id);
    QStringList playlistItems(qint64 id);          // paths, in order
    void appendToPlaylist(qint64 id, const QStringList &paths);
    void setPlaylistItems(qint64 id, const QStringList &paths);  // replace the whole order

    // Point every playlist entry for `from` at `to` (dropping repeats of `to`).
    void repointPlaylists(const QString &from, const QString &to);

    // ---- removed on purpose
    void addTombstone(const QString &path, qint64 sizeBytes, const QString &reason, const QString &keptPath);
    QVector<Tombstone> tombstones();

    // ---- phone sync
    QSet<QString> syncState(const QString &device, const QString &localRoot);
    void setSyncState(const QString &device, const QString &localRoot, const QSet<QString> &rels);

private:
    bool exec(const QString &sql);
    bool migrate();

    QString m_conn;  // QSqlDatabase connection name
    QString m_error;
};
