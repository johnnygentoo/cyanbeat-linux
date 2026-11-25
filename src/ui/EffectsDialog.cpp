// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/EffectsDialog.h"

#include "engine/GstEngine.h"
#include "engine/Vapor.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

EffectsDialog::EffectsDialog(Vapor *vapor, GstEngine *engine, QWidget *parent) : QDialog(parent), m_vapor(vapor)
{
    setWindowTitle(tr("Effects"));
    auto *v = new QVBoxLayout(this);

    m_switch = new QPushButton(tr("≈  VAPORWAVE"), this);
    m_switch->setObjectName(QStringLiteral("VaporSwitch"));
    m_switch->setCheckable(true);
    m_switch->setMinimumHeight(44);
    v->addWidget(m_switch);

    auto *presets = new QHBoxLayout;
    auto *classic = new QPushButton(tr("Classic"), this);
    classic->setToolTip(tr("Slowed tape (0.8×), big reverb, bass up"));
    auto *flat = new QPushButton(tr("Flat"), this);
    presets->addWidget(classic);
    presets->addWidget(flat);
    presets->addStretch(1);
    m_tape = new QCheckBox(tr("Pitch follows speed (tape)"), this);
    presets->addWidget(m_tape);
    v->addLayout(presets);

    auto *grid = new QGridLayout;
    m_speed = addRow(grid, 0, tr("Speed"), 50, 200, &m_speedV);
    m_pitch = addRow(grid, 1, tr("Pitch"), 50, 200, &m_pitchV);
    m_reverb = addRow(grid, 2, tr("Reverb"), 0, 100, &m_reverbV);
    m_bass = addRow(grid, 3, tr("Bass"), 0, 24, &m_bassV);  // half-dB steps
    v->addLayout(grid);

    QString note = tr("Speed keeps or shifts pitch independently.");
    if (!engine->hasPitch()) {
        m_pitch->setEnabled(false);
        m_tape->setEnabled(false);
        note = tr("Pitch needs GStreamer's SoundTouch plugin (gst-plugins-soundtouch). Speed works without it.");
    } else {
        note += tr(" While pitch is shifted, songs change with a short gap instead of gapless.");
    }
    auto *hint = new QLabel(note, this);
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    v->addWidget(hint);

    connect(m_switch, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updating)
            m_vapor->setEnabled(on);
    });
    connect(classic, &QPushButton::clicked, m_vapor, &Vapor::classic);
    connect(flat, &QPushButton::clicked, m_vapor, &Vapor::flat);
    connect(m_tape, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            m_vapor->setTape(on);
    });
    connect(m_speed, &QSlider::valueChanged, this, [this](int v) {
        if (!m_updating)
            m_vapor->setSpeed(v / 100.0);
    });
    connect(m_pitch, &QSlider::valueChanged, this, [this](int v) {
        if (!m_updating)
            m_vapor->setPitch(v / 100.0);
    });
    connect(m_reverb, &QSlider::valueChanged, this, [this](int v) {
        if (!m_updating)
            m_vapor->setReverb(v / 100.0);
    });
    connect(m_bass, &QSlider::valueChanged, this, [this](int v) {
        if (!m_updating)
            m_vapor->setBassDb(v / 2.0);
    });
    connect(m_vapor, &Vapor::changed, this, &EffectsDialog::refresh);
    refresh();
    resize(520, 340);
}

QSlider *EffectsDialog::addRow(QGridLayout *grid, int row, const QString &name, int min, int max, QLabel **value)
{
    auto *label = new QLabel(name, this);
    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    *value = new QLabel(this);
    (*value)->setMinimumWidth(70);
    (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(label, row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(*value, row, 2);
    return slider;
}

void EffectsDialog::refresh()
{
    m_updating = true;
    m_switch->setChecked(m_vapor->enabled());
    m_tape->setChecked(m_vapor->tape());
    m_speed->setValue(qRound(m_vapor->speed() * 100));
    m_pitch->setValue(qRound(m_vapor->pitch() * 100));
    m_reverb->setValue(qRound(m_vapor->reverb() * 100));
    m_bass->setValue(qRound(m_vapor->bassDb() * 2));
    m_speedV->setText(QStringLiteral("%1×").arg(m_vapor->speed(), 0, 'f', 2));
    m_pitchV->setText(QStringLiteral("%1×").arg(m_vapor->pitch(), 0, 'f', 2));
    m_reverbV->setText(QStringLiteral("%1 %").arg(qRound(m_vapor->reverb() * 100)));
    m_bassV->setText(QStringLiteral("+%1 dB").arg(m_vapor->bassDb(), 0, 'f', 1));
    m_updating = false;
}
