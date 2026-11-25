// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

struct Track {
    QString path;
    QString title;        // falls back to the file name when untagged (yt-dlp rips often are)
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    int year = 0;
    int trackNo = 0;
    int discNo = 0;
    qint64 durationMs = 0;
    qint64 sizeBytes = 0;
    qint64 mtime = 0;     // seconds since epoch; used by sync and incremental rescans

    // Collection stats (SQLite), kept across tag re-reads.
    int playCount = 0;
    qint64 lastPlayed = 0; // seconds since epoch, 0 = never
    int rating = 0;        // 0..5 stars
    qint64 addedAt = 0;    // seconds since epoch

    // What the browser panes group by.
    QString browseArtist() const { return albumArtist.isEmpty() ? artist : albumArtist; }
};

using TrackList = QVector<Track>;

QString formatDuration(qint64 ms);
// "3,795 songs, 10.2 days, 14.3 GB"
QString formatLibrarySummary(int count, qint64 totalMs, qint64 totalBytes);
