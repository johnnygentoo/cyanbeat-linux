// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QHash>
#include <QVector>
#include <functional>

class QTimer;

// Phones over ADB: what's connected now, and phones seen before (remembered by
// hardware serial) so one that was cabled once can come back over Wi-Fi.
class Devices : public QObject {
    Q_OBJECT
public:
    struct Phone {
        QString id;         // hardware serial (ro.serialno): the same over USB and Wi-Fi
        QString model;
        QString transport;  // adb -s value while connected, "" if not
        bool wireless = false;
        bool unauthorized = false;  // "allow USB debugging?" not answered yet
        QString lastIp;     // wlan0 address last seen
        bool tcpip = false; // `adb tcpip 5555` was set up from here
        bool mtp = false;   // reachable only over MTP ("File transfer", no developer mode)
    };

    explicit Devices(QObject *parent = nullptr);

    bool adbAvailable() const { return m_adb; }
    const QVector<Phone> &phones() const { return m_phones; }  // connected first, then remembered
    const Phone *phone(const QString &id) const;
    bool busy() const { return m_watcher.isRunning(); }

    void refresh();
    // Reconnect a remembered phone over Wi-Fi: wireless debugging (mDNS) first,
    // then the address it had when `adb tcpip` was set up.
    void connectWireless(const QString &id);
    // While cabled: switch the phone's adb to TCP 5555 and connect over Wi-Fi.
    // (Lasts until the phone reboots; Android's Wireless debugging is the
    // longer-lived alternative and is picked up automatically.)
    void enableWireless(const QString &id);
    void forget(const QString &id);
    // While a sync runs, don't open MTP sessions (a second one breaks the transfer).
    void setPaused(bool paused) { m_paused = paused; }

Q_SIGNALS:
    void changed();
    void message(const QString &text);

private:
    struct Snapshot {
        QVector<Phone> phones;
        QString message;
    };
    void startJob(std::function<Snapshot()> job);
    QVector<Phone> scan();  // worker thread
    void remember(const QVector<Phone> &connected);

    bool m_adb = false;
    bool m_mtp = false;
    bool m_paused = false;
    QHash<QString, QPair<QString, QString>> m_mtpCache;  // location → (serial, model), worker-owned
    QVector<Phone> m_phones;
    QFutureWatcher<Snapshot> m_watcher;
    QTimer *m_poll;
};
