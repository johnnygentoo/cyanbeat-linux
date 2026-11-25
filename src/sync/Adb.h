// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Thin, blocking wrapper over the `adb` tool. Call from a worker thread.
namespace Adb {

struct Device {
    QString serial;     // transport id for -s: "R5CY…", "192.168.1.5:5555", "adb-R5CY…._adb-tls-connect._tcp"
    QString state;      // "device", "unauthorized", "offline"
    QString model;      // "SM_S936U"
    bool wireless = false;
};

struct RemoteFile {
    QString path;       // absolute on the device
    qint64 size = 0;
    qint64 mtime = 0;
};

struct Result {
    bool ok = false;
    QString out;
    QString err;
};

QString program();  // settings override, PATH, ~/Android/Sdk/platform-tools, $ANDROID_HOME
bool available();

Result run(const QStringList &args, int timeoutMs = 30000);
QVector<Device> devices();
QStringList mdnsTlsServices();  // "_adb-tls-connect" service names (wireless debugging)
bool connect(const QString &target, QString *message = nullptr);

QString getprop(const QString &serial, const QString &prop);
QString wifiAddress(const QString &serial);  // wlan0 IPv4, "" if none

// Single-quote for the device's /bin/sh.
QString shellQuote(const QString &s);
Result shell(const QString &serial, const QString &command, int timeoutMs = 60000);

// Every regular file under `root` (skipping dot-folders), with size and mtime.
QVector<RemoteFile> listFiles(const QString &serial, const QString &root, bool *ok = nullptr);
qint64 freeBytes(const QString &serial, const QString &path);

bool push(const QString &serial, const QString &local, const QString &remote, QString *error = nullptr);
bool pull(const QString &serial, const QString &remote, const QString &local, QString *error = nullptr);
bool move(const QString &serial, const QString &from, const QString &to, QString *error = nullptr);
bool remove(const QString &serial, const QString &remote, QString *error = nullptr);

} // namespace Adb
