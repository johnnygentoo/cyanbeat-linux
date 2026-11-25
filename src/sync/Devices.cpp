// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Devices.h"

#include "sync/Adb.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

namespace {
QVector<Devices::Phone> loadKnown()
{
    QVector<Devices::Phone> out;
    const QJsonArray arr = QJsonDocument::fromJson(QSettings().value(QStringLiteral("sync/phones")).toByteArray()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Devices::Phone p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.model = o.value(QStringLiteral("model")).toString();
        p.lastIp = o.value(QStringLiteral("lastIp")).toString();
        p.tcpip = o.value(QStringLiteral("tcpip")).toBool();
        if (!p.id.isEmpty())
            out << p;
    }
    return out;
}

void saveKnown(const QVector<Devices::Phone> &phones)
{
    QJsonArray arr;
    for (const Devices::Phone &p : phones)
        arr << QJsonObject{{QStringLiteral("id"), p.id}, {QStringLiteral("model"), p.model},
                           {QStringLiteral("lastIp"), p.lastIp}, {QStringLiteral("tcpip"), p.tcpip}};
    QSettings().setValue(QStringLiteral("sync/phones"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
}

Devices::Devices(QObject *parent) : QObject(parent), m_poll(new QTimer(this))
{
    // CYANBEAT_NO_ADB=1 keeps test/snapshot runs off any connected phone.
    m_adb = Adb::available() && !qEnvironmentVariableIsSet("CYANBEAT_NO_ADB");
    m_phones = loadKnown();
    connect(&m_watcher, &QFutureWatcher<Snapshot>::finished, this, [this] {
        const Snapshot s = m_watcher.result();
        remember(s.phones);
        if (!s.message.isEmpty())
            Q_EMIT message(s.message);
        Q_EMIT changed();
    });
    m_poll->setInterval(10000);  // `adb devices` is cheap; notice plug/unplug within a few seconds
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (!busy())
            refresh();
    });
    if (m_adb) {
        m_poll->start();
        refresh();
    }
}

const Devices::Phone *Devices::phone(const QString &id) const
{
    for (const Phone &p : m_phones)
        if (p.id == id)
            return &p;
    return nullptr;
}

void Devices::startJob(std::function<Snapshot()> job)
{
    if (!m_adb || m_watcher.isRunning())
        return;
    m_watcher.setFuture(QtConcurrent::run(std::move(job)));
}

QVector<Devices::Phone> Devices::scan()
{
    QVector<Phone> out;
    for (const Adb::Device &d : Adb::devices()) {
        Phone p;
        p.transport = d.serial;
        p.wireless = d.wireless;
        p.model = d.model;
        if (d.state == QLatin1String("unauthorized")) {
            p.unauthorized = true;
            p.id = d.serial;
        } else if (d.state == QLatin1String("device")) {
            p.id = Adb::getprop(d.serial, QStringLiteral("ro.serialno"));
            if (p.id.isEmpty())
                p.id = d.serial;
            if (p.model.isEmpty())
                p.model = Adb::getprop(d.serial, QStringLiteral("ro.product.model"));
            p.lastIp = Adb::wifiAddress(d.serial);
        } else {
            continue;  // offline
        }
        p.model.replace(QLatin1Char('_'), QLatin1Char('-'));
        // The same phone on USB and Wi-Fi at once: prefer the cable.
        bool dup = false;
        for (Phone &o : out)
            if (o.id == p.id) {
                dup = true;
                if (o.wireless && !p.wireless)
                    o = p;
            }
        if (!dup)
            out << p;
    }
    return out;
}

void Devices::remember(const QVector<Phone> &connected)
{
    QVector<Phone> merged = connected;
    for (Phone &c : merged) {
        if (c.unauthorized)
            continue;
        for (const Phone &k : std::as_const(m_phones))
            if (k.id == c.id) {
                c.tcpip = k.tcpip;
                if (c.lastIp.isEmpty())
                    c.lastIp = k.lastIp;
            }
    }
    for (const Phone &k : std::as_const(m_phones)) {
        bool present = false;
        for (const Phone &c : std::as_const(merged))
            present = present || c.id == k.id;
        if (!present && !k.unauthorized) {
            Phone off = k;
            off.transport.clear();
            off.wireless = false;
            merged << off;
        }
    }
    m_phones = merged;
    QVector<Phone> keep;
    for (const Phone &p : std::as_const(m_phones))
        if (!p.unauthorized)
            keep << p;
    saveKnown(keep);
}

void Devices::refresh()
{
    startJob([this] { return Snapshot{scan(), {}}; });
}

void Devices::connectWireless(const QString &id)
{
    const Phone *p = phone(id);
    if (!p)
        return;
    const QString lastIp = p->lastIp;
    const bool tcpip = p->tcpip;
    startJob([this, id, lastIp, tcpip] {
        QString msg;
        bool ok = false;
        // 1. Android's Wireless debugging (already paired with this computer): adb
        //    names the mDNS service after the phone's serial.
        for (const QString &svc : Adb::mdnsTlsServices()) {
            if (svc.contains(id) && Adb::connect(svc, &msg)) {
                ok = true;
                break;
            }
        }
        // 2. `adb tcpip 5555` from an earlier cabled session (until the phone reboots).
        if (!ok && tcpip && !lastIp.isEmpty())
            ok = Adb::connect(lastIp + QStringLiteral(":5555"), &msg);
        Snapshot s{scan(), {}};
        if (!ok)
            s.message = tr("Couldn't reach the phone over Wi-Fi. Turn on Developer options › Wireless debugging "
                           "(pair it once with `adb pair`), or plug it in and choose \"Set Up Wi-Fi Sync\".")
                        + (msg.isEmpty() ? QString() : QStringLiteral("\n\nadb: ") + msg);
        return s;
    });
}

void Devices::enableWireless(const QString &id)
{
    const Phone *p = phone(id);
    if (!p || p->transport.isEmpty() || p->wireless)
        return;
    const QString transport = p->transport;
    startJob([this, id, transport] {
        Snapshot s;
        const QString ip = Adb::wifiAddress(transport);
        if (ip.isEmpty()) {
            s.phones = scan();
            s.message = tr("The phone isn't on Wi-Fi.");
            return s;
        }
        Adb::run({QStringLiteral("-s"), transport, QStringLiteral("tcpip"), QStringLiteral("5555")}, 15000);
        QString msg;
        bool ok = false;
        for (int attempt = 0; attempt < 5 && !ok; ++attempt) {
            QThread::msleep(1500);  // adbd restarts in TCP mode
            ok = Adb::connect(ip + QStringLiteral(":5555"), &msg);
        }
        s.phones = scan();
        for (Phone &ph : s.phones)
            if (ph.id == id) {
                ph.lastIp = ip;
                ph.tcpip = ok;
            }
        s.message = ok ? tr("Wi-Fi sync is set up (%1). You can unplug the phone; it lasts until the phone restarts.")
                             .arg(ip)
                       : tr("Couldn't connect over Wi-Fi: %1").arg(msg);
        return s;
    });
}

void Devices::forget(const QString &id)
{
    QVector<Phone> keep;
    for (const Phone &p : std::as_const(m_phones))
        if (p.id != id || !p.transport.isEmpty())
            keep << p;
    m_phones = keep;
    saveKnown(m_phones);
    Q_EMIT changed();
}
