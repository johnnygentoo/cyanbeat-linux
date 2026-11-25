// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

// ReplayGain from a file's tags: REPLAYGAIN_TRACK/ALBUM_GAIN ("-6.5 dB"), or
// Opus R128_TRACK/ALBUM_GAIN (Q7.8, relative to −23 LUFS; shifted to ReplayGain's
// −18 reference). Read when a song starts — no rescan needed.
namespace ReplayGain {

enum class Mode { Off, Track, Album };

struct Gains {
    bool hasTrack = false, hasAlbum = false;
    double track = 0.0, album = 0.0;  // dB
};

Gains read(const QString &path);
// The gain to apply: album falls back to track; untagged songs get 0 dB.
double pick(const Gains &g, Mode mode, double preampDb);

} // namespace ReplayGain
