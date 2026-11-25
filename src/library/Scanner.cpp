// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/Scanner.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tdebuglistener.h>
#include <taglib/tpropertymap.h>

namespace {

const QSet<QString> &audioExtensions()
{
    static const QSet<QString> exts = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("alac"),
        QStringLiteral("wav"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("wma"),
        QStringLiteral("ape"), QStringLiteral("wv"), QStringLiteral("mpc"), QStringLiteral("webm"),
        QStringLiteral("mka"), QStringLiteral("mp4"),
    };
    return exts;
}

QString qs(const TagLib::String &s)
{
    return QString::fromStdString(s.to8Bit(true)).trimmed();
}

int leadingInt(const QString &s)
{
    // "3/12" -> 3
    int n = 0;
    for (QChar c : s) {
        if (!c.isDigit())
            break;
        n = n * 10 + c.digitValue();
    }
    return n;
}

// TagLib prints every quirk of every tag to stderr; a 4k-song library is a wall of it.
class QuietTagLib : public TagLib::DebugListener {
public:
    void printMessage(const TagLib::String &) override {}
};

void silenceTagLib()
{
    static QuietTagLib quiet;
    static const bool once = (TagLib::setDebugListener(&quiet), true);
    Q_UNUSED(once);
}

} // namespace

namespace Scanner {

bool isAudioFile(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 && audioExtensions().contains(path.mid(dot + 1).toLower());
}

Track readTrack(const QString &path)
{
    silenceTagLib();
    Track t;
    const QFileInfo fi(path);
    t.path = fi.absoluteFilePath();
    t.sizeBytes = fi.size();
    t.mtime = fi.lastModified().toSecsSinceEpoch();

#ifdef Q_OS_WIN
    TagLib::FileRef ref(reinterpret_cast<const wchar_t *>(t.path.utf16()), true, TagLib::AudioProperties::Fast);
#else
    const QByteArray native = QFile::encodeName(t.path);
    TagLib::FileRef ref(native.constData(), true, TagLib::AudioProperties::Fast);
#endif
    if (!ref.isNull()) {
        if (const TagLib::Tag *tag = ref.tag()) {
            t.title = qs(tag->title());
            t.artist = qs(tag->artist());
            t.album = qs(tag->album());
            t.genre = qs(tag->genre());
            t.year = int(tag->year());
            t.trackNo = int(tag->track());
        }
        const TagLib::PropertyMap props = ref.properties();
        auto first = [&props](const char *key) -> QString {
            const auto it = props.find(key);
            return (it != props.end() && !it->second.isEmpty()) ? qs(it->second.front()) : QString();
        };
        t.albumArtist = first("ALBUMARTIST");
        t.discNo = leadingInt(first("DISCNUMBER"));
        if (const TagLib::AudioProperties *ap = ref.audioProperties())
            t.durationMs = ap->lengthInMilliseconds();
    }
    if (t.title.isEmpty())
        t.title = fi.completeBaseName();
    return t;
}

TrackList scanFolders(const QStringList &folders, const std::atomic_bool *cancel,
                      const QHash<QString, Track> *known)
{
    TrackList out;
    QSet<QString> seen;
    for (const QString &folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            if (cancel && cancel->load())
                return {};
            const QString path = it.next();
            if (!isAudioFile(path))
                continue;
            const QString canon = QFileInfo(path).canonicalFilePath();
            if (canon.isEmpty() || seen.contains(canon))
                continue;
            seen.insert(canon);
            if (known) {
                const auto hit = known->constFind(canon);
                if (hit != known->cend()) {
                    const QFileInfo fi(canon);
                    if (hit->sizeBytes == fi.size() && hit->mtime == fi.lastModified().toSecsSinceEpoch()) {
                        out.push_back(*hit);
                        continue;
                    }
                }
            }
            out.push_back(readTrack(canon));
        }
    }
    return out;
}

} // namespace Scanner
