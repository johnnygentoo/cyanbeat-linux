// SPDX-License-Identifier: GPL-3.0-or-later
// Plays a real file into a fakesink and checks that time moves, pause holds,
// seek lands and end-of-stream fires. Usage: engine_smoke [file]
// (defaults to $CYANBEAT_TEST_FILE; skips with code 77 when none is given).
#include "engine/GstEngine.h"
#include "library/Scanner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <functional>
#include <QFileInfo>
#include <QTimer>
#include <atomic>
#include <cmath>
#include <cstdio>

static bool waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
                           std::fprintf(stderr, "ok: %s\n", msg); } while (0)

int main(int argc, char **argv)
{
    qputenv("CYANBEAT_AUDIOSINK", "fakesink");
    QCoreApplication app(argc, argv);
    QString file = argc > 1 ? QString::fromLocal8Bit(argv[1]) : qEnvironmentVariable("CYANBEAT_TEST_FILE");
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        std::fprintf(stderr, "skip: no test file\n");
        return 77;
    }

    const Track t = Scanner::readTrack(file);
    std::fprintf(stderr, "track: %s / %s / %lld ms\n", qPrintable(t.title), qPrintable(t.artist),
                 static_cast<long long>(t.durationMs));
    CHECK(!t.title.isEmpty(), "scanner produced a title");

    GstEngine e;
    CHECK(e.isValid(), "engine initialised");
    bool eos = false;
    QString err;
    QObject::connect(&e, &GstEngine::endOfStream, [&] { eos = true; });
    QObject::connect(&e, &GstEngine::error, [&](const QString &m) { err = m; });

    e.playFile(file);
    CHECK(waitFor([&] { return e.state() == GstEngine::State::Playing; }, 3000), "reaches Playing");
    CHECK(waitFor([&] { return e.positionMs() > 300; }, 3000), "position advances");
    CHECK(e.durationMs() > 0, "duration known");

    e.pause();
    CHECK(waitFor([&] { return e.state() == GstEngine::State::Paused; }, 2000), "pauses");
    const qint64 held = e.positionMs();
    waitFor([] { return false; }, 400);
    CHECK(qAbs(e.positionMs() - held) < 50, "position holds while paused");

    const qint64 target = qMax<qint64>(0, e.durationMs() - 1500);
    e.seek(target);
    e.play();
    const bool landed = waitFor([&] { return e.positionMs() >= target - 250; }, 3000);
    std::fprintf(stderr, "seek target %lld, now %lld, dur %lld\n", (long long)target, (long long)e.positionMs(), (long long)e.durationMs());
    CHECK(landed, "seek lands near the end");
    CHECK(waitFor([&] { return eos; }, 6000), "end-of-stream fires");

    // EQ + tap: the tap sees post-EQ audio, so boosting every band raises its level.
    CHECK(e.hasEqualizer(), "filter chain built (EQ, limiter, tap)");
    std::atomic<double> sumSq{0};
    std::atomic<qint64> count{0};
    std::atomic<int> tapRate{0};
    e.setPcmTap([&](const float *pcm, int frames, int rate, int channels, qint64) {
        if (!pcm)
            return;
        double acc = 0;
        for (int i = 0; i < frames * channels; ++i)
            acc += double(pcm[i]) * pcm[i];
        sumSq = sumSq + acc;
        count += frames * channels;
        tapRate = rate;
    });
    auto level = [&](double gainDb) {
        const QVector<double> f{60, 120, 400, 1000, 2200, 4000, 8000, 12000};
        QVector<double> bw, g;
        for (double hz : f) { bw << hz; g << gainDb; }
        e.setEqualizer(f, bw, g);
        e.setLimiter(false);
        e.seek(20000);
        e.play();
        waitFor([] { return false; }, 300);  // settle
        sumSq = 0; count = 0;
        waitFor([] { return false; }, 1200);
        return count > 0 ? 10 * std::log10(sumSq / double(count) + 1e-9) : -999.0;
    };
    e.playFile(file);
    waitFor([&] { return e.state() == GstEngine::State::Playing; }, 3000);
    waitFor([] { return false; }, 800);
    CHECK(count > 0 && tapRate > 0, "tap receives audio while playing");
    CHECK(e.positionMs() < 1500, "position still tracks real playback (tap doesn't run the clock ahead)");
    const double flat = level(0), cut = level(-12);
    std::fprintf(stderr, "tap level: flat %.1f dB, all bands -12 dB → %.1f dB\n", flat, cut);
    CHECK(flat - cut > 4.0, "EQ audibly changes the signal (cut by > 4 dB)");
    e.setPcmTap({});

    e.playFile(QStringLiteral("/nonexistent/cyanbeat.mp3"));
    CHECK(waitFor([&] { return !err.isEmpty(); }, 3000), "bad file reports an error");
    CHECK(e.state() == GstEngine::State::Stopped, "error leaves engine stopped");
    return 0;
}
