// SPDX-License-Identifier: GPL-3.0-or-later
// Real-phone identity check (read-only: device lists, serials, model, wlan IP).
// A phone on adb + MTP at once is ONE phone; seen later over MTP only, it keeps
// the adb id (and so its sync history). Needs CYANBEAT_TEST_PHONE=<ro.serialno>.
#include "sync/Devices.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
                           else std::fprintf(stderr, "ok: %s\n", msg); } while (0)

static void waitChanged(Devices &d)
{
    QEventLoop loop;
    QObject::connect(&d, &Devices::changed, &loop, &QEventLoop::quit);
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char **argv)
{
    const QString serial = qEnvironmentVariable("CYANBEAT_TEST_PHONE");
    if (serial.isEmpty()) {
        std::fprintf(stderr, "skip: set CYANBEAT_TEST_PHONE\n");
        return 77;
    }
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("config"))));
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CyanBeatTest"));
    QCoreApplication::setApplicationName(QStringLiteral("devices_test"));

    {
        Devices d;
        waitChanged(d);
        int count = 0;
        const Devices::Phone *p = d.phone(serial);
        for (const auto &ph : d.phones())
            count += !ph.transport.isEmpty() && (ph.id == serial || ph.mtp);
        if (p)
            std::fprintf(stderr, "  adb+mtp: id %s, transport %s, mtp serial %s\n", qPrintable(p->id),
                         qPrintable(p->transport), qPrintable(p->mtpSerial));
        CHECK(p && !p->mtp && !p->transport.startsWith(QLatin1String("mtp:")), "on both: listed once, via adb");
        CHECK(count == 1, "no second entry for the MTP side");
        CHECK(p && !p->mtpSerial.isEmpty(), "the MTP serial is learned and remembered");
    }
    qputenv("CYANBEAT_ONLY_MTP", "1");
    {
        Devices d;
        waitChanged(d);
        const Devices::Phone *p = d.phone(serial);
        if (p)
            std::fprintf(stderr, "  mtp only: id %s, transport %s, model %s\n", qPrintable(p->id),
                         qPrintable(p->transport), qPrintable(p->model));
        CHECK(p && p->mtp && p->transport.startsWith(QLatin1String("mtp:")),
              "MTP only: same phone id as over adb (sync history carries over)");
    }
    std::fprintf(stderr, failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
