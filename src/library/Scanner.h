// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "library/Track.h"

#include <QHash>
#include <QStringList>
#include <atomic>

namespace Scanner {

bool isAudioFile(const QString &path);

// Reads tags + duration with TagLib. Always returns a usable Track: files TagLib
// can't parse still get a title from their file name.
Track readTrack(const QString &path);

// Walks every folder recursively. Safe to call from a worker thread.
// Files whose size + mtime match an entry in `known` reuse it instead of
// re-reading tags (a cold tag read over 34 GB on a spinning disk is ~75 s;
// a stat pass is ~1 s). Either pointer may be null.
TrackList scanFolders(const QStringList &folders, const std::atomic_bool *cancel = nullptr,
                      const QHash<QString, Track> *known = nullptr);

} // namespace Scanner
