// SPDX-License-Identifier: GPL-3.0-or-later
// Gapless, speed/pitch FX and ReplayGain, measured on generated 440 Hz tones.
#include "engine/Player.h"
#include "library/Library.h"
#include "library/ReplayGain.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
                           else std::fprintf(stderr, "ok: %s\n", msg); } while (0)

static void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static bool tone(const QString &path, double seconds, double freq = 440)
{
    const int buffers = int(seconds * 44100 / 1024);
    return QProcess::execute(QStringLiteral("gst-launch-1.0"),
                             {QStringLiteral("-q"), QStringLiteral("audiotestsrc"), QStringLiteral("freq=%1").arg(freq),
                              QStringLiteral("volume=0.3"), QStringLiteral("num-buffers=%1").arg(buffers),
                              QStringLiteral("samplesperbuffer=1024"), QStringLiteral("!"),
                              QStringLiteral("audio/x-raw,rate=44100,channels=2"), QStringLiteral("!"),
                              QStringLiteral("wavenc"), QStringLiteral("!"), QStringLiteral("filesink"),
                              QStringLiteral("location=") + path})
           == 0;
}

int main(int argc, char **argv)
{
    qputenv("CYANBEAT_AUDIOSINK", "fakesink");
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("config"))));
    qputenv("XDG_DATA_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("data"))));
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CyanBeatTest"));
    QCoreApplication::setApplicationName(QStringLiteral("playback_test"));

    const QString music = tmp.filePath(QStringLiteral("music"));
    QDir().mkpath(music);
    CHECK(tone(music + QStringLiteral("/a.wav"), 2) && tone(music + QStringLiteral("/b.wav"), 2)
              && tone(music + QStringLiteral("/c.wav"), 2) && tone(music + QStringLiteral("/long.wav"), 12),
          "generated test tones");
    // b gets a ReplayGain tag of −6 dB.
    {
        TagLib::FileRef f(QFile::encodeName(music + QStringLiteral("/b.wav")).constData());
        TagLib::PropertyMap p = f.properties();
        p.replace("REPLAYGAIN_TRACK_GAIN", TagLib::StringList(TagLib::String("-6.00 dB")));
        f.setProperties(p);
        f.save();
    }
    const auto g = ReplayGain::read(music + QStringLiteral("/b.wav"));
    CHECK(g.hasTrack && std::abs(g.track + 6.0) < 0.01, "ReplayGain tag read back (-6 dB)");

    QSettings().setValue(QStringLiteral("library/folders"), QStringList{music});
    Library lib;
    QEventLoop wait;
    QObject::connect(&lib, &Library::scanFinished, &wait, &QEventLoop::quit);
    lib.start();
    wait.exec();
    auto idx = [&](const char *n) { return lib.indexOfPath(music + QLatin1Char('/') + QLatin1String(n) + QStringLiteral(".wav")); };

    Player player(&lib);
    std::mutex mu;
    double sumSq = 0;
    qint64 samples = 0, crossings = 0;
    float last = 0;
    int rate = 0;
    player.engine()->setPcmTap([&](const float *pcm, int frames, int r, int ch, qint64) {
        if (!pcm)
            return;
        std::lock_guard l(mu);
        rate = r;
        for (int i = 0; i < frames; ++i) {
            const float v = pcm[i * ch];
            sumSq += double(v) * v;
            if ((last < 0) != (v < 0))
                ++crossings;
            last = v;
            ++samples;
        }
    });
    auto measure = [&](int ms, double *db, double *hz) {
        {
            std::lock_guard l(mu);
            sumSq = 0;
            samples = crossings = 0;
        }
        pump(ms);
        std::lock_guard l(mu);
        *db = samples ? 10 * std::log10(sumSq / samples + 1e-12) : -999;
        *hz = samples && rate ? crossings / 2.0 / (double(samples) / rate) : 0;
    };

    // ---- gapless
    int changes = 0, eos = 0;
    QVector<int> order;
    QElapsedTimer since;
    since.start();
    QObject::connect(&player, &Player::currentChanged, [&](int li) {
        ++changes;
        order << li;
        std::fprintf(stderr, "  t=%lld current → %d (pos %lld)\n", (long long)since.elapsed(), li, (long long)player.positionMs());
    });
    QObject::connect(player.engine(), &GstEngine::endOfStream, [&] { ++eos; });
    QElapsedTimer clock;
    clock.start();
    since.restart();
    player.playQueue({idx("a"), idx("b"), idx("c")}, 0);
    while (clock.elapsed() < 9000 && eos == 0)
        pump(20);
    std::fprintf(stderr, "  queue ran %lld ms, %d track changes, %d EOS\n", (long long)clock.elapsed(), changes, eos);
    CHECK(order.mid(0, 3) == QVector<int>({idx("a"), idx("b"), idx("c")}), "gapless: a → b → c in order");
    CHECK(eos == 1, "only one end-of-stream, at the end of the queue");
    CHECK(clock.elapsed() < 6800, "three 2 s songs took about 6 s (no restart gaps)");

    // ---- ReplayGain
    player.setReplayGain(ReplayGain::Mode::Track);
    double dbA, dbB, hz;
    player.playQueue({idx("a")}, 0);
    pump(500);
    measure(800, &dbA, &hz);
    player.playQueue({idx("b")}, 0);
    pump(500);
    measure(800, &dbB, &hz);
    std::fprintf(stderr, "  level a %.2f dB, b (RG -6) %.2f dB\n", dbA, dbB);
    CHECK(std::abs((dbA - dbB) - 6.0) < 0.5, "ReplayGain track mode applies the tag (-6 dB)");
    player.setReplayGain(ReplayGain::Mode::Off);

    // ---- speed / pitch
    if (!player.engine()->hasSpeed()) {
        std::fprintf(stderr, "skip FX: scaletempo not installed\n");
    } else {
        auto run = [&](double speed, double pitch, double *posGain, double *freq) {
            GstEngine::Effects fx;
            fx.speed = speed;
            fx.pitch = pitch;
            player.playQueue({idx("long")}, 0);
            pump(300);
            player.engine()->setEffects(fx);
            pump(400);
            const qint64 p0 = player.positionMs();
            double db;
            measure(2000, &db, freq);
            *posGain = (player.positionMs() - p0) / 2000.0;
            player.stop();
            pump(100);
        };
        double gain, freq;
        run(1.0, 1.0, &gain, &freq);
        std::fprintf(stderr, "  normal: position x%.2f, %.0f Hz\n", gain, freq);
        CHECK(std::abs(gain - 1.0) < 0.1 && std::abs(freq - 440) < 15, "normal: real time, 440 Hz");
        run(0.8, 1.0, &gain, &freq);
        std::fprintf(stderr, "  speed 0.8 keep pitch: position x%.2f, %.0f Hz\n", gain, freq);
        CHECK(std::abs(gain - 0.8) < 0.1 && std::abs(freq - 440) < 15, "speed 0.8: song moves at 0.8x, still 440 Hz");
        run(1.5, 1.0, &gain, &freq);
        std::fprintf(stderr, "  speed 1.5 keep pitch: position x%.2f, %.0f Hz\n", gain, freq);
        CHECK(std::abs(gain - 1.5) < 0.15, "speed 1.5: the song moves at 1.5x");
        CHECK(std::abs(freq - 440) < 15, "speed 1.5, pitch 1.0: pitch stays at 440 Hz");
        if (player.engine()->hasPitch()) {
            run(0.8, 0.8, &gain, &freq);
            std::fprintf(stderr, "  tape 0.8: position x%.2f, %.0f Hz\n", gain, freq);
            CHECK(std::abs(gain - 0.8) < 0.1 && std::abs(freq - 352) < 15, "tape 0.8: 0.8x and 352 Hz");
            run(1.0, 1.25, &gain, &freq);
            std::fprintf(stderr, "  pitch 1.25 only: position x%.2f, %.0f Hz\n", gain, freq);
            CHECK(std::abs(gain - 1.0) < 0.1 && std::abs(freq - 550) < 20, "pitch 1.25 alone: 550 Hz at normal speed");
        } else {
            std::fprintf(stderr, "skip pitch: install gst-plugins-soundtouch\n");
        }
        // Gapless must survive the effect chain at a non-1 rate too.
        GstEngine::Effects slow;
        slow.speed = 0.8;
        player.engine()->setEffects(slow);
        int changes2 = 0;
        QElapsedTimer c2;
        c2.start();
        auto conn = QObject::connect(&player, &Player::currentChanged, [&] { ++changes2; });
        const int eosBefore = eos;
        player.playQueue({idx("a"), idx("b"), idx("c")}, 0);
        while (c2.elapsed() < 12000 && eos == eosBefore)
            pump(20);
        QObject::disconnect(conn);
        std::fprintf(stderr, "  gapless at 0.8x: %lld ms for 6 s of songs, %d changes\n", (long long)c2.elapsed(), changes2);
        CHECK(changes2 >= 3 && c2.elapsed() > 7000 && c2.elapsed() < 7900,
              "with a speed effect, songs still chain (~7.5 s; brief hiccup at each switch)");
        player.engine()->setEffects({});
    }
    std::fprintf(stderr, failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
