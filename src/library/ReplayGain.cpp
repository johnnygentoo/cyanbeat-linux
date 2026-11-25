// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/ReplayGain.h"

#include <QFile>
#include <QRegularExpression>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

namespace ReplayGain {

namespace {
bool parseDb(const TagLib::PropertyMap &props, const char *key, double *out)
{
    const auto it = props.find(key);
    if (it == props.end() || it->second.isEmpty())
        return false;
    static const QRegularExpression num(QStringLiteral("([-+]?\\d+(?:[.,]\\d+)?)"));
    const auto m = num.match(QString::fromStdString(it->second.front().to8Bit(true)));
    if (!m.hasMatch())
        return false;
    *out = m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    return true;
}

bool parseR128(const TagLib::PropertyMap &props, const char *key, double *out)
{
    const auto it = props.find(key);
    if (it == props.end() || it->second.isEmpty())
        return false;
    bool ok = false;
    const int q78 = QString::fromStdString(it->second.front().to8Bit(true)).trimmed().toInt(&ok);
    if (!ok)
        return false;
    *out = q78 / 256.0 + 5.0;  // −23 LUFS → ReplayGain's −18
    return true;
}
}

Gains read(const QString &path)
{
    Gains g;
#ifdef Q_OS_WIN
    TagLib::FileRef ref(reinterpret_cast<const wchar_t *>(path.utf16()), false);
#else
    TagLib::FileRef ref(QFile::encodeName(path).constData(), false);
#endif
    if (ref.isNull())
        return g;
    const TagLib::PropertyMap props = ref.properties();
    g.hasTrack = parseDb(props, "REPLAYGAIN_TRACK_GAIN", &g.track) || parseR128(props, "R128_TRACK_GAIN", &g.track);
    g.hasAlbum = parseDb(props, "REPLAYGAIN_ALBUM_GAIN", &g.album) || parseR128(props, "R128_ALBUM_GAIN", &g.album);
    return g;
}

double pick(const Gains &g, Mode mode, double preampDb)
{
    switch (mode) {
    case Mode::Off:
        return 0.0;
    case Mode::Album:
        if (g.hasAlbum)
            return g.album + preampDb;
        [[fallthrough]];
    case Mode::Track:
        return g.hasTrack ? g.track + preampDb : 0.0;
    }
    return 0.0;
}

} // namespace ReplayGain
