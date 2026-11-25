// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Track.h"

#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

// Finding the same song stored more than once, and ranking the copies.
namespace Duplicates {

enum Reason {
    SameBytes = 1,  // byte-identical files
    SameVideo = 2,  // same YouTube id in the file name (yt-dlp's "[id]" or "-id" style)
    SameSong = 4,   // same words in artist+title (any order), same length
};

struct Quality {
    QString codec;          // human-readable, e.g. "Opus", "MPEG-1 Layer 3 (MP3)"
    int bitrateKbps = 0;    // audio stream; 0 = unknown
    bool bitrateEstimated = false;
    int sampleRate = 0;
    int channels = 0;
    bool lossless = false;
    bool hasVideo = false;  // a video file kept for its audio
    qint64 durationMs = 0;
};

enum class Prefer { BestQuality, SmallestFile };

struct Member {
    int index = -1;  // library index
    Quality quality;
    double score = 0;
};

struct Group {
    QVector<Member> members;
    int reasons = 0;
    int keep = 0;    // position in members of the copy to keep
};

// ---- matching (pure; exposed for tests)
QString youtubeId(const QString &path);       // "" if none
QString normalizedTitle(const Track &t);      // noise-stripped, for display/debug
QStringList songTokens(const Track &t);       // sorted unique words of artist + title
QStringList titleTokens(const Track &t);      // sorted unique words of the title alone
QString artistKey(const Track &t);            // "" if unknown
bool sameLength(qint64 a, qint64 b);

// Groups of library indices that look like the same song, with their reasons.
// Hashes same-sized files to find byte-identical copies (reads those files).
struct RawGroup {
    QVector<int> indices;
    int reasons = 0;
};
QVector<RawGroup> findGroups(const TrackList &tracks, const std::atomic_bool *cancel = nullptr,
                             bool hashFiles = true);

// ---- ranking
Quality probe(const QString &path, qint64 knownDurationMs = 0);  // GStreamer discoverer + TagLib
double score(const Quality &q, qint64 sizeBytes, qint64 longestInGroupMs, Prefer prefer);
QString describe(const Quality &q);  // "Opus · 160 kbps · 48 kHz"

// Full pipeline for the dialog: find, probe each member, score, pick keepers.
// `progress(done, total)` may be called from the worker thread.
QVector<Group> scan(const TrackList &tracks, Prefer prefer, const std::atomic_bool *cancel,
                    const std::function<void(int, int)> &progress = {});
void rescore(QVector<Group> &groups, const TrackList &tracks, Prefer prefer);

} // namespace Duplicates
