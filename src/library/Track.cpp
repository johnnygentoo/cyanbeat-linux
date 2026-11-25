// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/Track.h"

#include <QLocale>

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("--:--");
    const qint64 s = ms / 1000;
    const qint64 h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(sec, 2, 10, QLatin1Char('0'));
}

QString formatLibrarySummary(int count, qint64 totalMs, qint64 totalBytes)
{
    const QLocale loc;
    QString out = count == 1 ? QStringLiteral("1 song") : QStringLiteral("%1 songs").arg(loc.toString(count));
    if (count == 0)
        return out;

    const double hours = totalMs / 3'600'000.0;
    if (hours >= 24.0)
        out += QStringLiteral(", %1 days").arg(hours / 24.0, 0, 'f', 1);
    else if (hours >= 1.0)
        out += QStringLiteral(", %1 hours").arg(hours, 0, 'f', 1);
    else
        out += QStringLiteral(", %1 minutes").arg(qRound(totalMs / 60'000.0));

    out += QStringLiteral(", ") + loc.formattedDataSize(totalBytes, 1, QLocale::DataSizeTraditionalFormat);
    return out;
}
