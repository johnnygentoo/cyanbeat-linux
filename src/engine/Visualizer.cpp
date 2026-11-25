// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/Visualizer.h"

#include "engine/Player.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <cmath>
#include <complex>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr int kFft = 2048;
constexpr int kCavaRate = 44100;

void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * float(M_PI) / float(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

QColor parseColor(QString v)
{
    v = v.trimmed();
    v.remove(QLatin1Char('\'')).remove(QLatin1Char('"'));
    const QColor c(v);
    return c.isValid() ? c : QColor();
}
}

Visualizer::Visualizer(Player *player, QObject *parent)
    : QObject(parent), m_player(player), m_timer(new QTimer(this)), m_idleStop(new QTimer(this))
{
    m_target.fill(0.0f, BarCount);
    m_shown.fill(0.0f, BarCount);
    m_ring.fill(0.0f, kFft);
    m_preferCava = QSettings().value(QStringLiteral("visualizer/preferCava"), true).toBool();
    loadUserCavaConfig();

    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &Visualizer::tick);
    m_idleStop->setSingleShot(true);
    m_idleStop->setInterval(5000);  // let cava go after a while paused/stopped
    connect(m_idleStop, &QTimer::timeout, this, &Visualizer::stopCava);

    m_player->engine()->setPcmTap([this](const float *pcm, int frames, int rate, int channels, qint64 pts) {
        onPcm(pcm, frames, rate, channels, pts);
    });
    connect(m_player, &Player::stateChanged, this, &Visualizer::updateRunning);
    updateRunning();
}

Visualizer::~Visualizer()
{
    m_player->engine()->setPcmTap({});
    stopCava();
}

bool Visualizer::cavaAvailable()
{
#ifdef Q_OS_UNIX
    return !QStandardPaths::findExecutable(QStringLiteral("cava")).isEmpty();
#else
    return false;
#endif
}

void Visualizer::setPreferCava(bool on)
{
    m_preferCava = on;
    QSettings().setValue(QStringLiteral("visualizer/preferCava"), on);
    stopCava();
    updateRunning();
}

void Visualizer::loadUserCavaConfig()
{
    // Default look: the JohnnyCyan core colour.
    m_colors = {QColor(0x15, 0xF4, 0xEE)};
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                         + QStringLiteral("/cava/config");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QString section;
    QColor fg;
    bool gradient = false;
    QMap<int, QColor> stops;
    static const QRegularExpression kv(QStringLiteral("^\\s*([A-Za-z0-9_]+)\\s*=\\s*(.*?)\\s*$"));
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            section = line.mid(1, line.indexOf(QLatin1Char(']')) - 1).toLower();
            continue;
        }
        const auto m = kv.match(line);
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1).toLower(), value = m.captured(2);
        if (section == QLatin1String("smoothing"))
            m_userSmoothing << QStringLiteral("%1 = %2").arg(key, value);
        else if (section == QLatin1String("eq"))
            m_userEq << QStringLiteral("%1 = %2").arg(key, value);
        else if (section == QLatin1String("color")) {
            if (key == QLatin1String("foreground"))
                fg = parseColor(value);
            else if (key == QLatin1String("gradient"))
                gradient = value.trimmed() == QLatin1String("1");
            else if (key.startsWith(QLatin1String("gradient_color_")))
                if (const QColor c = parseColor(value); c.isValid())
                    stops.insert(key.mid(15).toInt(), c);
        }
    }
    if (gradient && stops.size() >= 2)
        m_colors = stops.values().toVector();
    else if (fg.isValid())
        m_colors = {fg};
}

// ---------------------------------------------------------------- feeding

void Visualizer::onPcm(const float *pcm, int frames, int rate, int channels, qint64 ptsMs)
{
    std::lock_guard lock(m_mutex);
    if (!pcm) {  // seek / flush
        m_pending.clear();
        m_flushed = true;
        return;
    }
    if (frames <= 0 || channels <= 0 || rate <= 0)
        return;
    Chunk c{ptsMs, rate, channels, QVector<float>(pcm, pcm + frames * channels)};
    m_pending.push_back(std::move(c));
    while (m_pending.size() > 400)  // ~10 s worth: something stalled, don't grow forever
        m_pending.pop_front();
}

void Visualizer::updateRunning()
{
    const bool playing = m_player->state() == GstEngine::State::Playing;
    const Backend want = (m_preferCava && cavaAvailable()) ? Backend::Cava : Backend::BuiltIn;
    if (want != m_backend) {
        m_backend = want;
        Q_EMIT backendChanged();
    }
    if (playing) {
        m_idleStop->stop();
        if (m_backend == Backend::Cava && !m_cava)
            startCava();
        m_timer->start();
    } else if (m_cava) {
        m_idleStop->start();
    }
    if (!m_timer->isActive())
        m_timer->start();  // keep animating so bars fall back down
}

void Visualizer::tick()
{
    const bool playing = m_player->state() == GstEngine::State::Playing;
    const qint64 pos = m_player->positionMs();

    // Release audio that playback has reached; drop what's long gone.
    std::vector<Chunk> due;
    {
        std::lock_guard lock(m_mutex);
        if (m_flushed) {
            m_flushed = false;
            m_ring.fill(0.0f);
        }
        while (!m_pending.empty() && m_pending.front().ptsMs <= pos + 10) {
            if (m_pending.front().ptsMs >= pos - 1000)
                due.push_back(std::move(m_pending.front()));
            m_pending.pop_front();
        }
    }
    for (const Chunk &c : due)
        release(c);

    if (m_backend == Backend::BuiltIn && m_ringDirty) {
        m_ringDirty = false;
        builtInAnalyze();
    }
    if (!playing)
        m_target.fill(0.0f);

    // Display: cava already smooths, so follow it; the built-in gets cava-ish gravity.
    bool any = false;
    for (int i = 0; i < BarCount; ++i) {
        float &s = m_shown[i];
        const float t = m_target[i];
        if (m_backend == Backend::Cava && playing)
            s = t;
        else
            s = t > s ? s + (t - s) * 0.6f : qMax(t, s - 0.025f);
        any = any || s > 0.001f;
    }
    Q_EMIT barsChanged();
    if (!playing && !any)
        m_timer->stop();
}

void Visualizer::release(const Chunk &c)
{
    if (m_backend == Backend::Cava) {
        writeFifo(c);
        return;
    }
    const int frames = int(c.pcm.size() / c.channels);
    for (int i = 0; i < frames; ++i) {
        float sum = 0;
        for (int ch = 0; ch < c.channels; ++ch)
            sum += c.pcm[i * c.channels + ch];
        m_ring[m_ringPos] = sum / c.channels;
        m_ringPos = (m_ringPos + 1) % kFft;
    }
    m_ringRate = c.rate;
    m_ringDirty = true;
}

void Visualizer::builtInAnalyze()
{
    std::vector<std::complex<float>> buf(kFft);
    for (int i = 0; i < kFft; ++i) {
        const float hann = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (kFft - 1));
        buf[i] = m_ring[(m_ringPos + i) % kFft] * hann;
    }
    fft(buf);
    const double nyquist = m_ringRate / 2.0;
    const double lo = 50.0, hi = qMin(16000.0, nyquist * 0.95);
    for (int b = 0; b < BarCount; ++b) {
        const double f0 = lo * std::pow(hi / lo, double(b) / BarCount);
        const double f1 = lo * std::pow(hi / lo, double(b + 1) / BarCount);
        int k0 = int(f0 / nyquist * (kFft / 2)), k1 = int(f1 / nyquist * (kFft / 2));
        k0 = qBound(1, k0, kFft / 2 - 1);
        k1 = qBound(k0 + 1, k1, kFft / 2);
        float peak = 0;
        for (int k = k0; k < k1; ++k)
            peak = qMax(peak, std::abs(buf[k]));
        // Like cava: linear magnitude (square-rooted to tame the range), treble
        // weighted up because it carries less energy, then auto-gain below.
        const double weight = 1.0 + 2.5 * double(b) / BarCount;
        m_target[b] = float(std::sqrt(peak / (kFft / 2.0)) * weight * m_sens);
    }
    // cava-style autosens: back off when bars hit the top, creep up when they don't.
    float peak = 0;
    for (float t : m_target)
        peak = qMax(peak, t);
    if (peak > 0.98f)
        m_sens *= 0.97f;
    else if (peak > 0.05f)
        m_sens = qMin(m_sens * 1.004f, 20.0f);
    for (float &t : m_target)
        t = qMin(t, 1.0f);
}

// ---------------------------------------------------------------- cava

void Visualizer::startCava()
{
#ifdef Q_OS_UNIX
    if (m_cava)
        return;
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    m_runDir = (runtime.isEmpty() ? QDir::tempPath() : runtime)
               + QStringLiteral("/cyanbeat-%1").arg(QCoreApplication::applicationPid());
    QDir().mkpath(m_runDir + QStringLiteral("/cava"));
    m_fifoPath = m_runDir + QStringLiteral("/pcm.fifo");
    ::unlink(QFile::encodeName(m_fifoPath).constData());
    if (::mkfifo(QFile::encodeName(m_fifoPath).constData(), 0600) != 0) {
        qWarning("CyanBeat: mkfifo failed (%s); using the built-in visualizer", strerror(errno));
        m_backend = Backend::BuiltIn;
        Q_EMIT backendChanged();
        return;
    }

    QStringList cfg = {
        QStringLiteral("[general]"), QStringLiteral("framerate = 60"),
        QStringLiteral("bars = %1").arg(BarCount), QStringLiteral("autosens = 1"), QStringLiteral("sleep_timer = 0"),
        QStringLiteral("[input]"), QStringLiteral("method = fifo"), QStringLiteral("source = %1").arg(m_fifoPath),
        QStringLiteral("sample_rate = %1").arg(kCavaRate), QStringLiteral("sample_bits = 16"),
        QStringLiteral("[output]"), QStringLiteral("method = raw"), QStringLiteral("raw_target = /dev/stdout"),
        QStringLiteral("data_format = ascii"), QStringLiteral("ascii_max_range = 1000"),
        QStringLiteral("bar_delimiter = 59"), QStringLiteral("frame_delimiter = 10"),
        QStringLiteral("channels = mono"), QStringLiteral("mono_option = average"),
    };
    if (!m_userSmoothing.isEmpty())
        cfg << QStringLiteral("[smoothing]") << m_userSmoothing;
    if (!m_userEq.isEmpty())
        cfg << QStringLiteral("[eq]") << m_userEq;
    QFile out(m_runDir + QStringLiteral("/cava/config"));
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(cfg.join(QLatin1Char('\n')).toUtf8() + '\n');
    out.close();

    m_cava = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_runDir);  // our config, no -p on the command line
    m_cava->setProcessEnvironment(env);
    m_cava->setProgram(QStandardPaths::findExecutable(QStringLiteral("cava")));
    m_cava->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_cava, &QProcess::readyReadStandardOutput, this, &Visualizer::readCava);
    connect(m_cava, &QProcess::finished, this, [this] {
        if (m_stoppingCava)
            return;
        // Died (or got killed): carry on with the built-in analyzer, try cava again later.
        qWarning("CyanBeat: cava exited; using the built-in visualizer for now");
        const QString err = QString::fromUtf8(m_cava->readAllStandardError()).trimmed();
        if (!err.isEmpty())
            qWarning("cava: %s", qPrintable(err.left(300)));
        stopCava();
        m_backend = Backend::BuiltIn;
        Q_EMIT backendChanged();
        QTimer::singleShot(10000, this, &Visualizer::updateRunning);
    });
    m_cava->start();
#endif
}

void Visualizer::stopCava()
{
#ifdef Q_OS_UNIX
    m_stoppingCava = true;
    if (m_cava) {
        m_cava->disconnect(this);
        m_cava->terminate();
        if (!m_cava->waitForFinished(500)) {
            m_cava->kill();
            m_cava->waitForFinished(500);
        }
        delete m_cava;
        m_cava = nullptr;
    }
    if (m_fifoFd >= 0) {
        ::close(m_fifoFd);
        m_fifoFd = -1;
    }
    if (!m_runDir.isEmpty()) {
        QDir(m_runDir).removeRecursively();
        m_runDir.clear();
    }
    m_cavaBuf.clear();
    m_stoppingCava = false;
#endif
}

void Visualizer::readCava()
{
    m_cavaBuf += m_cava->readAllStandardOutput();
    const int end = int(m_cavaBuf.lastIndexOf('\n'));
    if (end < 0)
        return;
    const int start = int(m_cavaBuf.lastIndexOf('\n', end - 1)) + 1;
    const QList<QByteArray> vals = m_cavaBuf.mid(start, end - start).split(';');
    m_cavaBuf.remove(0, end + 1);
    for (int i = 0; i < BarCount && i < vals.size(); ++i)
        m_target[i] = qBound(0.0f, vals[i].toFloat() / 1000.0f, 1.0f);
}

void Visualizer::writeFifo(const Chunk &c)
{
#ifdef Q_OS_UNIX
    if (!m_cava || m_fifoPath.isEmpty())
        return;
    if (m_fifoFd < 0) {
        // Fails (ENXIO) until cava has the read end open; just try again next time.
        m_fifoFd = ::open(QFile::encodeName(m_fifoPath).constData(), O_WRONLY | O_NONBLOCK);
        if (m_fifoFd < 0)
            return;
    }
    // cava's FIFO input wants 44.1 kHz stereo S16; resample linearly if needed.
    const int inFrames = int(c.pcm.size() / c.channels);
    const double step = double(c.rate) / kCavaRate;
    const int outFrames = int(inFrames / step);
    QVector<qint16> out(outFrames * 2);
    for (int i = 0; i < outFrames; ++i) {
        const double x = i * step;
        const int i0 = qMin(int(x), inFrames - 1), i1 = qMin(i0 + 1, inFrames - 1);
        const float frac = float(x - i0);
        for (int ch = 0; ch < 2; ++ch) {
            const int src = qMin(ch, c.channels - 1);
            const float a = c.pcm[i0 * c.channels + src], b = c.pcm[i1 * c.channels + src];
            out[i * 2 + ch] = qint16(qBound(-1.0f, a + (b - a) * frac, 1.0f) * 32767.0f);
        }
    }
    const ssize_t n = ::write(m_fifoFd, out.constData(), size_t(out.size()) * sizeof(qint16));
    if (n < 0 && errno == EPIPE) {  // cava went away
        ::close(m_fifoFd);
        m_fifoFd = -1;
    }
    // EAGAIN (pipe full): drop this chunk; the visualizer can afford it, playback isn't affected.
#else
    Q_UNUSED(c);
#endif
}
