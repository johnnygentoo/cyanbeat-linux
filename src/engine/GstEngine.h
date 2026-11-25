// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;
typedef struct _GstPad GstPad;
typedef struct _GstPadProbeInfo GstPadProbeInfo;

// Audio playback on GStreamer playbin3. Bus messages arrive on GStreamer's
// streaming threads and are marshalled onto the Qt thread, so this works with
// any Qt event dispatcher (including Windows, which has no GLib main loop).
class GstEngine : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };
    Q_ENUM(State)

    explicit GstEngine(QObject *parent = nullptr);
    ~GstEngine() override;

    bool isValid() const { return m_pipeline != nullptr; }
    QString initError() const { return m_initError; }

    // Loads a local file and starts playing it.
    void playFile(const QString &path);
    void play();
    void pause();
    void togglePause();
    void stop();
    void seek(qint64 ms);

    State state() const { return m_state; }
    qint64 positionMs() const;
    qint64 durationMs() const;

    // 0..1 on a perceptual (cubic) curve, like a hardware volume knob.
    void setVolume(double v);
    double volume() const { return m_volume; }

    // ---- audio filters: EQ → limiter → (tap) → output
    bool hasEqualizer() const { return m_eq != nullptr; }
    // One peaking band per entry; gains in dB. Applied live, no gap.
    void setEqualizer(const QVector<double> &freqsHz, const QVector<double> &bandwidthsHz,
                      const QVector<double> &gainsDb);
    // A soft limiter after the EQ so boosts can't clip; off = bit-transparent.
    void setLimiter(bool on);

    // ReplayGain (or any fixed gain) for the current song, in dB.
    void setReplayGainDb(double db);

    // The Android app's FX: speed and pitch independently (0.5–2.0; equal values =
    // tape-style slowdown/speedup), reverb 0..1, bass boost in dB.
    struct Effects {
        double speed = 1.0, pitch = 1.0, reverb = 0.0, bassDb = 0.0;
    };
    bool hasSpeed() const { return m_tempo != nullptr; }   // scaletempo
    bool hasPitch() const { return m_pitch != nullptr; }   // gst-plugins-soundtouch
    bool hasReverb() const { return m_reverb != nullptr; } // freeverb
    void setEffects(const Effects &fx);

    // Gapless: the file to start the moment the current one ends (inside the same
    // stream, no gap). Empty = stop at the end. Emits gaplessAdvanced() when it starts.
    void setNextFile(const QString &path);

    // The audio exactly as it's heard (after EQ + limiter): interleaved float with
    // its rate, channel count and stream time (ms, comparable to positionMs()).
    // It arrives AHEAD of playback, on a GStreamer thread — queue it and release
    // it against positionMs(). A call with pcm == nullptr means "flushed (seek)".
    using PcmTap = std::function<void(const float *pcm, int frames, int rate, int channels, qint64 ptsMs)>;
    void setPcmTap(PcmTap tap);

Q_SIGNALS:
    void stateChanged(GstEngine::State state);
    void durationChanged(qint64 ms);
    void endOfStream();
    void gaplessAdvanced();
    void error(const QString &message);

private:
    static int busSyncHandler(GstBus *bus, GstMessage *msg, void *self);
    void handleMessage(GstMessage *msg);
    void setState(State s);

    void buildFilters();
    void updateLimiter();
    void onAboutToFinish(GstElement *pipeline);
    static int onTapProbe(GstPad *pad, GstPadProbeInfo *info, void *self);

    GstElement *m_pipeline = nullptr;
    GstElement *m_eq = nullptr;       // equalizer-nbands (owned by the filter bin)
    GstElement *m_limiter = nullptr;  // rglimiter
    GstElement *m_rgVolume = nullptr;
    GstElement *m_bass = nullptr;
    GstElement *m_pitch = nullptr;
    GstElement *m_tempo = nullptr;
    GstElement *m_reverb = nullptr;
    bool m_limitEq = false;
    double m_rgDb = 0.0;
    Effects m_fx;
    double m_rate = 1.0;
    bool m_rateStale = false;
    std::mutex m_nextMutex;
    QByteArray m_nextUri;
    std::atomic_bool m_gaplessPending{false};
    std::mutex m_tapMutex;
    std::shared_ptr<PcmTap> m_tap;
    State m_state = State::Stopped;
    double m_volume = 0.8;
    QString m_initError;
};
