// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/Equalizer.h"

#include "engine/GstEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <cmath>
#include <complex>

const QVector<double> &Equalizer::freqs()
{
    static const QVector<double> f{60, 120, 400, 1000, 2200, 4000, 8000, 12000};
    return f;
}

const QVector<double> &Equalizer::bandwidths()
{
    // Same Q as the Android app: each band's width follows the gap to its
    // neighbours (60→120 is an octave, 120→400 nearly two), so faders overlap evenly.
    static const QVector<double> bw = [] {
        const auto &f = freqs();
        QVector<double> out;
        for (int i = 0; i < f.size(); ++i) {
            double sum = 0;
            int n = 0;
            if (i > 0) { sum += std::log2(f[i] / f[i - 1]); ++n; }
            if (i + 1 < f.size()) { sum += std::log2(f[i + 1] / f[i]); ++n; }
            const double r = std::pow(2.0, sum / n);
            const double q = std::sqrt(r) / (r - 1.0);
            out << f[i] / q;
        }
        return out;
    }();
    return bw;
}

QString Equalizer::label(double hz)
{
    if (hz < 1000)
        return QString::number(int(hz));
    const double k = hz / 1000.0;
    return k == std::floor(k) ? QStringLiteral("%1k").arg(int(k)) : QStringLiteral("%1k").arg(k, 0, 'f', 1);
}

double Equalizer::responseDb(const QVector<double> &gainsDb, double hz)
{
    constexpr double fs = 48000.0;
    const auto &f = freqs();
    const auto &bw = bandwidths();
    const std::complex<double> z = std::polar(1.0, -2.0 * M_PI * hz / fs);  // z^-1
    double db = 0;
    for (int i = 0; i < f.size() && i < gainsDb.size(); ++i) {
        if (gainsDb[i] == 0.0)
            continue;
        const double a = std::pow(10.0, gainsDb[i] / 40.0);
        const double w0 = 2.0 * M_PI * f[i] / fs;
        const double alpha = std::sin(w0) / (2.0 * (f[i] / bw[i]));
        const double b0 = 1 + alpha * a, b1 = -2 * std::cos(w0), b2 = 1 - alpha * a;
        const double a0 = 1 + alpha / a, a1 = -2 * std::cos(w0), a2 = 1 - alpha / a;
        const auto h = (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z);
        db += 20.0 * std::log10(std::abs(h));
    }
    return db;
}

const QVector<Equalizer::Preset> &Equalizer::builtIns()
{
    // Same names and curves as the Android app.
    static const QVector<Preset> p{
        {QStringLiteral("flat"), {0, 0, 0, 0, 0, 0, 0, 0}, true},
        {QStringLiteral("rock"), {5, 3, -1, -2, 1, 3, 5, 5}, true},
        {QStringLiteral("pop"), {-1, 2, 4, 4, 2, 0, -1, -2}, true},
        {QStringLiteral("jazz"), {4, 2, 0, 2, -2, -1, 1, 3}, true},
        {QStringLiteral("classical"), {5, 4, 1, 0, 0, 0, 2, 4}, true},
        {QStringLiteral("dance"), {6, 5, 1, -2, -1, 2, 4, 5}, true},
        {QStringLiteral("bass"), {8, 6, 3, 0, -1, 0, 1, 2}, true},
        {QStringLiteral("vocal"), {-2, -1, 2, 4, 4, 3, 1, -1}, true},
        {QStringLiteral("treble"), {-2, -2, -1, 0, 2, 4, 6, 7}, true},
    };
    return p;
}

Equalizer::Equalizer(GstEngine *engine, QObject *parent) : QObject(parent), m_engine(engine)
{
    QSettings s;
    m_enabled = s.value(QStringLiteral("eq/enabled"), false).toBool();
    const QVariantList g = s.value(QStringLiteral("eq/gains")).toList();
    m_gains.fill(0.0, int(freqs().size()));
    for (int i = 0; i < g.size() && i < m_gains.size(); ++i)
        m_gains[i] = qBound(MinDb, g[i].toDouble(), MaxDb);
    m_preset = s.value(QStringLiteral("eq/preset"), QStringLiteral("flat")).toString();
    loadCustom();
    apply();
}

void Equalizer::apply()
{
    if (!m_engine)
        return;
    QVector<double> g = m_gains;
    if (!m_enabled)
        g.fill(0.0);
    m_engine->setEqualizer(freqs(), bandwidths(), g);
    bool boosting = false;
    for (double d : g)
        boosting = boosting || d > 0.0;
    m_engine->setLimiter(boosting);
}

void Equalizer::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    apply();
    save();
    Q_EMIT changed();
}

void Equalizer::setGain(int band, double db)
{
    if (band < 0 || band >= m_gains.size())
        return;
    QVector<double> g = m_gains;
    g[band] = db;
    setGains(g);
}

void Equalizer::setGains(const QVector<double> &db)
{
    QVector<double> g(int(freqs().size()), 0.0);
    for (int i = 0; i < g.size() && i < db.size(); ++i)
        g[i] = qBound(MinDb, db[i], MaxDb);
    m_gains = g;
    // Name the curve if it's exactly a preset.
    m_preset.clear();
    for (const Preset &p : presets())
        if (p.gains == m_gains)
            m_preset = p.name;
    apply();
    save();
    Q_EMIT changed();
}

QVector<Equalizer::Preset> Equalizer::presets() const
{
    return builtIns() + m_custom;
}

void Equalizer::applyPreset(const QString &name)
{
    for (const Preset &p : presets())
        if (p.name == name) {
            setGains(p.gains);
            m_preset = name;
            save();
            Q_EMIT changed();
            return;
        }
}

void Equalizer::savePreset(const QString &name)
{
    const QString n = name.trimmed().left(32);
    if (n.isEmpty())
        return;
    for (const Preset &p : builtIns())
        if (p.name.compare(n, Qt::CaseInsensitive) == 0)
            return;  // built-ins are read-only
    bool replaced = false;
    for (Preset &p : m_custom)
        if (p.name == n) {
            p.gains = m_gains;
            replaced = true;
        }
    if (!replaced)
        m_custom.push_back({n, m_gains, false});
    m_preset = n;
    saveCustom();
    save();
    Q_EMIT presetsChanged();
    Q_EMIT changed();
}

void Equalizer::deletePreset(const QString &name)
{
    const auto before = m_custom.size();
    m_custom.erase(std::remove_if(m_custom.begin(), m_custom.end(), [&](const Preset &p) { return p.name == name; }),
                   m_custom.end());
    if (m_custom.size() == before)
        return;
    if (m_preset == name)
        m_preset.clear();
    saveCustom();
    save();
    Q_EMIT presetsChanged();
    Q_EMIT changed();
}

void Equalizer::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("eq/enabled"), m_enabled);
    QVariantList g;
    for (double d : m_gains)
        g << d;
    s.setValue(QStringLiteral("eq/gains"), g);
    s.setValue(QStringLiteral("eq/preset"), m_preset);
}

void Equalizer::loadCustom()
{
    // JSON like the Android app's custom presets: [{"name": …, "gains": [8 numbers]}].
    const QByteArray json = QSettings().value(QStringLiteral("eq/custom")).toByteArray();
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Preset p;
        p.name = o.value(QStringLiteral("name")).toString();
        for (const QJsonValue &g : o.value(QStringLiteral("gains")).toArray())
            p.gains << qBound(MinDb, g.toDouble(), MaxDb);
        if (!p.name.isEmpty() && p.gains.size() == freqs().size())
            m_custom << p;
    }
}

void Equalizer::saveCustom() const
{
    QJsonArray arr;
    for (const Preset &p : m_custom) {
        QJsonArray g;
        for (double d : p.gains)
            g << d;
        arr << QJsonObject{{QStringLiteral("name"), p.name}, {QStringLiteral("gains"), g}};
    }
    QSettings().setValue(QStringLiteral("eq/custom"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
