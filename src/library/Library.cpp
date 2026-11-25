// SPDX-License-Identifier: GPL-3.0-or-later
#include "library/Library.h"

#include "library/Scanner.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>

Library::Library(QObject *parent) : QObject(parent)
{
    QSettings s;
    if (s.contains(QStringLiteral("library/folders"))) {
        m_folders = s.value(QStringLiteral("library/folders")).toStringList();
    } else {
        // First run: start with the standard Music folder, like a classic jukebox app.
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!music.isEmpty() && QDir(music).exists())
            m_folders << music;
        saveFolders();
    }

    connect(&m_watcher, &QFutureWatcher<TrackList>::finished, this, [this] {
        if (m_rescanQueued) {
            m_rescanQueued = false;
            rescan();
            return;
        }
        if (m_cancel && m_cancel->load())
            return;
        setTracks(m_watcher.result());
        saveCache();
        Q_EMIT scanFinished();
    });
}

Library::~Library()
{
    if (m_cancel)
        m_cancel->store(true);
    m_watcher.waitForFinished();
}

void Library::start()
{
    loadCache();
    rescan();
}

void Library::setTracks(TrackList tracks)
{
    std::sort(tracks.begin(), tracks.end(), [](const Track &a, const Track &b) {
        const int c = QString::localeAwareCompare(a.browseArtist(), b.browseArtist());
        if (c != 0)
            return c < 0;
        if (a.album != b.album)
            return QString::localeAwareCompare(a.album, b.album) < 0;
        if (a.discNo != b.discNo)
            return a.discNo < b.discNo;
        if (a.trackNo != b.trackNo)
            return a.trackNo < b.trackNo;
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    // A rescan that found nothing new must not reset the view under the user.
    const bool same = tracks.size() == m_tracks.size()
        && std::equal(tracks.cbegin(), tracks.cend(), m_tracks.cbegin(), [](const Track &a, const Track &b) {
               return a.path == b.path && a.sizeBytes == b.sizeBytes && a.mtime == b.mtime;
           });
    if (same && !m_tracks.isEmpty())
        return;
    Q_EMIT tracksAboutToChange();
    m_tracks = std::move(tracks);
    Q_EMIT tracksChanged();
}

namespace {
constexpr quint32 kCacheMagic = 0x43424c31;  // "CBL1"
constexpr quint32 kCacheVersion = 1;

QString cachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/library.bin");
}
}

bool Library::loadCache()
{
    QFile f(cachePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&f);
    quint32 magic = 0, version = 0;
    QStringList folders;
    qint64 count = 0;
    in >> magic >> version >> folders >> count;
    if (magic != kCacheMagic || version != kCacheVersion || folders != m_folders || count < 0 || count > 10'000'000)
        return false;
    TrackList tracks;
    tracks.reserve(count);
    for (qint64 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        Track t;
        in >> t.path >> t.title >> t.artist >> t.album >> t.albumArtist >> t.genre >> t.year >> t.trackNo
           >> t.discNo >> t.durationMs >> t.sizeBytes >> t.mtime;
        tracks.push_back(std::move(t));
    }
    if (in.status() != QDataStream::Ok)
        return false;
    setTracks(std::move(tracks));
    return true;
}

void Library::saveCache() const
{
    const QString path = cachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    QDataStream out(&f);
    out << kCacheMagic << kCacheVersion << m_folders << qint64(m_tracks.size());
    for (const Track &t : m_tracks)
        out << t.path << t.title << t.artist << t.album << t.albumArtist << t.genre << t.year << t.trackNo
            << t.discNo << t.durationMs << t.sizeBytes << t.mtime;
    f.commit();
}

void Library::addFolder(const QString &folder)
{
    const QString clean = QDir::cleanPath(folder);
    if (clean.isEmpty() || m_folders.contains(clean))
        return;
    m_folders << clean;
    saveFolders();
    rescan();
}

void Library::removeFolder(const QString &folder)
{
    if (m_folders.removeAll(QDir::cleanPath(folder)) == 0)
        return;
    saveFolders();
    rescan();
}

void Library::rescan()
{
    if (m_watcher.isRunning()) {
        m_rescanQueued = true;
        return;
    }
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const QStringList folders = m_folders;
    auto cancel = m_cancel;
    auto known = std::make_shared<QHash<QString, Track>>();
    known->reserve(m_tracks.size());
    for (const Track &t : m_tracks)
        known->insert(t.path, t);
    Q_EMIT scanStarted();
    m_watcher.setFuture(QtConcurrent::run([folders, cancel, known] {
        return Scanner::scanFolders(folders, cancel.get(), known.get());
    }));
}

qint64 Library::totalDurationMs() const
{
    qint64 sum = 0;
    for (const Track &t : m_tracks)
        sum += t.durationMs;
    return sum;
}

qint64 Library::totalBytes() const
{
    qint64 sum = 0;
    for (const Track &t : m_tracks)
        sum += t.sizeBytes;
    return sum;
}

void Library::saveFolders() const
{
    QSettings().setValue(QStringLiteral("library/folders"), m_folders);
}
