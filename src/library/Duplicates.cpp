// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/Duplicates.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <numeric>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <taglib/fileref.h>

namespace Duplicates {

namespace {

// Bracketed bits that say nothing about which song it is.
const QRegularExpression &noiseWords()
{
    static const QRegularExpression re(
        QStringLiteral("\\b(official|video|audio|lyrics?|hd|hq|4k|1080p|720p|visuali[sz]er|mv|m/v|"
                       "explicit|clean|stereo|mono|remaster(ed)?|high quality|full song|music video)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString stripNoise(QString s)
{
    static const QRegularExpression ytBracket(QStringLiteral("\\s*\\[[A-Za-z0-9_-]{11}\\]$"));
    static const QRegularExpression ytSuffix(QStringLiteral("-[A-Za-z0-9_-]{11}$"));
    static const QRegularExpression bracket(QStringLiteral("[\\(\\[\\{]([^\\)\\]\\}]*)[\\)\\]\\}]"));
    static const QRegularExpression trackNo(QStringLiteral("^\\s*\\d{1,3}(?:\\s*[._-]\\s*|\\s+)"));
    s.remove(ytBracket);
    s.remove(ytSuffix);
    QString out;
    qsizetype last = 0;
    for (auto it = bracket.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        if (!noiseWords().match(m.captured(1)).hasMatch())
            out += m.captured(0);
        last = m.capturedEnd();
    }
    out += s.mid(last);
    out.remove(trackNo);
    return out.trimmed();
}

QStringList words(const QString &s)
{
    static const QRegularExpression sep(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QStringList w = s.toLower().split(sep, Qt::SkipEmptyParts);
    w.removeAll(QStringLiteral("ft"));
    w.removeAll(QStringLiteral("feat"));
    w.removeAll(QStringLiteral("featuring"));
    std::sort(w.begin(), w.end());
    w.erase(std::unique(w.begin(), w.end()), w.end());
    return w;
}

// Artist/title as best we know them: tags if the file has an artist tag,
// otherwise "Artist - Title" parsed out of the file name.
void artistTitle(const Track &t, QString *artist, QString *title)
{
    if (!t.artist.isEmpty()) {
        *artist = t.artist;
        *title = stripNoise(t.title);
        return;
    }
    const QString name = stripNoise(t.title);
    const int dash = name.indexOf(QStringLiteral(" - "));
    if (dash > 0) {
        *artist = name.left(dash).trimmed();
        *title = name.mid(dash + 3).trimmed();
    } else {
        artist->clear();
        *title = name;
    }
}

struct UnionFind {
    QVector<int> parent;
    QHash<int, int> reasons;
    explicit UnionFind(int n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int x)
    {
        while (parent[x] != x)
            x = parent[x] = parent[parent[x]];
        return x;
    }
    void unite(int a, int b, int reason)
    {
        a = find(a);
        b = find(b);
        const int r = reasons.value(a) | reasons.value(b) | reason;
        if (a != b) {
            parent[b] = a;
            reasons.remove(b);
        }
        reasons[a] = r;
    }
};

QByteArray fileHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result();
}

double codecFactor(const QString &codec)
{
    const QString c = codec.toLower();
    if (c.contains(QLatin1String("opus")))
        return 1.6;
    if (c.contains(QLatin1String("aac")) || c.contains(QLatin1String("mpeg-4")))
        return 1.3;
    if (c.contains(QLatin1String("vorbis")))
        return 1.25;
    if (c.contains(QLatin1String("windows media")) || c.contains(QLatin1String("wma")))
        return 0.9;
    return 1.0;  // mp3 and the rest
}

} // namespace

// ---------------------------------------------------------------- matching

QString youtubeId(const QString &path)
{
    // yt-dlp puts the id last: "Title [id].ext" (current) or "Title-id.ext" (older).
    // An 11-letter plain word ("[Arrangement]") is not an id.
    static const QRegularExpression bracket(QStringLiteral("\\[([A-Za-z0-9_-]{11})\\]$"));
    static const QRegularExpression suffix(QStringLiteral("-([A-Za-z0-9_-]{11})$"));
    static const QRegularExpression word(QStringLiteral("^[A-Z]?[a-z]+$"));
    const QString base = QFileInfo(path).completeBaseName();
    for (const auto *re : {&bracket, &suffix}) {
        const auto m = re->match(base);
        if (m.hasMatch() && !word.match(m.captured(1)).hasMatch())
            return m.captured(1);
    }
    return {};
}

QString normalizedTitle(const Track &t)
{
    QString artist, title;
    artistTitle(t, &artist, &title);
    return artist.isEmpty() ? title : artist + QStringLiteral(" - ") + title;
}

QStringList songTokens(const Track &t)
{
    QString artist, title;
    artistTitle(t, &artist, &title);
    return words(artist + QLatin1Char(' ') + title);
}

QStringList titleTokens(const Track &t)
{
    QString artist, title;
    artistTitle(t, &artist, &title);
    return words(title);
}

QString artistKey(const Track &t)
{
    QString artist, title;
    artistTitle(t, &artist, &title);
    return words(artist).join(QLatin1Char(' '));
}

bool sameLength(qint64 a, qint64 b)
{
    if (a <= 0 || b <= 0)
        return false;
    const qint64 tolerance = qMax<qint64>(2500, qMax(a, b) / 100);
    return qAbs(a - b) <= tolerance;
}

QVector<RawGroup> findGroups(const TrackList &tracks, const std::atomic_bool *cancel, bool hashFiles)
{
    const int n = int(tracks.size());
    UnionFind uf(n);
    auto cancelled = [cancel] { return cancel && cancel->load(); };

    // 1. Same YouTube video.
    QHash<QString, QVector<int>> byId;
    for (int i = 0; i < n; ++i)
        if (const QString id = youtubeId(tracks[i].path); !id.isEmpty())
            byId[id] << i;
    for (const auto &list : std::as_const(byId))
        for (int k = 1; k < list.size(); ++k)
            uf.unite(list[0], list[k], SameVideo);

    // 2. Byte-identical: only files that share a size get hashed.
    if (hashFiles) {
        QHash<qint64, QVector<int>> bySize;
        for (int i = 0; i < n; ++i)
            if (tracks[i].sizeBytes > 0)
                bySize[tracks[i].sizeBytes] << i;
        for (const auto &list : std::as_const(bySize)) {
            if (list.size() < 2)
                continue;
            QHash<QByteArray, int> firstWithHash;
            for (int i : list) {
                if (cancelled())
                    return {};
                const QByteArray h = fileHash(tracks[i].path);
                if (h.isEmpty())
                    continue;
                if (const auto it = firstWithHash.constFind(h); it != firstWithHash.cend())
                    uf.unite(*it, i, SameBytes);
                else
                    firstWithHash.insert(h, i);
            }
        }
    }

    // 3. Same song: same words (any order — catches "Title-Artist" vs "Artist - Title"),
    //    or the same title where at most one side names an artist. Lengths must agree,
    //    and two different named artists never match (covers stay separate).
    QHash<QString, QVector<int>> bySong, byTitle;
    QVector<QString> artists(n);
    for (int i = 0; i < n; ++i) {
        const QStringList song = songTokens(tracks[i]);
        if (song.isEmpty())
            continue;
        bySong[song.join(QLatin1Char(' '))] << i;
        artists[i] = artistKey(tracks[i]);
        const QStringList title = titleTokens(tracks[i]);
        if (!title.isEmpty())
            byTitle[title.join(QLatin1Char(' '))] << i;
    }
    // Same words in artist+title: any order, lengths must agree.
    for (const auto &bucket : std::as_const(bySong)) {
        if (bucket.size() < 2 || bucket.size() > 60)  // huge buckets = generic titles; skip
            continue;
        for (int a = 0; a < bucket.size(); ++a)
            for (int b = a + 1; b < bucket.size(); ++b)
                if (sameLength(tracks[bucket[a]].durationMs, tracks[bucket[b]].durationMs))
                    uf.unite(bucket[a], bucket[b], SameSong);
    }
    // Same title, one side with no artist: attach it to the named copy, but only
    // when exactly one artist fits — otherwise it would chain two artists'
    // same-named songs into one group.
    for (const auto &bucket : std::as_const(byTitle)) {
        if (bucket.size() < 2 || bucket.size() > 60)
            continue;
        for (int i : bucket) {
            if (!artists[i].isEmpty())
                continue;
            int match = -1;
            QSet<QString> fits;
            for (int j : bucket) {
                if (artists[j].isEmpty() || !sameLength(tracks[i].durationMs, tracks[j].durationMs))
                    continue;
                fits.insert(artists[j]);
                match = j;
            }
            if (fits.size() == 1)
                uf.unite(match, i, SameSong);
        }
    }

    QHash<int, RawGroup> groups;
    for (int i = 0; i < n; ++i) {
        const int root = uf.find(i);
        groups[root].indices << i;
    }
    QVector<RawGroup> out;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (it->indices.size() < 2)
            continue;
        it->reasons = uf.reasons.value(it.key());
        out << *it;
    }
    std::sort(out.begin(), out.end(), [&](const RawGroup &a, const RawGroup &b) {
        return QString::localeAwareCompare(normalizedTitle(tracks[a.indices[0]]),
                                           normalizedTitle(tracks[b.indices[0]])) < 0;
    });
    return out;
}

// ---------------------------------------------------------------- quality

Quality probe(const QString &path, qint64 knownDurationMs)
{
    Quality q;
    q.durationMs = knownDurationMs;
    static const bool ready = [] {
        if (!gst_is_initialized())
            gst_init(nullptr, nullptr);
        gst_pb_utils_init();
        return true;
    }();
    Q_UNUSED(ready);

    if (GstDiscoverer *disc = gst_discoverer_new(8 * GST_SECOND, nullptr)) {
        const QByteArray uri = QUrl::fromLocalFile(path).toEncoded();
        if (GstDiscovererInfo *info = gst_discoverer_discover_uri(disc, uri.constData(), nullptr)) {
            if (gst_discoverer_info_get_result(info) == GST_DISCOVERER_OK) {
                if (const GstClockTime d = gst_discoverer_info_get_duration(info); GST_CLOCK_TIME_IS_VALID(d) && d > 0)
                    q.durationMs = qint64(d / GST_MSECOND);
                GList *videos = gst_discoverer_info_get_video_streams(info);
                for (GList *l = videos; l; l = l->next)
                    if (!gst_discoverer_video_info_is_image(GST_DISCOVERER_VIDEO_INFO(l->data)))
                        q.hasVideo = true;  // cover art doesn't count
                gst_discoverer_stream_info_list_free(videos);

                GList *audios = gst_discoverer_info_get_audio_streams(info);
                if (audios) {
                    auto *a = GST_DISCOVERER_AUDIO_INFO(audios->data);
                    q.sampleRate = int(gst_discoverer_audio_info_get_sample_rate(a));
                    q.channels = int(gst_discoverer_audio_info_get_channels(a));
                    guint br = gst_discoverer_audio_info_get_bitrate(a);
                    if (!br)
                        br = gst_discoverer_audio_info_get_max_bitrate(a);
                    q.bitrateKbps = int(br / 1000);
                    if (GstCaps *caps = gst_discoverer_stream_info_get_caps(GST_DISCOVERER_STREAM_INFO(a))) {
                        if (gchar *desc = gst_pb_utils_get_codec_description(caps)) {
                            q.codec = QString::fromUtf8(desc);
                            g_free(desc);
                        }
                        if (gst_caps_get_size(caps) > 0) {
                            const QByteArray name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
                            q.lossless = name == "audio/x-flac" || name == "audio/x-raw" || name == "audio/x-alac"
                                         || name == "audio/x-wavpack" || name == "audio/x-ape"
                                         || name == "audio/x-true-hd";
                        }
                        gst_caps_unref(caps);
                    }
                }
                gst_discoverer_stream_info_list_free(audios);
            }
            gst_discoverer_info_unref(info);
        }
        g_object_unref(disc);
    }

    if (q.bitrateKbps == 0) {
#ifdef Q_OS_WIN
        TagLib::FileRef ref(reinterpret_cast<const wchar_t *>(path.utf16()), true, TagLib::AudioProperties::Average);
#else
        TagLib::FileRef ref(QFile::encodeName(path).constData(), true, TagLib::AudioProperties::Average);
#endif
        if (!ref.isNull() && ref.audioProperties())
            q.bitrateKbps = ref.audioProperties()->bitrate();
    }
    if (q.bitrateKbps == 0 && !q.hasVideo && q.durationMs > 0) {
        q.bitrateKbps = int(QFileInfo(path).size() * 8 / q.durationMs);  // bits per ms == kbps
        q.bitrateEstimated = true;
    }
    if (q.codec.isEmpty())
        q.codec = QFileInfo(path).suffix().toUpper();
    return q;
}

double score(const Quality &q, qint64 sizeBytes, qint64 longestInGroupMs, Prefer prefer)
{
    // A copy noticeably shorter than the others is a cut-off download: never the keeper.
    const bool truncated = longestInGroupMs > 0 && q.durationMs > 0 && longestInGroupMs - q.durationMs > 5000;
    if (prefer == Prefer::SmallestFile)
        return (truncated ? -1e12 : 0) - double(sizeBytes);

    double s;
    if (q.lossless)
        s = 2000 + q.sampleRate / 1000.0;
    else if (q.bitrateKbps > 0)
        s = qMin(q.bitrateKbps, 500) * codecFactor(q.codec);
    else
        s = 100 * codecFactor(q.codec);
    if (truncated)
        s *= 0.3;
    if (q.hasVideo)
        s -= 1;  // same sound, but carries a video stream nobody listens to
    return s;
}

QString describe(const Quality &q)
{
    // GStreamer's codec names are long ("MPEG-1 Layer 3 (MP3)"); keep the gist.
    QString codec = q.codec;
    static const QRegularExpression wma(QStringLiteral("Windows Media Audio\\s*(\\d*)"));
    if (codec.contains(QLatin1String("(MP3)")) || codec.contains(QLatin1String("Layer 3")))
        codec = QStringLiteral("MP3");
    else if (const auto m = wma.match(codec); m.hasMatch())
        codec = QStringLiteral("WMA %1").arg(m.captured(1)).trimmed();
    else if (codec.contains(QLatin1String("AAC")))
        codec = QStringLiteral("AAC");
    else if (codec.startsWith(QLatin1String("Free Lossless")))
        codec = QStringLiteral("FLAC");
    QStringList parts;
    parts << codec;
    if (q.lossless)
        parts << QStringLiteral("lossless");
    else if (q.bitrateKbps > 0)
        parts << QStringLiteral("%1%2 kbps").arg(q.bitrateEstimated ? QStringLiteral("~") : QString()).arg(q.bitrateKbps);
    if (q.sampleRate > 0)
        parts << QStringLiteral("%1 kHz").arg(q.sampleRate / 1000.0, 0, 'g', 3);
    if (q.hasVideo)
        parts << QStringLiteral("+ video");
    return parts.join(QStringLiteral(" · "));
}

void rescore(QVector<Group> &groups, const TrackList &tracks, Prefer prefer)
{
    for (Group &g : groups) {
        qint64 longest = 0;
        for (const Member &m : std::as_const(g.members))
            longest = qMax(longest, m.quality.durationMs);
        for (Member &m : g.members)
            m.score = score(m.quality, tracks[m.index].sizeBytes, longest, prefer);
        // Best score wins; ties go to the tagged copy, then the one with more history.
        g.keep = 0;
        for (int k = 1; k < g.members.size(); ++k) {
            const Member &a = g.members[k], &b = g.members[g.keep];
            const Track &ta = tracks[a.index], &tb = tracks[b.index];
            const auto rank = [](const Member &m, const Track &t) {
                return std::make_tuple(m.score, !t.artist.isEmpty(), t.playCount + t.rating);
            };
            if (rank(a, ta) > rank(b, tb))
                g.keep = k;
        }
    }
}

QVector<Group> scan(const TrackList &tracks, Prefer prefer, const std::atomic_bool *cancel,
                    const std::function<void(int, int)> &progress)
{
    const QVector<RawGroup> raw = findGroups(tracks, cancel);
    int total = 0;
    for (const RawGroup &r : raw)
        total += int(r.indices.size());
    int done = 0;
    QVector<Group> out;
    out.reserve(raw.size());
    for (const RawGroup &r : raw) {
        Group g;
        g.reasons = r.reasons;
        for (int i : r.indices) {
            if (cancel && cancel->load())
                return {};
            Member m;
            m.index = i;
            m.quality = probe(tracks[i].path, tracks[i].durationMs);
            g.members << m;
            if (progress)
                progress(++done, total);
        }
        out << g;
    }
    rescore(out, tracks, prefer);
    return out;
}

} // namespace Duplicates
