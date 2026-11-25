// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class GstEngine;

// The graphic EQ: the CyanBeat Android app's 8 bands and built-in presets, so a
// preset sounds the same on the phone and the desktop. Boosts are real boosts
// (no hidden "headroom" trim); a soft limiter switches on only while a band is
// above 0 dB, so a flat EQ leaves the audio bit-for-bit untouched.
class Equalizer : public QObject {
    Q_OBJECT
public:
    static const QVector<double> &freqs();       // 60 … 12000 Hz
    static const QVector<double> &bandwidths();  // Hz, from the Android app's per-band Q
    static constexpr double MinDb = -12.0;       // GStreamer's equalizer tops out at +12
    static constexpr double MaxDb = 12.0;
    static QString label(double hz);              // "60", "1k", "2.2k"
    // Response of the whole cascade at `hz` (RBJ peaking biquads, 48 kHz).
    static double responseDb(const QVector<double> &gainsDb, double hz);

    struct Preset {
        QString name;
        QVector<double> gains;
        bool builtIn = false;
    };
    static const QVector<Preset> &builtIns();

    explicit Equalizer(GstEngine *engine, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QVector<double> gains() const { return m_gains; }
    void setGain(int band, double db);
    void setGains(const QVector<double> &db);
    // Name of the preset the curve matches, or "" for a hand-made curve.
    QString presetName() const { return m_preset; }

    QVector<Preset> presets() const;  // built-ins, then yours
    void applyPreset(const QString &name);
    void savePreset(const QString &name);    // the current curve
    void deletePreset(const QString &name);  // yours only

Q_SIGNALS:
    void changed();
    void presetsChanged();

private:
    void apply();
    void save() const;
    void loadCustom();
    void saveCustom() const;

    GstEngine *m_engine;
    bool m_enabled = false;
    QVector<double> m_gains;
    QString m_preset;
    QVector<Preset> m_custom;
};
