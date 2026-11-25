// SPDX-License-Identifier: GPL-3.0-or-later
// Duplicate matching on the naming patterns found in a real yt-dlp library.
#include "library/Duplicates.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <cstdio>

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
                           else std::fprintf(stderr, "ok: %s\n", msg); } while (0)

static Track mk(const QString &file, qint64 ms, const QString &artist = {}, const QString &title = {})
{
    Track t;
    t.path = QStringLiteral("/music/") + file;
    t.title = title.isEmpty() ? QFileInfo(t.path).completeBaseName() : title;
    t.artist = artist;
    t.durationMs = ms;
    t.sizeBytes = 1000 + ms;
    return t;
}

static bool together(const QVector<Duplicates::RawGroup> &groups, std::initializer_list<int> idx, int *reasons = nullptr)
{
    for (const auto &g : groups) {
        bool all = true;
        for (int i : idx)
            all = all && g.indices.contains(i);
        if (all) {
            if (reasons)
                *reasons = g.reasons;
            return true;
        }
    }
    return false;
}

static bool grouped(const QVector<Duplicates::RawGroup> &groups, int i)
{
    for (const auto &g : groups)
        if (g.indices.contains(i))
            return true;
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace Duplicates;

    CHECK(youtubeId(QStringLiteral("/m/Bee Gees - Stayin' Alive (Official Video) [I_izvAbhExY].mp3")) == QLatin1String("I_izvAbhExY"), "id from [id]");
    CHECK(youtubeId(QStringLiteral("/m/Bee Gees - Stayin' Alive (Official Video)-I_izvAbhExY.mp3")) == QLatin1String("I_izvAbhExY"), "id from -id");
    CHECK(youtubeId(QStringLiteral("/m/why mona - Stayin' Alive.mp3")).isEmpty(), "no id in a plain name");
    CHECK(youtubeId(QStringLiteral("/m/Condemned Tower [Arrangement] [csW-g1B8oeU].mp3")) == QLatin1String("csW-g1B8oeU"),
          "[Arrangement] is a word, not an id");
    CHECK(youtubeId(QStringLiteral("/m/Song [Arrangement].mp3")).isEmpty(), "an 11-letter word alone is not an id");

    TrackList t;
    t << mk(QStringLiteral("Bee Gees - Stayin' Alive (Official Video) [I_izvAbhExY].mp3"), 250000)   // 0
      << mk(QStringLiteral("Bee Gees - Stayin' Alive (Official Video)-I_izvAbhExY.mp3"), 250400)     // 1
      << mk(QStringLiteral("Stayin' Alive (Gospel & Choir) [_BBKacJqZCU].mp3"), 250000)              // 2 cover
      << mk(QStringLiteral("Stayin' Alive-Why Mona.mp3"), 201000)                                     // 3
      << mk(QStringLiteral("why mona - Stayin' Alive.mp3"), 201900)                                   // 4
      << mk(QStringLiteral("03 Candy Bars [j-tZgtqypb0].mp3"), 170500)                               // 5
      << mk(QStringLiteral("03 Candy Bars.wma"), 170039, QStringLiteral("Grand Buffet"), QStringLiteral("Candy Bars")) // 6
      << mk(QStringLiteral("Grand Buffet - Candy Bars.mp3"), 171000)                                  // 7
      << mk(QStringLiteral("Some Other Band - Candy Bars.mp3"), 170000)                               // 8 different artist
      << mk(QStringLiteral("Creedence Clearwater Revival - Fortunate Son [ZWijx_AgPiA].mp3"), 140000) // 9
      << mk(QStringLiteral("Creedence Clearwater Revival - Fortunate Son (Live).mp3"), 190000)        // 10 different length
      << mk(QStringLiteral("4K - Scenic Relaxation Film With Calming Music [0xhzwDXfLds].mp3"), 3600000) // 11
      << mk(QStringLiteral("4K - Scenic Relaxation Film With Calming Music [0xhzwDXfLds].webm"), 3600200); // 12

    const auto groups = findGroups(t, nullptr, /*hashFiles=*/false);
    int r = 0;
    CHECK(together(groups, {0, 1}, &r) && (r & SameVideo), "same video in [id] and -id styles");
    CHECK(!grouped(groups, 2), "a cover with a different title stays out");
    CHECK(together(groups, {3, 4}, &r) && (r & SameSong), "\"Title-Artist\" matches \"Artist - Title\"");
    CHECK(!together(groups, {7, 8}), "same title by a different artist is not a duplicate");
    CHECK(together(groups, {6, 7}), "tagged copy matches \"Artist - Title\" file name");
    CHECK(!grouped(groups, 5) || !together(groups, {5, 8}), "an untagged title that fits two artists joins neither wrongly");
    {
        TrackList solo;
        solo << t[5] << t[6] << t[7];  // without the competing artist
        CHECK(together(findGroups(solo, nullptr, false), {0, 1, 2}), "track number / tags / filename variants of one song");
    }
    CHECK(!grouped(groups, 10), "a live version with a different length stays out");
    CHECK(together(groups, {11, 12}, &r) && (r & SameVideo), "mp3 and webm of the same video");

    // Ranking.
    Quality mp3; mp3.codec = QStringLiteral("MPEG-1 Layer 3 (MP3)"); mp3.bitrateKbps = 128; mp3.durationMs = 200000;
    Quality opus; opus.codec = QStringLiteral("Opus"); opus.bitrateKbps = 128; opus.durationMs = 200000;
    Quality flac; flac.codec = QStringLiteral("FLAC"); flac.lossless = true; flac.sampleRate = 44100; flac.durationMs = 200000;
    Quality cut = mp3; cut.bitrateKbps = 320; cut.durationMs = 120000;
    CHECK(score(opus, 3, 200000, Prefer::BestQuality) > score(mp3, 3, 200000, Prefer::BestQuality), "opus 128 beats mp3 128");
    CHECK(score(flac, 9, 200000, Prefer::BestQuality) > score(opus, 3, 200000, Prefer::BestQuality), "lossless beats lossy");
    CHECK(score(cut, 3, 200000, Prefer::BestQuality) < score(mp3, 3, 200000, Prefer::BestQuality), "a truncated 320 loses to a full 128");
    CHECK(score(mp3, 3, 200000, Prefer::SmallestFile) > score(flac, 9, 200000, Prefer::SmallestFile), "smallest-file mode prefers the small one");
    CHECK(score(cut, 1, 200000, Prefer::SmallestFile) < score(flac, 9, 200000, Prefer::SmallestFile), "…but never a truncated copy");

    std::fprintf(stderr, failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
