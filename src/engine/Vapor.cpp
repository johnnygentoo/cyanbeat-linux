// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/Vapor.h"

#include "engine/GstEngine.h"

#include <QSettings>
#include <QTimer>

Vapor::Vapor(GstEngine *engine, QObject *parent) : QObject(parent), m_engine(engine), m_debounce(new QTimer(this))
{
    QSettings s;
    m_speed = s.value(QStringLiteral("fx/speed"), m_speed).toDouble();
    m_pitch = s.value(QStringLiteral("fx/pitch"), m_pitch).toDouble();
    m_reverb = s.value(QStringLiteral("fx/reverb"), m_reverb).toDouble();
    m_bass = s.value(QStringLiteral("fx/bass"), m_bass).toDouble();
    m_tape = s.value(QStringLiteral("fx/tape"), m_tape).toBool();
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &Vapor::apply);
}

void Vapor::schedule()
{
    save();
    Q_EMIT changed();
    if (m_enabled)
        m_debounce->start();
}

void Vapor::apply()
{
    GstEngine::Effects fx;
    if (m_enabled) {
        fx.speed = m_speed;
        fx.pitch = m_engine->hasPitch() ? m_pitch : 1.0;
        fx.reverb = m_reverb;
        fx.bassDb = m_bass;
    }
    m_engine->setEffects(fx);
}

void Vapor::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    m_debounce->stop();
    apply();
    Q_EMIT enabledChanged(on);
    Q_EMIT changed();
}

void Vapor::setSpeed(double v)
{
    m_speed = qBound(0.5, v, 2.0);
    if (m_tape)
        m_pitch = m_speed;
    schedule();
}

void Vapor::setPitch(double v)
{
    m_pitch = qBound(0.5, v, 2.0);
    if (m_tape)
        m_speed = m_pitch;
    schedule();
}

void Vapor::setReverb(double v)
{
    m_reverb = qBound(0.0, v, 1.0);
    schedule();
}

void Vapor::setBassDb(double v)
{
    m_bass = qBound(0.0, v, 12.0);
    schedule();
}

void Vapor::setTape(bool on)
{
    m_tape = on;
    if (on)
        m_pitch = m_speed;
    schedule();
}

void Vapor::classic()
{
    m_speed = m_pitch = 0.8;
    m_reverb = 0.85;
    m_bass = 6.0;
    m_tape = true;
    save();
    if (!m_enabled)
        setEnabled(true);
    else
        apply();
    Q_EMIT changed();
}

void Vapor::flat()
{
    m_speed = m_pitch = 1.0;
    m_reverb = 0.0;
    m_bass = 0.0;
    schedule();
}

void Vapor::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("fx/speed"), m_speed);
    s.setValue(QStringLiteral("fx/pitch"), m_pitch);
    s.setValue(QStringLiteral("fx/reverb"), m_reverb);
    s.setValue(QStringLiteral("fx/bass"), m_bass);
    s.setValue(QStringLiteral("fx/tape"), m_tape);
}
