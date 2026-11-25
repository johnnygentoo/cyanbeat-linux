// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/Collection.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {
constexpr int kSchemaVersion = 1;
}

Collection::~Collection()
{
    if (m_conn.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_conn, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_conn);
}

bool Collection::open(const QString &path)
{
    if (path != QLatin1String(":memory:"))
        QDir().mkpath(QFileInfo(path).absolutePath());
    const QString conn = QStringLiteral("cyanbeat-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(path);
    if (!db.open()) {
        m_error = db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(conn);
        return false;
    }
    m_conn = conn;
    // WAL + NORMAL: crash-safe, and doesn't fsync the spinning disk on every play count.
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return migrate();
}

bool Collection::exec(const QString &sql)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (!q.exec(sql)) {
        m_error = q.lastError().text();
        qWarning("Collection: %s -- %s", qPrintable(m_error), qPrintable(sql));
        return false;
    }
    return true;
}

bool Collection::migrate()
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.exec(QStringLiteral("PRAGMA user_version"));
    const int version = q.next() ? q.value(0).toInt() : 0;
    if (version >= kSchemaVersion)
        return true;

    QSqlDatabase db = QSqlDatabase::database(m_conn);
    db.transaction();
    bool ok = exec(QStringLiteral(
                  "CREATE TABLE IF NOT EXISTS tracks ("
                  " path TEXT PRIMARY KEY,"
                  " title TEXT, artist TEXT, album TEXT, album_artist TEXT, genre TEXT,"
                  " year INTEGER, track_no INTEGER, disc_no INTEGER,"
                  " duration_ms INTEGER, size_bytes INTEGER, mtime INTEGER,"
                  " play_count INTEGER NOT NULL DEFAULT 0,"
                  " last_played INTEGER NOT NULL DEFAULT 0,"
                  " rating INTEGER NOT NULL DEFAULT 0,"
                  " added_at INTEGER NOT NULL DEFAULT 0)"))
        && exec(QStringLiteral(
                  "CREATE TABLE IF NOT EXISTS playlists ("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  " name TEXT NOT NULL,"
                  " created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))"))
        && exec(QStringLiteral(
                  "CREATE TABLE IF NOT EXISTS playlist_items ("
                  " playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
                  " pos INTEGER NOT NULL,"
                  " path TEXT NOT NULL,"
                  " PRIMARY KEY (playlist_id, pos))"))
        && exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
    if (ok)
        db.commit();
    else
        db.rollback();
    return ok;
}

// ---------------------------------------------------------------- tracks

TrackList Collection::loadTracks()
{
    TrackList out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral(
            "SELECT path, title, artist, album, album_artist, genre, year, track_no, disc_no,"
            " duration_ms, size_bytes, mtime, play_count, last_played, rating, added_at FROM tracks")))
        return out;
    while (q.next()) {
        Track t;
        t.path = q.value(0).toString();
        t.title = q.value(1).toString();
        t.artist = q.value(2).toString();
        t.album = q.value(3).toString();
        t.albumArtist = q.value(4).toString();
        t.genre = q.value(5).toString();
        t.year = q.value(6).toInt();
        t.trackNo = q.value(7).toInt();
        t.discNo = q.value(8).toInt();
        t.durationMs = q.value(9).toLongLong();
        t.sizeBytes = q.value(10).toLongLong();
        t.mtime = q.value(11).toLongLong();
        t.playCount = q.value(12).toInt();
        t.lastPlayed = q.value(13).toLongLong();
        t.rating = q.value(14).toInt();
        t.addedAt = q.value(15).toLongLong();
        out.push_back(std::move(t));
    }
    return out;
}

void Collection::saveTracks(const TrackList &changed, const QStringList &removed)
{
    if (changed.isEmpty() && removed.isEmpty())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    db.transaction();
    QSqlQuery up(db);
    up.prepare(QStringLiteral(
        "INSERT INTO tracks (path, title, artist, album, album_artist, genre, year, track_no, disc_no,"
        " duration_ms, size_bytes, mtime, play_count, last_played, rating, added_at)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(path) DO UPDATE SET title=excluded.title, artist=excluded.artist,"
        " album=excluded.album, album_artist=excluded.album_artist, genre=excluded.genre,"
        " year=excluded.year, track_no=excluded.track_no, disc_no=excluded.disc_no,"
        " duration_ms=excluded.duration_ms, size_bytes=excluded.size_bytes, mtime=excluded.mtime"));
    for (const Track &t : changed) {
        up.addBindValue(t.path);
        up.addBindValue(t.title);
        up.addBindValue(t.artist);
        up.addBindValue(t.album);
        up.addBindValue(t.albumArtist);
        up.addBindValue(t.genre);
        up.addBindValue(t.year);
        up.addBindValue(t.trackNo);
        up.addBindValue(t.discNo);
        up.addBindValue(t.durationMs);
        up.addBindValue(t.sizeBytes);
        up.addBindValue(t.mtime);
        up.addBindValue(t.playCount);
        up.addBindValue(t.lastPlayed);
        up.addBindValue(t.rating);
        up.addBindValue(t.addedAt);
        if (!up.exec())
            qWarning("Collection: upsert %s: %s", qPrintable(t.path), qPrintable(up.lastError().text()));
    }
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM tracks WHERE path = ?"));
    for (const QString &p : removed) {
        del.addBindValue(p);
        del.exec();
    }
    db.commit();
}

void Collection::saveStats(const Track &t)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("UPDATE tracks SET play_count=?, last_played=?, rating=? WHERE path=?"));
    q.addBindValue(t.playCount);
    q.addBindValue(t.lastPlayed);
    q.addBindValue(t.rating);
    q.addBindValue(t.path);
    q.exec();
}

// ---------------------------------------------------------------- playlists

QVector<Playlist> Collection::playlists()
{
    QVector<Playlist> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.exec(QStringLiteral("SELECT id, name FROM playlists ORDER BY name COLLATE NOCASE, id"));
    while (q.next())
        out.push_back({q.value(0).toLongLong(), q.value(1).toString()});
    return out;
}

qint64 Collection::createPlaylist(const QString &name)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("INSERT INTO playlists (name) VALUES (?)"));
    q.addBindValue(name);
    if (!q.exec())
        return 0;
    return q.lastInsertId().toLongLong();
}

void Collection::renamePlaylist(qint64 id, const QString &name)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("UPDATE playlists SET name=? WHERE id=?"));
    q.addBindValue(name);
    q.addBindValue(id);
    q.exec();
}

void Collection::deletePlaylist(qint64 id)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE id=?"));
    q.addBindValue(id);
    q.exec();
}

QStringList Collection::playlistItems(qint64 id)
{
    QStringList out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT path FROM playlist_items WHERE playlist_id=? ORDER BY pos"));
    q.addBindValue(id);
    q.exec();
    while (q.next())
        out << q.value(0).toString();
    return out;
}

void Collection::appendToPlaylist(qint64 id, const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COALESCE(MAX(pos), -1) + 1 FROM playlist_items WHERE playlist_id=?"));
    q.addBindValue(id);
    q.exec();
    int pos = q.next() ? q.value(0).toInt() : 0;
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, pos, path) VALUES (?,?,?)"));
    for (const QString &p : paths) {
        ins.addBindValue(id);
        ins.addBindValue(pos++);
        ins.addBindValue(p);
        ins.exec();
    }
    db.commit();
}

void Collection::setPlaylistItems(qint64 id, const QStringList &paths)
{
    QSqlDatabase db = QSqlDatabase::database(m_conn);
    db.transaction();
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id=?"));
    del.addBindValue(id);
    del.exec();
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO playlist_items (playlist_id, pos, path) VALUES (?,?,?)"));
    int pos = 0;
    for (const QString &p : paths) {
        ins.addBindValue(id);
        ins.addBindValue(pos++);
        ins.addBindValue(p);
        ins.exec();
    }
    db.commit();
}
