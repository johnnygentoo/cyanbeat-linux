// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

class GstEngine;
class QTimer;

// Vaporwave mode / FX, as in the Android app: one switch, continuous speed,
// pitch, reverb (and bass). Classic = 0.8 speed, 0.8 pitch, big reverb, bass up.
// Like Android, the switch isn't remembered across restarts; the knobs are.
class Vapor : public QObject {
    Q_OBJECT
public:
    explicit Vapor(GstEngine *engine, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    double speed() const { return m_speed; }
    double pitch() const { return m_pitch; }
    double reverb() const { return m_reverb; }
    double bassDb() const { return m_bass; }
    bool tape() const { return m_tape; }  // pitch follows speed

    void setEnabled(bool on);
    void setSpeed(double v);
    void setPitch(double v);
    void setReverb(double v);
    void setBassDb(double v);
    void setTape(bool on);
    void classic();
    void flat();

Q_SIGNALS:
    void changed();
    void enabledChanged(bool on);

private:
    void schedule();  // dragging a slider shouldn't seek on every pixel
    void apply();
    void save() const;

    GstEngine *m_engine;
    QTimer *m_debounce;
    bool m_enabled = false;
    double m_speed = 0.8, m_pitch = 0.8, m_reverb = 0.85, m_bass = 6.0;
    bool m_tape = true;
};
