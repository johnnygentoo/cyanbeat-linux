// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/Player.h"

#include "library/Library.h"

#include <QHash>
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>

Player::Player(Library *library, QObject *parent)
    : QObject(parent), m_library(library), m_engine(new GstEngine(this)), m_tick(new QTimer(this))
{
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, [this] {
        const qint64 pos = m_engine->positionMs(), dur = durationMs();
        Q_EMIT positionTick(pos, dur);
        // A play counts once it's heard half the song or four minutes, whichever comes first.
        if (!m_playCounted && currentIndex() >= 0 && m_engine->state() == GstEngine::State::Playing) {
            const qint64 threshold = dur > 0 ? qMin<qint64>(dur / 2, 240'000) : 30'000;
            if (pos >= threshold) {
                m_playCounted = true;
                m_library->recordPlay(currentIndex());
            }
        }
    });

    connect(m_engine, &GstEngine::endOfStream, this, &Player::onEndOfStream);
    connect(m_engine, &GstEngine::gaplessAdvanced, this, &Player::onGaplessAdvanced);
    connect(m_engine, &GstEngine::error, this, &Player::onError);
    connect(m_engine, &GstEngine::stateChanged, this, [this](GstEngine::State s) {
        if (s == GstEngine::State::Playing)
            m_tick->start();
        else
            m_tick->stop();
        Q_EMIT positionTick(m_engine->positionMs(), durationMs());
        Q_EMIT stateChanged(s);
    });

    // A rescan rebuilds the track list; keep the queue by path across it.
    connect(m_library, &Library::tracksAboutToChange, this, [this] {
        m_queuePaths.clear();
        for (int li : std::as_const(m_queue))
            m_queuePaths << m_library->tracks()[li].path;
    });
    connect(m_library, &Library::tracksChanged, this, [this] {
        const int before = currentIndex();
        m_queue.clear();
        m_queuePos = -1;
        for (const QString &p : std::as_const(m_queuePaths)) {
            const int li = m_library->indexOfPath(p);
            if (li < 0)
                continue;
            if (p == m_currentPath && m_queuePos < 0)
                m_queuePos = int(m_queue.size());
            m_queue << li;
        }
        m_queuePaths.clear();
        if (currentIndex() != before)
            Q_EMIT currentChanged(currentIndex());
        updatePreload();
    });
}

int Player::currentIndex() const
{
    return (m_queuePos >= 0 && m_queuePos < m_queue.size()) ? m_queue[m_queuePos] : -1;
}

const Track *Player::currentTrack() const
{
    const int li = currentIndex();
    return li >= 0 ? &m_library->tracks()[li] : nullptr;
}

qint64 Player::durationMs() const
{
    // The song's own length first: with a speed effect GStreamer reports the
    // stretched duration (4:53 at 0.8× came back as 6:05) while position stays in
    // song time.
    const Track *t = currentTrack();
    if (t && t->durationMs > 0)
        return t->durationMs;
    return m_engine->durationMs();
}

bool Player::hasNext() const
{
    return !m_queue.isEmpty() && (m_queuePos + 1 < m_queue.size() || m_repeat != Repeat::Off);
}

bool Player::hasPrevious() const
{
    return !m_queue.isEmpty() && m_queuePos >= 0;
}

void Player::playQueue(QVector<int> tracks, int start)
{
    if (start < 0 || start >= tracks.size())
        return;
    int pos = start;
    if (m_shuffle) {
        const int chosen = tracks[start];
        tracks.removeAt(start);
        std::shuffle(tracks.begin(), tracks.end(), *QRandomGenerator::global());
        tracks.prepend(chosen);
        pos = 0;
    }
    m_queue = std::move(tracks);
    m_errorStreak = 0;
    playQueueIndex(pos);
}

void Player::playQueueIndex(int pos)
{
    if (pos < 0 || pos >= m_queue.size())
        return;
    m_queuePos = pos;
    m_playCounted = false;
    const Track &t = m_library->tracks()[m_queue[pos]];
    m_currentPath = t.path;
    applyReplayGain();
    m_engine->playFile(t.path);
    updatePreload();
    Q_EMIT currentChanged(m_queue[pos]);
    Q_EMIT positionTick(0, t.durationMs);
}

int Player::nextPosition() const
{
    if (m_queue.isEmpty() || m_queuePos < 0)
        return -1;
    if (m_repeat == Repeat::One)
        return m_queuePos;
    if (m_queuePos + 1 < m_queue.size())
        return m_queuePos + 1;
    return m_repeat == Repeat::All ? 0 : -1;
}

void Player::updatePreload()
{
    m_preloadPos = nextPosition();
    const Track *t = m_preloadPos >= 0 ? &m_library->tracks()[m_queue[m_preloadPos]] : nullptr;
    m_engine->setNextFile(t ? t->path : QString());
}

void Player::onGaplessAdvanced()
{
    // The engine already started the preloaded song; catch the queue up.
    if (m_preloadPos < 0 || m_preloadPos >= m_queue.size())
        return;
    if (!m_playCounted && currentIndex() >= 0)
        m_library->recordPlay(currentIndex());
    m_errorStreak = 0;
    m_queuePos = m_preloadPos;
    m_playCounted = false;
    m_currentPath = m_library->tracks()[m_queue[m_queuePos]].path;
    applyReplayGain();
    updatePreload();
    Q_EMIT currentChanged(currentIndex());
    Q_EMIT positionTick(0, durationMs());
}

void Player::applyReplayGain()
{
    const Track *t = currentTrack();
    m_engine->setReplayGainDb(t && m_rgMode != ReplayGain::Mode::Off
                                  ? ReplayGain::pick(ReplayGain::read(t->path), m_rgMode, m_rgPreamp)
                                  : 0.0);
}

void Player::setReplayGain(ReplayGain::Mode mode, double preampDb)
{
    m_rgMode = mode;
    m_rgPreamp = preampDb;
    applyReplayGain();
}

void Player::play()
{
    switch (m_engine->state()) {
    case GstEngine::State::Playing:
        return;
    case GstEngine::State::Paused:
        m_engine->play();
        return;
    case GstEngine::State::Stopped:
        if (currentIndex() >= 0)
            playQueueIndex(m_queuePos);
        else if (m_startFallback)
            m_startFallback();
        return;
    }
}

void Player::pause()
{
    m_engine->pause();
}

void Player::togglePlay()
{
    if (m_engine->state() == GstEngine::State::Playing)
        pause();
    else
        play();
}

void Player::stop()
{
    m_engine->stop();
    Q_EMIT positionTick(0, durationMs());
}

void Player::next(bool userInitiated)
{
    if (m_queue.isEmpty())
        return;
    if (!userInitiated && m_repeat == Repeat::One) {
        playQueueIndex(m_queuePos);
        return;
    }
    int pos = m_queuePos + 1;
    if (pos >= m_queue.size()) {
        if (m_repeat == Repeat::Off && !userInitiated) {
            stop();
            m_queuePos = 0;
            Q_EMIT currentChanged(currentIndex());
            return;
        }
        if (m_shuffle)
            std::shuffle(m_queue.begin(), m_queue.end(), *QRandomGenerator::global());
        pos = 0;
    }
    playQueueIndex(pos);
}

void Player::previous()
{
    if (m_queue.isEmpty())
        return;
    if (m_engine->positionMs() > 3000 || m_queuePos <= 0) {
        seek(0);
        return;
    }
    playQueueIndex(m_queuePos - 1);
}

void Player::seek(qint64 ms)
{
    const qint64 dur = durationMs();
    if (dur > 0)
        ms = qBound<qint64>(0, ms, dur);
    m_engine->seek(ms);
    Q_EMIT seeked(ms);
    Q_EMIT positionTick(ms, dur);
}

void Player::onEndOfStream()
{
    m_errorStreak = 0;
    if (!m_playCounted && currentIndex() >= 0) {  // short song that finished between ticks
        m_playCounted = true;
        m_library->recordPlay(currentIndex());
    }
    next(false);
}

void Player::onError(const QString &msg)
{
    Q_EMIT skipped(m_currentPath.section(QLatin1Char('/'), -1), msg);
    // Auto-skip unplayable files, but don't spin through a whole broken library.
    if (++m_errorStreak <= 5)
        QTimer::singleShot(150, this, [this] { next(false); });
}

void Player::setShuffle(bool on)
{
    if (m_shuffle == on)
        return;
    m_shuffle = on;
    // Reorder what's left of the queue so the switch takes effect immediately.
    if (on && m_queuePos >= 0 && m_queuePos + 1 < m_queue.size())
        std::shuffle(m_queue.begin() + m_queuePos + 1, m_queue.end(), *QRandomGenerator::global());
    updatePreload();
    Q_EMIT shuffleChanged(on);
}

void Player::setRepeat(Repeat r)
{
    if (m_repeat == r)
        return;
    m_repeat = r;
    updatePreload();
    Q_EMIT repeatChanged(r);
}

void Player::cycleRepeat()
{
    setRepeat(m_repeat == Repeat::Off ? Repeat::All : m_repeat == Repeat::All ? Repeat::One : Repeat::Off);
}

void Player::setVolume(double v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(v + 1.0, m_engine->volume() + 1.0))
        return;
    m_engine->setVolume(v);
    Q_EMIT volumeChanged(v);
}
