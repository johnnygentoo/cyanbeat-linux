// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>
#include <memory>

// How sync reaches the phone: ADB (developer mode) or MTP (plain "File transfer").
// Blocking; created, used and destroyed on one worker thread.
class Transport {
public:
    struct File {
        QString path;  // absolute phone path, e.g. /sdcard/Music/a.mp3
        qint64 size = 0;
    };
    virtual ~Transport() = default;
    virtual bool ok() const = 0;
    virtual QString error() const = 0;
    virtual QVector<File> list(const QString &root, bool *ok) = 0;
    virtual qint64 freeBytes(const QString &path) = 0;
    virtual bool push(const QString &local, const QString &remote, QString *err) = 0;
    virtual bool pull(const QString &remote, const QString &local, QString *err) = 0;
    virtual bool rename(const QString &from, const QString &to, QString *err) = 0;
    virtual bool remove(const QString &remote, QString *err) = 0;

    // "adb:<serial>" or "mtp:<busnum>:<devnum>".
    static std::unique_ptr<Transport> open(const QString &spec);
};
