// SPDX-License-Identifier: GPL-3.0-or-later
// Library + Collection behaviour on a throwaway folder of (empty) .mp3 files:
// playlists survive a file disappearing and coming back, reorder/remove act on
// the entries you can see, and play stats survive restarts and tag re-reads.
#include "library/Library.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
                           else std::fprintf(stderr, "ok: %s\n", msg); } while (0)

static void touch(const QString &path, const QByteArray &bytes = {})
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(bytes);
}

static void waitScan(Library &lib)
{
    QEventLoop loop;
    QObject::connect(&lib, &Library::scanFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
}

static QStringList names(Library &lib, const QVector<int> &idx)
{
    QStringList out;
    for (int i : idx)
        out << lib.tracks()[i].title;
    return out;
}

int main(int argc, char **argv)
{
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("config"))));
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CyanBeatTest"));
    QCoreApplication::setApplicationName(QStringLiteral("collection_test"));

    const QString music = tmp.filePath(QStringLiteral("music"));
    QDir().mkpath(music);
    for (const char *n : {"a", "b", "c", "d"})
        touch(music + QLatin1Char('/') + QLatin1String(n) + QStringLiteral(".mp3"));
    QSettings().setValue(QStringLiteral("library/folders"), QStringList{music});
    const QString db = tmp.filePath(QStringLiteral("collection.db"));

    qint64 pid = 0;
    {
        Library lib(db);
        lib.start();
        waitScan(lib);
        CHECK(lib.tracks().size() == 4, "scanned 4 files");
        CHECK(lib.tracks()[0].addedAt > 0, "date added is set");

        auto idx = [&](const char *n) { return lib.indexOfPath(music + QLatin1Char('/') + QLatin1String(n) + QStringLiteral(".mp3")); };
        pid = lib.createPlaylist(QStringLiteral("mix"), {idx("a"), idx("b"), idx("c"), idx("d")});
        CHECK(pid > 0, "playlist created");
        CHECK(names(lib, lib.playlistTracks(pid)) == QStringList({"a", "b", "c", "d"}), "playlist holds a b c d");

        QFile::remove(music + QStringLiteral("/b.mp3"));
        lib.rescan();
        waitScan(lib);
        CHECK(lib.tracks().size() == 3, "b gone from the library");
        CHECK(names(lib, lib.playlistTracks(pid)) == QStringList({"a", "c", "d"}), "playlist shows a c d");

        lib.reorderPlaylist(pid, {idx("d"), idx("a"), idx("c")});
        touch(music + QStringLiteral("/b.mp3"));
        lib.rescan();
        waitScan(lib);
        CHECK(names(lib, lib.playlistTracks(pid)) == QStringList({"d", "b", "a", "c"}),
              "b came back in its old slot after reorder (d b a c)");

        lib.removeFromPlaylist(pid, {0});
        CHECK(names(lib, lib.playlistTracks(pid)) == QStringList({"b", "a", "c"}), "removed d (b a c)");

        lib.recordPlay(idx("a"));
        lib.recordPlay(idx("a"));
        lib.setRating(idx("c"), 4);
        lib.renamePlaylist(pid, QStringLiteral("night drive"));
    }
    {
        Library lib(db);
        lib.start();
        waitScan(lib);
        auto idx = [&](const char *n) { return lib.indexOfPath(music + QLatin1Char('/') + QLatin1String(n) + QStringLiteral(".mp3")); };
        CHECK(lib.tracks()[idx("a")].playCount == 2, "play count persisted");
        CHECK(lib.tracks()[idx("a")].lastPlayed > 0, "last played persisted");
        CHECK(lib.tracks()[idx("c")].rating == 4, "rating persisted");
        const auto lists = lib.playlists();
        CHECK(lists.size() == 1 && lists[0].name == QStringLiteral("night drive"), "playlist rename persisted");
        CHECK(names(lib, lib.playlistTracks(pid)) == QStringList({"b", "a", "c"}), "playlist order persisted");

        // Re-tag / rewrite a: new size + mtime forces a tag re-read; stats must stay.
        touch(music + QStringLiteral("/a.mp3"), QByteArray(64, 'x'));
        QFile f(music + QStringLiteral("/a.mp3"));
        if (f.open(QIODevice::ReadWrite))
            f.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime);
        f.close();
        lib.rescan();
        waitScan(lib);
        CHECK(lib.tracks()[idx("a")].sizeBytes == 64, "a was re-read");
        CHECK(lib.tracks()[idx("a")].playCount == 2, "play count survived the re-read");

        lib.deletePlaylist(pid);
        CHECK(lib.playlists().isEmpty(), "playlist deleted");
        CHECK(lib.playlistTracks(pid).isEmpty(), "its entries went with it");
    }
    std::fprintf(stderr, failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
