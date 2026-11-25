// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QVector>
#include <deque>
#include <mutex>

class Player;
class QProcess;
class QTimer;

// Spectrum bars for what CyanBeat is playing, in step with what you hear.
//
// Backends:
//  - cava: your cava, fed CyanBeat's own audio through a FIFO (input method
//    "fifo"), bars read from its raw output. Your ~/.config/cava/config supplies
//    [smoothing], [eq] and [color]. Started as plain `cava` with its own
//    XDG_CONFIG_HOME — never `cava -p`, which waybar-cava.sh pkills.
//  - built-in: a small FFT analyzer, for systems without cava (or Windows).
class Visualizer : public QObject {
    Q_OBJECT
public:
    enum class Backend { Cava, BuiltIn };
    static constexpr int BarCount = 48;

    explicit Visualizer(Player *player, QObject *parent = nullptr);
    ~Visualizer() override;

    static bool cavaAvailable();
    Backend backend() const { return m_backend; }
    void setPreferCava(bool on);
    bool preferCava() const { return m_preferCava; }

    const QVector<float> &bars() const { return m_shown; }  // 0..1
    // From your cava config: the gradient if it has one, else the foreground colour.
    const QVector<QColor> &colors() const { return m_colors; }

Q_SIGNALS:
    void barsChanged();
    void backendChanged();

private:
    struct Chunk {
        qint64 ptsMs;
        int rate;
        int channels;
        QVector<float> pcm;  // interleaved
    };

    void onPcm(const float *pcm, int frames, int rate, int channels, qint64 ptsMs);  // GStreamer thread
    void tick();                                                                       // GUI, ~60 Hz
    void release(const Chunk &c);
    void builtInAnalyze();
    void updateRunning();
    void startCava();
    void stopCava();
    void readCava();
    void writeFifo(const Chunk &c);
    void loadUserCavaConfig();

    Player *m_player;
    QTimer *m_timer;
    QTimer *m_idleStop;
    Backend m_backend = Backend::BuiltIn;
    bool m_preferCava = true;

    std::mutex m_mutex;
    std::deque<Chunk> m_pending;
    bool m_flushed = false;

    // built-in analyzer
    QVector<float> m_ring;  // mono, last N samples
    int m_ringPos = 0;
    int m_ringRate = 44100;
    bool m_ringDirty = false;
    float m_sens = 1.0f;

    // cava
    QProcess *m_cava = nullptr;
    QString m_runDir, m_fifoPath;
    int m_fifoFd = -1;
    QByteArray m_cavaBuf;
    QStringList m_userSmoothing, m_userEq;
    bool m_stoppingCava = false;

    QVector<float> m_target, m_shown;
    QVector<QColor> m_colors;
};
