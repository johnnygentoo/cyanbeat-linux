// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync/Adb.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace Adb {

QString program()
{
    const QString custom = QSettings().value(QStringLiteral("sync/adbPath")).toString();
    if (!custom.isEmpty() && QFileInfo(custom).isExecutable())
        return custom;
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("adb"));
    if (!onPath.isEmpty())
        return onPath;
    QStringList candidates;
    for (const char *var : {"ANDROID_HOME", "ANDROID_SDK_ROOT"})
        if (const QString v = qEnvironmentVariable(var); !v.isEmpty())
            candidates << v + QStringLiteral("/platform-tools/adb");
    candidates << QDir::homePath() + QStringLiteral("/Android/Sdk/platform-tools/adb");
    for (const QString &c : candidates)
        if (QFileInfo(c).isExecutable())
            return c;
    return {};
}

bool available()
{
    return !program().isEmpty();
}

Result run(const QStringList &args, int timeoutMs)
{
    Result r;
    const QString adb = program();
    if (adb.isEmpty()) {
        r.err = QStringLiteral("adb not found");
        return r;
    }
    QProcess p;
    p.start(adb, args);
    if (!p.waitForStarted(5000)) {
        r.err = p.errorString();
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        r.err = QStringLiteral("adb timed out");
        return r;
    }
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    r.err = QString::fromUtf8(p.readAllStandardError());
    r.ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return r;
}

QVector<Device> devices()
{
    QVector<Device> out;
    const Result r = run({QStringLiteral("devices"), QStringLiteral("-l")}, 10000);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString &line : r.out.split(QLatin1Char('\n'))) {
        const QStringList parts = line.trimmed().split(ws, Qt::SkipEmptyParts);
        if (parts.size() < 2 || line.startsWith(QLatin1String("List of")) || line.startsWith(QLatin1Char('*')))
            continue;
        Device d;
        d.serial = parts[0];
        d.state = parts[1];
        for (const QString &kv : parts.mid(2))
            if (kv.startsWith(QLatin1String("model:")))
                d.model = kv.mid(6);
        d.wireless = d.serial.contains(QLatin1Char(':')) || d.serial.contains(QLatin1String("._adb-tls-connect."));
        out << d;
    }
    return out;
}

QStringList mdnsTlsServices()
{
    QStringList out;
    const Result r = run({QStringLiteral("mdns"), QStringLiteral("services")}, 8000);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString &line : r.out.split(QLatin1Char('\n'))) {
        const QStringList parts = line.trimmed().split(ws, Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts[1].startsWith(QLatin1String("_adb-tls-connect")))
            out << parts[0] + QLatin1Char('.') + parts[1].section(QLatin1Char('.'), 0, 1);  // name._adb-tls-connect._tcp
    }
    return out;
}

bool connect(const QString &target, QString *message)
{
    const Result r = run({QStringLiteral("connect"), target}, 15000);
    const QString text = (r.out + r.err).trimmed();
    if (message)
        *message = text;
    return r.ok && (text.contains(QLatin1String("connected to")) && !text.contains(QLatin1String("failed")));
}

QString shellQuote(const QString &s)
{
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

Result shell(const QString &serial, const QString &command, int timeoutMs)
{
    return run({QStringLiteral("-s"), serial, QStringLiteral("shell"), command}, timeoutMs);
}

QString getprop(const QString &serial, const QString &prop)
{
    return shell(serial, QStringLiteral("getprop ") + prop, 10000).out.trimmed();
}

QString wifiAddress(const QString &serial)
{
    const Result r = shell(serial, QStringLiteral("ip -f inet addr show wlan0"), 10000);
    static const QRegularExpression inet(QStringLiteral("inet (\\d+\\.\\d+\\.\\d+\\.\\d+)"));
    const auto m = inet.match(r.out);
    return m.hasMatch() ? m.captured(1) : QString();
}

QVector<RemoteFile> listFiles(const QString &serial, const QString &root, bool *ok)
{
    QVector<RemoteFile> out;
    // toybox find + stat; '|' separates size and mtime, the rest of the line is the path.
    const QString cmd = QStringLiteral("find %1 -type f -not -path '*/.*' -exec stat -c '%s|%Y|%n' {} + 2>/dev/null")
                            .arg(shellQuote(root));
    const Result r = shell(serial, cmd, 300000);
    if (ok)
        *ok = r.ok || !r.out.isEmpty();
    for (const QString &line : r.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const int a = int(line.indexOf(QLatin1Char('|')));
        const int b = a < 0 ? -1 : int(line.indexOf(QLatin1Char('|'), a + 1));
        if (b < 0)
            continue;
        RemoteFile f;
        f.size = line.left(a).toLongLong();
        f.mtime = line.mid(a + 1, b - a - 1).toLongLong();
        f.path = line.mid(b + 1);
        if (f.path.endsWith(QLatin1Char('\r')))
            f.path.chop(1);
        out << f;
    }
    return out;
}

qint64 freeBytes(const QString &serial, const QString &path)
{
    const Result r = shell(serial, QStringLiteral("df -k %1 | tail -1").arg(shellQuote(path)), 10000);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const QStringList parts = r.out.trimmed().split(ws, Qt::SkipEmptyParts);
    return parts.size() >= 4 ? parts[3].toLongLong() * 1024 : -1;
}

bool push(const QString &serial, const QString &local, const QString &remote, QString *error)
{
    // adb takes the remote path literally (no shell), so odd names are fine.
    const Result r = run({QStringLiteral("-s"), serial, QStringLiteral("push"), local, remote}, 30 * 60 * 1000);
    if (!r.ok && error)
        *error = (r.err + r.out).trimmed();
    return r.ok;
}

bool pull(const QString &serial, const QString &remote, const QString &local, QString *error)
{
    const Result r = run({QStringLiteral("-s"), serial, QStringLiteral("pull"), QStringLiteral("-a"), remote, local},
                         30 * 60 * 1000);
    if (!r.ok && error)
        *error = (r.err + r.out).trimmed();
    return r.ok;
}

bool move(const QString &serial, const QString &from, const QString &to, QString *error)
{
    const Result r = shell(serial, QStringLiteral("mv -f %1 %2").arg(shellQuote(from), shellQuote(to)));
    if (!r.ok && error)
        *error = (r.err + r.out).trimmed();
    return r.ok;
}

bool remove(const QString &serial, const QString &remote, QString *error)
{
    const Result r = shell(serial, QStringLiteral("rm -f %1").arg(shellQuote(remote)));
    if (!r.ok && error)
        *error = (r.err + r.out).trimmed();
    return r.ok;
}

} // namespace Adb
