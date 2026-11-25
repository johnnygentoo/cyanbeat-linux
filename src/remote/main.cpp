// SPDX-License-Identifier: GPL-3.0-or-later
// cyanbeat-remote — control a running CyanBeat over MPRIS with cmus-remote's
// flags, so scripts written for cmus (waybar buttons, keybinds) work unchanged:
//
//   -p/--play  -u/--pause (toggle)  -s/--stop  -n/--next  -r/--prev
//   -S/--shuffle (toggle)  -R/--repeat (toggle)  -v/--volume VOL  -k/--seek SEEK
//   -Q/--query (cmus-style status)  --raise
//   --or-cmus  if CyanBeat isn't running, hand the same arguments to cmus-remote
//
// VOL: 50, 50%, +5%, -5%.  SEEK: 90, +5, -5, 1:30, +1m, -1h.
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>
#include <cstdio>
#include <unistd.h>

namespace {

const QString kService = QStringLiteral("org.mpris.MediaPlayer2.cyanbeat");
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kProps = QStringLiteral("org.freedesktop.DBus.Properties");

int usage()
{
    std::fputs("usage: cyanbeat-remote [--or-cmus] [-p] [-u] [-s] [-n] [-r] [-S] [-R] [-Q] [--raise]\n"
               "                       [-v VOL] [-k SEEK]\n"
               "  -p --play      play            -u --pause     toggle pause\n"
               "  -s --stop      stop            -n --next      next song\n"
               "  -r --prev      previous song   -S --shuffle   toggle shuffle\n"
               "  -R --repeat    toggle repeat   -Q --query     print status (cmus format)\n"
               "  -v --volume    50 | 50% | +5% | -5%\n"
               "  -k --seek      90 | +5 | -5 | 1:30 | +1m\n"
               "  --raise        bring the window to the front\n"
               "  --or-cmus      if CyanBeat is not running, pass the arguments to cmus-remote\n",
               stderr);
    return 2;
}

QVariantMap getAll(const QString &iface)
{
    QDBusInterface props(kService, kPath, kProps);
    QDBusReply<QVariantMap> r = props.call(QStringLiteral("GetAll"), iface);
    return r.isValid() ? r.value() : QVariantMap();
}

void setProp(const QString &name, const QVariant &v)
{
    QDBusInterface props(kService, kPath, kProps);
    props.call(QStringLiteral("Set"), kPlayer, name, QVariant::fromValue(QDBusVariant(v)));
}

QVariantMap demarshalMap(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(v.value<QDBusArgument>());
    return v.toMap();
}

QString firstString(const QVariant &v)
{
    const QStringList l = v.toStringList();
    return l.isEmpty() ? v.toString() : l.first();
}

void call(const char *method, const QList<QVariant> &args = {})
{
    QDBusInterface player(kService, kPath, kPlayer);
    player.callWithArgumentList(QDBus::Block, QLatin1String(method), args);
}

// Returns the seek target in ms given the current position, or -1 on a bad spec.
qint64 parseSeek(const QString &spec, qint64 posMs)
{
    static const QRegularExpression clock(QStringLiteral("^(\\d+):(\\d{1,2})$"));
    static const QRegularExpression rel(QStringLiteral("^([+-]?)(\\d+)([mh]?)$"));
    if (const auto m = clock.match(spec); m.hasMatch())
        return (m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 1000;
    if (const auto m = rel.match(spec); m.hasMatch()) {
        qint64 secs = m.captured(2).toLongLong();
        if (m.captured(3) == QLatin1String("m"))
            secs *= 60;
        else if (m.captured(3) == QLatin1String("h"))
            secs *= 3600;
        if (m.captured(1) == QLatin1String("+"))
            return posMs + secs * 1000;
        if (m.captured(1) == QLatin1String("-"))
            return qMax<qint64>(0, posMs - secs * 1000);
        return secs * 1000;
    }
    return -1;
}

double parseVolume(const QString &spec, double current)
{
    static const QRegularExpression re(QStringLiteral("^([+-]?)(\\d+(?:\\.\\d+)?)%?$"));
    const auto m = re.match(spec);
    if (!m.hasMatch())
        return -1;
    const double v = m.captured(2).toDouble() / 100.0;
    if (m.captured(1) == QLatin1String("+"))
        return qBound(0.0, current + v, 1.0);
    if (m.captured(1) == QLatin1String("-"))
        return qBound(0.0, current - v, 1.0);
    return qBound(0.0, v, 1.0);
}

void query()
{
    const QVariantMap p = getAll(kPlayer);
    const QVariantMap md = demarshalMap(p.value(QStringLiteral("Metadata")));
    const QString status = p.value(QStringLiteral("PlaybackStatus")).toString().toLower();
    QTextStream out(stdout);
    out << "status " << (status.isEmpty() ? QStringLiteral("stopped") : status) << '\n';
    const QUrl url(md.value(QStringLiteral("xesam:url")).toString());
    if (url.isValid() && !url.isEmpty()) {
        out << "file " << (url.isLocalFile() ? url.toLocalFile() : url.toString()) << '\n';
        out << "duration " << md.value(QStringLiteral("mpris:length")).toLongLong() / 1'000'000 << '\n';
        out << "position " << p.value(QStringLiteral("Position")).toLongLong() / 1'000'000 << '\n';
        auto tag = [&out](const char *name, const QString &value) {
            if (!value.isEmpty())
                out << "tag " << name << ' ' << value << '\n';
        };
        tag("artist", firstString(md.value(QStringLiteral("xesam:artist"))));
        tag("album", md.value(QStringLiteral("xesam:album")).toString());
        tag("title", md.value(QStringLiteral("xesam:title")).toString());
        tag("albumartist", firstString(md.value(QStringLiteral("xesam:albumArtist"))));
        tag("genre", firstString(md.value(QStringLiteral("xesam:genre"))));
        tag("date", md.value(QStringLiteral("xesam:contentCreated")).toString().left(4));
        const int trackNo = md.value(QStringLiteral("xesam:trackNumber")).toInt();
        if (trackNo > 0)
            tag("tracknumber", QString::number(trackNo));
    }
    const QString loop = p.value(QStringLiteral("LoopStatus")).toString();
    const int vol = qRound(p.value(QStringLiteral("Volume")).toDouble() * 100);
    out << "set shuffle " << (p.value(QStringLiteral("Shuffle")).toBool() ? "true" : "false") << '\n';
    out << "set repeat " << (loop == QLatin1String("Playlist") ? "true" : "false") << '\n';
    out << "set repeat_current " << (loop == QLatin1String("Track") ? "true" : "false") << '\n';
    out << "set vol_left " << vol << '\n';
    out << "set vol_right " << vol << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    const bool orCmus = args.removeAll(QStringLiteral("--or-cmus")) > 0;

    QDBusConnection bus = QDBusConnection::sessionBus();
    const bool running = bus.isConnected() && bus.interface()->isServiceRegistered(kService);
    if (!running) {
        if (orCmus) {
            std::vector<QByteArray> store;
            std::vector<char *> cargs;
            store.push_back(QByteArrayLiteral("cmus-remote"));
            for (const QString &a : std::as_const(args))
                store.push_back(a.toLocal8Bit());
            for (QByteArray &b : store)
                cargs.push_back(b.data());
            cargs.push_back(nullptr);
            execvp("cmus-remote", cargs.data());
            std::perror("cyanbeat-remote: cmus-remote");
            return 1;
        }
        std::fputs("cyanbeat-remote: CyanBeat is not running\n", stderr);
        return 1;
    }
    if (args.isEmpty())
        return usage();

    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        auto value = [&]() -> QString {
            return i + 1 < args.size() ? args[++i] : QString();
        };
        if (a == QLatin1String("-p") || a == QLatin1String("--play")) {
            call("Play");
        } else if (a == QLatin1String("-u") || a == QLatin1String("--pause")) {
            call("PlayPause");
        } else if (a == QLatin1String("-s") || a == QLatin1String("--stop")) {
            call("Stop");
        } else if (a == QLatin1String("-n") || a == QLatin1String("--next")) {
            call("Next");
        } else if (a == QLatin1String("-r") || a == QLatin1String("--prev")) {
            call("Previous");
        } else if (a == QLatin1String("-S") || a == QLatin1String("--shuffle")) {
            setProp(QStringLiteral("Shuffle"), !getAll(kPlayer).value(QStringLiteral("Shuffle")).toBool());
        } else if (a == QLatin1String("-R") || a == QLatin1String("--repeat")) {
            const QString loop = getAll(kPlayer).value(QStringLiteral("LoopStatus")).toString();
            setProp(QStringLiteral("LoopStatus"),
                    loop == QLatin1String("None") ? QStringLiteral("Playlist") : QStringLiteral("None"));
        } else if (a == QLatin1String("-v") || a == QLatin1String("--volume")) {
            const QString spec = value();
            const double v = parseVolume(spec, getAll(kPlayer).value(QStringLiteral("Volume")).toDouble());
            if (v < 0) {
                std::fprintf(stderr, "cyanbeat-remote: bad volume '%s'\n", qPrintable(spec));
                return 2;
            }
            setProp(QStringLiteral("Volume"), v);
        } else if (a == QLatin1String("-k") || a == QLatin1String("--seek")) {
            const QString spec = value();
            const QVariantMap p = getAll(kPlayer);
            const qint64 pos = p.value(QStringLiteral("Position")).toLongLong() / 1000;
            const qint64 target = parseSeek(spec, pos);
            if (target < 0) {
                std::fprintf(stderr, "cyanbeat-remote: bad seek '%s'\n", qPrintable(spec));
                return 2;
            }
            const QVariantMap md = demarshalMap(p.value(QStringLiteral("Metadata")));
            const auto track = md.value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>();
            call("SetPosition", {QVariant::fromValue(track), qlonglong(target) * 1000});
        } else if (a == QLatin1String("-Q") || a == QLatin1String("--query")) {
            query();
        } else if (a == QLatin1String("--raise")) {
            QDBusInterface root(kService, kPath, QStringLiteral("org.mpris.MediaPlayer2"));
            root.call(QStringLiteral("Raise"));
        } else if (a == QLatin1String("-h") || a == QLatin1String("--help")) {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "cyanbeat-remote: unknown option '%s'\n", qPrintable(a));
            return usage();
        }
    }
    return 0;
}
