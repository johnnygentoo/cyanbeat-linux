// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class GstEngine;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class Vapor;

// Window › Effects: the vaporwave switch and its knobs (the Android FX screen).
class EffectsDialog : public QDialog {
    Q_OBJECT
public:
    EffectsDialog(Vapor *vapor, GstEngine *engine, QWidget *parent = nullptr);

private:
    void refresh();
    QSlider *addRow(class QGridLayout *grid, int row, const QString &name, int min, int max, QLabel **value);

    Vapor *m_vapor;
    QPushButton *m_switch;
    QSlider *m_speed, *m_pitch, *m_reverb, *m_bass;
    QLabel *m_speedV, *m_pitchV, *m_reverbV, *m_bassV;
    QCheckBox *m_tape;
    bool m_updating = false;
};
