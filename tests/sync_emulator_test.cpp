// SPDX-License-Identifier: GPL-3.0-or-later
// End-to-end sync against a real device. Works only in a throwaway folder it
// creates and removes: /sdcard/Music/cyanbeat-synctest-<pid>.
// Needs CYANBEAT_TEST_ADB_SERIAL (setup + checking go through adb); skips (77)
// without it. CYANBEAT_TEST_SYNC_TRANSPORT=mtp:<bus>:<dev> runs the sync itself
// over MTP instead. CYANBEAT_TEST_COMPARE_ROOT=/sdcard/Music first compares a
// read-only MTP listing of that folder with adb's.
#include "library/Library.h"
#include "sync/Adb.h"
#include "sync/SyncSession.h"
#include "sync/Transport.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
                           else std::fprintf(stderr, "ok: %s\n", msg); } while (0)

template <typename Sig, typename Obj>
static void waitSignal(Obj *o, Sig sig, int ms = 60000)
{
    QEventLoop loop;
    QObject::connect(o, sig, &loop, &QEventLoop::quit);
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

static void writeFile(const QString &path, int bytes)
{
    QDir().mkpath(path.section(QLatin1Char('/'), 0, -2));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QByteArray(bytes, 'x'));
}

static int count(const SyncSession::Plan &p, SyncPlan::Action a)
{
    int n = 0;
    for (const auto &fp : p.folders)
        for (const auto &i : fp.items)
            n += i.action == a;
    return n;
}

int main(int argc, char **argv)
{
    const QString serial = qEnvironmentVariable("CYANBEAT_TEST_ADB_SERIAL");
    if (serial.isEmpty()) {
        std::fprintf(stderr, "skip: set CYANBEAT_TEST_ADB_SERIAL (an emulator)\n");
        return 77;
    }
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("config"))));
    qputenv("XDG_DATA_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("data"))));
    QDir().mkpath(tmp.filePath(QStringLiteral("data")));
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CyanBeatTest"));
    QCoreApplication::setApplicationName(QStringLiteral("sync_test"));
    const QString via = qEnvironmentVariable("CYANBEAT_TEST_SYNC_TRANSPORT", serial);
    std::fprintf(stderr, "sync via %s, checked via adb %s\n", qPrintable(via), qPrintable(serial));

    if (const QString root = qEnvironmentVariable("CYANBEAT_TEST_COMPARE_ROOT"); !root.isEmpty()) {
        // Read-only: the same folder seen through the sync transport and through adb.
        bool okA = false, okB = false;
        const auto t = Transport::open(via);
        CHECK(t && t->ok(), "transport opens");
        QHash<QString, qint64> viaT, viaAdb;
        if (t && t->ok())
            for (const auto &f : t->list(root, &okA))
                viaT.insert(f.path, f.size);
        for (const auto &f : Adb::listFiles(serial, root, &okB))
            viaAdb.insert(f.path, f.size);
        std::fprintf(stderr, "  %s: %lld files via %s, %lld via adb\n", qPrintable(root), (long long)viaT.size(),
                     qPrintable(via), (long long)viaAdb.size());
        int mismatched = 0;
        for (auto it = viaAdb.cbegin(); it != viaAdb.cend(); ++it)
            if (viaT.value(it.key(), -1) != it.value()) {
                if (++mismatched <= 5)
                    std::fprintf(stderr, "    differs: %s\n", qPrintable(it.key()));
            }
        CHECK(okA && viaT.size() == viaAdb.size() && mismatched == 0, "listing matches adb exactly (paths and sizes)");
    }

    const QString local = tmp.filePath(QStringLiteral("music"));
    const QString remote = QStringLiteral("/sdcard/Music/cyanbeat-synctest-%1").arg(QCoreApplication::applicationPid());
    writeFile(local + QStringLiteral("/both.mp3"), 1000);
    writeFile(local + QStringLiteral("/pc only.mp3"), 2000);
    writeFile(local + QStringLiteral("/What? Really: yes.mp3"), 3000);
    writeFile(local + QStringLiteral("/sub/deep.mp3"), 4000);
    writeFile(local + QStringLiteral("/dupe.mp3"), 5000);
    writeFile(local + QStringLiteral("/notes.txt"), 10);  // not audio: never synced

    Adb::shell(serial, QStringLiteral("mkdir -p %1/phone-sub").arg(Adb::shellQuote(remote)));
    Adb::push(serial, local + QStringLiteral("/both.mp3"), remote + QStringLiteral("/both.mp3"));
    Adb::push(serial, local + QStringLiteral("/dupe.mp3"), remote + QStringLiteral("/dupe.mp3"));
    Adb::shell(serial, QStringLiteral("head -c 6000 /dev/zero > %1").arg(Adb::shellQuote(remote + QStringLiteral("/phone only.mp3"))));
    Adb::shell(serial, QStringLiteral("head -c 7000 /dev/zero > %1").arg(Adb::shellQuote(remote + QStringLiteral("/phone-sub/new.opus"))));

    QSettings().setValue(QStringLiteral("library/folders"), QStringList{local});
    SyncSession::setFolders({{local, remote, true}});
    Library lib;
    lib.start();
    waitSignal(&lib, &Library::scanFinished);
    // "dupe.mp3" was removed here on purpose (as Find Duplicates would).
    lib.removeDuplicates({{local + QStringLiteral("/dupe.mp3"), local + QStringLiteral("/both.mp3")}});
    waitSignal(&lib, &Library::scanFinished);

    SyncSession sync(&lib);
    SyncSession::Plan plan;
    QObject::connect(&sync, &SyncSession::planned, [&](const SyncSession::Plan &p) { plan = p; });
    SyncSession::Report report;
    QObject::connect(&sync, &SyncSession::finished, [&](const SyncSession::Report &r) { report = r; });

    sync.scan(via, QStringLiteral("test-phone"), SyncPlan::Mode::Merge);
    waitSignal(&sync, &SyncSession::planned);
    CHECK(count(plan, SyncPlan::Action::Push) == 3, "merge plan: 3 to push (pc only, odd name, sub/deep)");
    CHECK(count(plan, SyncPlan::Action::Pull) == 2, "merge plan: 2 to pull (phone only, phone-sub/new.opus)");
    CHECK(count(plan, SyncPlan::Action::DeleteRemote) == 1, "merge plan: removed duplicate offered for deletion on phone");
    CHECK(plan.freeOnPhone > 0, "free space on phone read");

    sync.run(via, QStringLiteral("test-phone"), plan);
    waitSignal(&sync, &SyncSession::finished, 120000);
    CHECK(report.pushed == 3 && report.pulled == 2 && report.deletedRemote == 1 && report.failed == 0,
          "run: 3 pushed, 2 pulled, 1 deleted on phone, 0 failed");
    if (!report.errors.isEmpty())
        std::fprintf(stderr, "  errors: %s\n", qPrintable(report.errors.join(QStringLiteral("; "))));

    QStringList onPhone;
    for (const auto &f : Adb::listFiles(serial, remote))
        onPhone << f.path.mid(remote.size() + 1);
    onPhone.sort();
    std::fprintf(stderr, "  phone now: %s\n", qPrintable(onPhone.join(QStringLiteral(" | "))));
    CHECK(onPhone.contains(QString::fromUtf8("What？ Really： yes.mp3")), "odd name landed under its full-width form");
    CHECK(onPhone.contains(QStringLiteral("sub/deep.mp3")), "subfolder created on phone");
    CHECK(!onPhone.contains(QStringLiteral("dupe.mp3")), "removed duplicate is gone from the phone");
    bool parts = false;
    for (const QString &f : onPhone)
        parts = parts || f.endsWith(QLatin1String(".cbpart"));
    CHECK(!parts, "no temporary .cbpart files left on the phone");
    CHECK(QFile::exists(local + QStringLiteral("/phone only.mp3")) && QFile::exists(local + QStringLiteral("/phone-sub/new.opus")),
          "pulled files are on this PC");
    CHECK(QFileInfo(local + QStringLiteral("/phone-sub/new.opus")).size() == 7000, "pulled file is complete");
    CHECK(!QFile::exists(local + QStringLiteral("/dupe.mp3")), "removed duplicate was not pulled back");

    // Second scan: everything in step.
    waitSignal(&lib, &Library::scanFinished, 5000);
    sync.scan(via, QStringLiteral("test-phone"), SyncPlan::Mode::Mirror);
    waitSignal(&sync, &SyncSession::planned);
    int pending = 0;
    for (const auto &fp : plan.folders)
        pending += int(fp.items.size());
    CHECK(pending == 0, "after the sync both sides match (nothing to do, even in mirror mode)");

    // Mirror: delete one on each side, mirror should carry both deletions across.
    Adb::remove(serial, remote + QStringLiteral("/pc only.mp3"));
    QFile::remove(local + QStringLiteral("/phone only.mp3"));
    sync.scan(via, QStringLiteral("test-phone"), SyncPlan::Mode::Mirror);
    waitSignal(&sync, &SyncSession::planned);
    CHECK(count(plan, SyncPlan::Action::DeleteLocal) == 1 && count(plan, SyncPlan::Action::DeleteRemote) == 1,
          "mirror: deletion on each side is proposed for the other");
    sync.scan(via, QStringLiteral("test-phone"), SyncPlan::Mode::Merge);
    waitSignal(&sync, &SyncSession::planned);
    CHECK(count(plan, SyncPlan::Action::Push) == 1 && count(plan, SyncPlan::Action::Pull) == 1
              && count(plan, SyncPlan::Action::DeleteLocal) == 0,
          "merge: the same deletions are undone instead (keep olds)");

    Adb::shell(serial, QStringLiteral("rm -rf %1").arg(Adb::shellQuote(remote)));
    bool gone = false;
    Adb::listFiles(serial, remote, &gone);
    std::fprintf(stderr, failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
