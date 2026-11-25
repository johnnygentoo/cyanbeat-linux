// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/VisualizerView.h"

#include "engine/Visualizer.h"
#include "ui/Theme.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
QColor colorAt(const QVector<QColor> &stops, double t)
{
    if (stops.isEmpty())
        return Theme::accent();
    if (stops.size() == 1)
        return stops.first();
    t = qBound(0.0, t, 1.0) * (stops.size() - 1);
    const int i = qMin(int(t), int(stops.size()) - 2);
    const double f = t - i;
    const QColor &a = stops[i], &b = stops[i + 1];
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f), float(a.greenF() + (b.greenF() - a.greenF()) * f),
                            float(a.blueF() + (b.blueF() - a.blueF()) * f));
}
}

VisualizerView::VisualizerView(Visualizer *vis, QWidget *parent) : QWidget(parent), m_vis(vis)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    connect(vis, &Visualizer::barsChanged, this, [this] {
        if (isVisible())
            update();
    });
}

void VisualizerView::setCaption(const QString &title, const QString &subtitle)
{
    m_title = title;
    m_subtitle = subtitle;
    update();
}

void VisualizerView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, QColor(0x0c, 0x1a, 0x24));
    bg.setColorAt(1.0, Theme::DarkAnchor);
    p.fillRect(rect(), bg);

    const QVector<float> &bars = m_vis->bars();
    const int n = int(bars.size());
    if (n == 0)
        return;
    const double margin = 28.0;
    const double floorY = height() * 0.72;
    const double maxH = floorY - 70.0;
    const double slot = (width() - 2 * margin) / n;
    const double barW = qMax(2.0, slot * 0.72);
    // Vaporwave reskins this too: magenta rising into cyan.
    const QVector<QColor> stops = Theme::vapor() ? QVector<QColor>{Theme::Magenta, Theme::Cyan} : m_vis->colors();

    for (int i = 0; i < n; ++i) {
        const double h = qMax(2.0, double(bars[i]) * maxH);
        const double x = margin + i * slot + (slot - barW) / 2;
        // Colour climbs the cava gradient with height (a single foreground colour stays flat).
        QLinearGradient g(0, floorY, 0, floorY - maxH);
        g.setColorAt(0.0, colorAt(stops, 0.0));
        g.setColorAt(1.0, colorAt(stops, 1.0));
        QPainterPath bar;
        bar.addRoundedRect(QRectF(x, floorY - h, barW, h), qMin(3.0, barW / 2), qMin(3.0, barW / 2));
        p.fillPath(bar, g);
        // Reflection on the "floor".
        QColor r = colorAt(stops, 0.0);
        QLinearGradient rg(0, floorY, 0, floorY + h * 0.5);
        r.setAlpha(70);
        rg.setColorAt(0.0, r);
        r.setAlpha(0);
        rg.setColorAt(1.0, r);
        p.fillRect(QRectF(x, floorY + 2, barW, h * 0.5), rg);
    }

    QFont title = font();
    title.setPointSizeF(title.pointSizeF() * 1.6);
    title.setBold(true);
    p.setFont(title);
    p.setPen(Theme::accent());
    p.drawText(QRectF(margin, 16, width() - 2 * margin, 30), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(title).elidedText(m_title, Qt::ElideRight, int(width() - 2 * margin)));
    QColor dim = Theme::accent();
    dim.setAlpha(170);
    p.setFont(font());
    p.setPen(dim);
    p.drawText(QRectF(margin, 46, width() - 2 * margin, 20), Qt::AlignLeft | Qt::AlignVCenter, m_subtitle);
    p.drawText(QRectF(margin, height() - 24, width() - 2 * margin, 18), Qt::AlignRight | Qt::AlignVCenter,
               m_vis->backend() == Visualizer::Backend::Cava ? tr("cava") : tr("built-in analyzer"));
}

void VisualizerView::mouseDoubleClickEvent(QMouseEvent *)
{
    Q_EMIT closeRequested();
}

void VisualizerView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
        Q_EMIT closeRequested();
    else
        QWidget::keyPressEvent(e);
}
