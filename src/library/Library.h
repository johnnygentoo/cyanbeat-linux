// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Track.h"

#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <atomic>
#include <memory>

// The music collection: watched folders + the tracks found in them.
// Kept in memory, with a binary cache so launch is instant and rescans only
// re-read changed files. The SQLite collection (play counts, ratings,
// playlists) replaces the cache later.
class Library : public QObject {
    Q_OBJECT
public:
    explicit Library(QObject *parent = nullptr);
    ~Library() override;

    const TrackList &tracks() const { return m_tracks; }
    QStringList folders() const { return m_folders; }
    bool isScanning() const { return m_watcher.isRunning(); }

    // Shows the cached library immediately, then rescans in the background.
    void start();

    void addFolder(const QString &folder);
    void removeFolder(const QString &folder);
    void rescan();

    qint64 totalDurationMs() const;
    qint64 totalBytes() const;

Q_SIGNALS:
    void scanStarted();
    void tracksAboutToChange();  // last chance to read old indices
    void tracksChanged();
    void scanFinished();         // always, even when nothing changed

private:
    void saveFolders() const;
    bool loadCache();
    void saveCache() const;
    void setTracks(TrackList tracks);

    QStringList m_folders;
    TrackList m_tracks;
    QFutureWatcher<TrackList> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_rescanQueued = false;
};
