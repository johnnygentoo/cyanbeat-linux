// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QHash>
#include <QPair>
#include <QVector>

// MTP phones on USB (libmtp). Paths like /sdcard/Music map onto the primary
// storage ("Music" at its root).
namespace Mtp {

struct Device {
    QString location;  // "<busnum>:<devnum>" (stable while plugged in)
    QString serial;    // matches Android's ro.serialno on most phones
    QString model;
};

bool available();
// Lists phones (opens each briefly to read its serial). Blocking.
QVector<Device> devices(QString *error = nullptr);
// Same, but USB locations already in `cache` (location → serial, model) aren't reopened.
QVector<Device> devicesCached(const QHash<QString, QPair<QString, QString>> &cache);

} // namespace Mtp
