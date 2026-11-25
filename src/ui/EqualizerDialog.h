// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QVector>

class Equalizer;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

// Window › Equalizer: on/off, presets (the Android app's, plus your own),
// a live response curve and 8 faders. Double-click a fader to zero it.
class EqualizerDialog : public QDialog {
    Q_OBJECT
public:
    explicit EqualizerDialog(Equalizer *eq, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void refresh();
    void refreshPresets();

    Equalizer *m_eq;
    QCheckBox *m_on;
    QComboBox *m_preset;
    QPushButton *m_delete;
    QWidget *m_curve;
    QVector<QSlider *> m_sliders;
    QVector<QLabel *> m_values;
    bool m_updating = false;
};
