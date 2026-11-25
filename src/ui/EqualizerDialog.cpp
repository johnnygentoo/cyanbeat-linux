// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/EqualizerDialog.h"

#include "engine/Equalizer.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {

// The EQ's frequency response, drawn like the Android app's curve.
class CurveView : public QWidget {
public:
    explicit CurveView(Equalizer *eq, QWidget *parent) : QWidget(parent), m_eq(eq)
    {
        setMinimumHeight(120);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath frame;
        frame.addRoundedRect(r, 6, 6);
        p.fillPath(frame, Theme::DarkAnchor);
        QColor rim = Theme::accent();
        rim.setAlpha(90);
        p.setPen(QPen(rim, 1));
        p.drawPath(frame);

        const QRectF plot = r.adjusted(34, 8, -10, -18);
        auto xOf = [&](double hz) { return plot.left() + plot.width() * std::log10(hz / 20.0) / 3.0; };
        auto yOf = [&](double db) { return plot.center().y() - plot.height() / 2 * db / Equalizer::MaxDb; };

        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.8);
        p.setFont(small);
        QColor grid = Theme::accent();
        grid.setAlpha(35);
        QColor text = Theme::accent();
        text.setAlpha(140);
        for (int db : {-12, -6, 0, 6, 12}) {
            p.setPen(QPen(db == 0 ? rim : grid, 1));
            p.drawLine(QPointF(plot.left(), yOf(db)), QPointF(plot.right(), yOf(db)));
            p.setPen(text);
            p.drawText(QRectF(r.left(), yOf(db) - 8, 30, 16), Qt::AlignRight | Qt::AlignVCenter,
                       db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db));
        }
        for (double hz : {100.0, 1000.0, 10000.0}) {
            p.setPen(QPen(grid, 1));
            p.drawLine(QPointF(xOf(hz), plot.top()), QPointF(xOf(hz), plot.bottom()));
            p.setPen(text);
            p.drawText(QRectF(xOf(hz) - 20, plot.bottom() + 2, 40, 14), Qt::AlignCenter, Equalizer::label(hz));
        }

        const QVector<double> g = m_eq->gains();
        QPainterPath curve, fill;
        for (int i = 0; i <= 240; ++i) {
            const double hz = 20.0 * std::pow(1000.0, i / 240.0);
            const QPointF pt(xOf(hz), yOf(qBound(-14.0, Equalizer::responseDb(g, hz), 14.0)));
            if (i == 0) {
                curve.moveTo(pt);
                fill.moveTo(pt.x(), yOf(0));
            }
            curve.lineTo(pt);
            fill.lineTo(pt);
        }
        fill.lineTo(plot.right(), yOf(0));
        fill.closeSubpath();
        // Cyan lows into violet highs, see-through so the grid shows under it.
        const bool on = m_eq->enabled();
        QLinearGradient across(plot.left(), 0, plot.right(), 0);
        QColor c0 = Theme::accent(), c1 = Theme::accent2();
        c0.setAlpha(on ? 95 : 30);
        c1.setAlpha(on ? 95 : 30);
        across.setColorAt(0.0, c0);
        across.setColorAt(1.0, c1);
        p.fillPath(fill, across);
        QLinearGradient stroke(plot.left(), 0, plot.right(), 0);
        QColor s0 = Theme::accent(), s1 = Theme::accent2();
        s0.setAlpha(on ? 255 : 110);
        s1.setAlpha(on ? 255 : 110);
        stroke.setColorAt(0.0, s0);
        stroke.setColorAt(1.0, s1);
        p.setPen(QPen(QBrush(stroke), 2));
        p.drawPath(curve);
    }

private:
    Equalizer *m_eq;
};

} // namespace

EqualizerDialog::EqualizerDialog(Equalizer *eq, QWidget *parent) : QDialog(parent), m_eq(eq)
{
    setWindowTitle(tr("Equalizer"));
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_on = new QCheckBox(tr("On"), this);
    top->addWidget(m_on);
    top->addSpacing(16);
    m_preset = new QComboBox(this);
    m_preset->setMinimumWidth(180);
    top->addWidget(m_preset);
    auto *saveAs = new QPushButton(tr("Save As…"), this);
    m_delete = new QPushButton(tr("Delete"), this);
    top->addWidget(saveAs);
    top->addWidget(m_delete);
    top->addStretch(1);
    v->addLayout(top);

    m_curve = new CurveView(eq, this);
    v->addWidget(m_curve, 1);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    const auto &f = Equalizer::freqs();
    for (int i = 0; i < f.size(); ++i) {
        auto *value = new QLabel(this);
        value->setAlignment(Qt::AlignCenter);
        auto *slider = new QSlider(Qt::Vertical, this);
        slider->setRange(int(Equalizer::MinDb * 10), int(Equalizer::MaxDb * 10));
        slider->setSingleStep(5);
        slider->setPageStep(10);
        slider->setMinimumHeight(170);
        slider->setToolTip(tr("%1 Hz — double-click for 0 dB").arg(Equalizer::label(f[i])));
        slider->installEventFilter(this);
        auto *label = new QLabel(Equalizer::label(f[i]), this);
        label->setAlignment(Qt::AlignCenter);
        grid->addWidget(value, 0, i, Qt::AlignHCenter);
        grid->addWidget(slider, 1, i, Qt::AlignHCenter);
        grid->addWidget(label, 2, i, Qt::AlignHCenter);
        m_sliders << slider;
        m_values << value;
        connect(slider, &QSlider::valueChanged, this, [this, i](int tenths) {
            if (!m_updating)
                m_eq->setGain(i, tenths / 10.0);
        });
    }
    v->addLayout(grid);

    auto *note = new QLabel(tr("Boosts are real boosts; a limiter keeps them from clipping."), this);
    note->setObjectName(QStringLiteral("Hint"));
    v->addWidget(note);

    connect(m_on, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating)
            m_eq->setEnabled(on);
    });
    connect(m_preset, &QComboBox::activated, this, [this](int index) {
        const QString name = m_preset->itemData(index).toString();
        if (!name.isEmpty()) {
            m_eq->setEnabled(true);  // picking a preset means you want to hear it
            m_eq->applyPreset(name);
        }
    });
    connect(saveAs, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Preset"), tr("Name:"), QLineEdit::Normal,
                                                   m_eq->presetName(), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        for (const auto &p : Equalizer::builtIns())
            if (p.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
                QMessageBox::information(this, tr("Save Preset"), tr("\"%1\" is a built-in preset; pick another name.")
                                                                      .arg(p.name));
                return;
            }
        m_eq->savePreset(name);
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const QString name = m_eq->presetName();
        if (QMessageBox::question(this, tr("Delete Preset"), tr("Delete the preset \"%1\"?").arg(name))
            == QMessageBox::Yes)
            m_eq->deletePreset(name);
    });
    connect(eq, &Equalizer::changed, this, &EqualizerDialog::refresh);
    connect(eq, &Equalizer::presetsChanged, this, &EqualizerDialog::refreshPresets);

    refreshPresets();
    refresh();
    resize(560, 470);
}

void EqualizerDialog::refreshPresets()
{
    m_updating = true;
    m_preset->clear();
    m_preset->addItem(tr("Manual"), QString());
    for (const auto &p : m_eq->presets())
        m_preset->addItem(p.builtIn ? p.name.left(1).toUpper() + p.name.mid(1) : p.name, p.name);
    m_updating = false;
    refresh();
}

void EqualizerDialog::refresh()
{
    m_updating = true;
    m_on->setChecked(m_eq->enabled());
    const QVector<double> g = m_eq->gains();
    for (int i = 0; i < m_sliders.size() && i < g.size(); ++i) {
        m_sliders[i]->setValue(qRound(g[i] * 10));
        m_values[i]->setText(g[i] > 0 ? QStringLiteral("+%1").arg(g[i], 0, 'f', 1)
                                      : QString::number(g[i], 'f', 1));
    }
    const QString name = m_eq->presetName();
    const int idx = m_preset->findData(name);
    m_preset->setCurrentIndex(idx >= 0 ? idx : 0);
    bool custom = !name.isEmpty();
    for (const auto &p : Equalizer::builtIns())
        custom = custom && p.name != name;
    m_delete->setEnabled(custom);
    m_updating = false;
    m_curve->update();
}

bool EqualizerDialog::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() == QEvent::MouseButtonDblClick) {
        const int i = m_sliders.indexOf(static_cast<QSlider *>(obj));
        if (i >= 0) {
            m_eq->setGain(i, 0.0);
            return true;
        }
    }
    return QDialog::eventFilter(obj, e);
}
