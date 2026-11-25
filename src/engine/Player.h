// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "engine/GstEngine.h"
#include "library/ReplayGain.h"

#include <QObject>
#include <QStringList>
#include <QVector>
#include <functional>

class Library;
class QTimer;
struct Track;

// The play queue on top of the engine: what's playing, what's next, shuffle,
// repeat, volume, play counting. The window, MPRIS and cyanbeat-remote all
// drive playback through this.
class Player : public QObject {
    Q_OBJECT
public:
    enum class Repeat { Off, All, One };
    Q_ENUM(Repeat)

    explicit Player(Library *library, QObject *parent = nullptr);

    GstEngine *engine() const { return m_engine; }

    // Replace the queue with `tracks` (library indices) and start at `start`.
    // With shuffle on, `start` plays first and the rest is shuffled.
    void playQueue(QVector<int> tracks, int start);
    // Called by play()/togglePlay() when nothing is queued yet (e.g. "play the selection").
    void setStartFallback(std::function<void()> fn) { m_startFallback = std::move(fn); }

    void play();
    void pause();
    void togglePlay();
    void stop();
    void next(bool userInitiated = true);
    void previous();
    void seek(qint64 ms);

    void setShuffle(bool on);
    void setRepeat(Repeat r);
    void cycleRepeat();
    void setVolume(double v);  // 0..1, perceptual
    void setReplayGain(ReplayGain::Mode mode, double preampDb = 0.0);
    ReplayGain::Mode replayGainMode() const { return m_rgMode; }

    GstEngine::State state() const { return m_engine->state(); }
    bool shuffle() const { return m_shuffle; }
    Repeat repeat() const { return m_repeat; }
    double volume() const { return m_engine->volume(); }
    qint64 positionMs() const { return m_engine->positionMs(); }
    qint64 durationMs() const;
    int currentIndex() const;  // library index, -1 if none
    const Track *currentTrack() const;
    bool hasNext() const;
    bool hasPrevious() const;

Q_SIGNALS:
    void currentChanged(int libraryIndex);
    void stateChanged(GstEngine::State state);
    void positionTick(qint64 posMs, qint64 durMs);  // every 250 ms while playing
    void seeked(qint64 posMs);
    void shuffleChanged(bool on);
    void repeatChanged(Player::Repeat r);
    void volumeChanged(double v);
    void skipped(const QString &file, const QString &error);

private:
    void playQueueIndex(int pos);
    int nextPosition() const;  // what plays when the current song ends, -1 = stop
    void updatePreload();      // tell the engine (gapless)
    void applyReplayGain();
    void onGaplessAdvanced();
    void onEndOfStream();
    void onError(const QString &msg);

    Library *m_library;
    GstEngine *m_engine;
    QTimer *m_tick;
    QVector<int> m_queue;
    int m_queuePos = -1;
    QString m_currentPath;
    QStringList m_queuePaths;  // queue parked as paths while a rescan swaps indices
    bool m_shuffle = false;
    Repeat m_repeat = Repeat::Off;
    int m_errorStreak = 0;
    bool m_playCounted = false;
    std::function<void()> m_startFallback;
    int m_preloadPos = -1;
    ReplayGain::Mode m_rgMode = ReplayGain::Mode::Off;
    double m_rgPreamp = 0.0;
};
