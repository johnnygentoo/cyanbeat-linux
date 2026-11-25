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

    e.playFile(QStringLiteral("/nonexistent/cyanbeat.mp3"));
    CHECK(waitFor([&] { return !err.isEmpty(); }, 3000), "bad file reports an error");
    CHECK(e.state() == GstEngine::State::Stopped, "error leaves engine stopped");
    return 0;
}
