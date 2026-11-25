// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;
typedef struct _GstMessage GstMessage;

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

Q_SIGNALS:
    void stateChanged(GstEngine::State state);
    void durationChanged(qint64 ms);
    void endOfStream();
    void error(const QString &message);

private:
    static int busSyncHandler(GstBus *bus, GstMessage *msg, void *self);
    void handleMessage(GstMessage *msg);
    void setState(State s);

    GstElement *m_pipeline = nullptr;
    State m_state = State::Stopped;
    double m_volume = 0.8;
    QString m_initError;
};
