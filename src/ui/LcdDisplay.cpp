// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/LcdDisplay.h"

#include "library/Track.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr double kPad = 10.0;
constexpr double kTimeW = 52.0;
}

LcdDisplay::LcdDisplay(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void LcdDisplay::setTrack(const QString &title, const QString &artist, const QString &album)
{
    m_hasTrack = true;
    m_title = title;
    m_artist = artist;
    m_album = album;
    m_pos = m_dur = 0;
    update();
}

void LcdDisplay::clearTrack()
{
    m_hasTrack = false;
    m_pos = m_dur = 0;
    update();
}

void LcdDisplay::setPosition(qint64 positionMs, qint64 durationMs)
{
    if (m_pos == positionMs && m_dur == durationMs)
        return;
    m_pos = positionMs;
    m_dur = durationMs;
    update();
}

void LcdDisplay::setBars(const QVector<float> &bars)
{
    m_bars = bars;
    if (m_hasTrack)
        update();
}

void LcdDisplay::setStatusText(const QString &text)
{
    m_status = text;
    update();
}

QRectF LcdDisplay::barRect() const
{
    const double y = height() - 15.0;
    return {kPad + kTimeW, y, width() - 2 * (kPad + kTimeW), 6.0};
}

QRectF LcdDisplay::remainingRect() const
{
    return {width() - kPad - kTimeW, height() - 22.0, kTimeW, 18.0};
}

qint64 LcdDisplay::msAt(double x) const
{
    const QRectF bar = barRect();
    const double f = qBound(0.0, (x - bar.left()) / bar.width(), 1.0);
    return qint64(f * m_dur);
}

void LcdDisplay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Display body: a recessed dark panel with a soft cyan rim.
    const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient bg(body.topLeft(), body.bottomLeft());
    bg.setColorAt(0.0, QColor(0x0c, 0x1a, 0x24));
    bg.setColorAt(1.0, Theme::DarkAnchor);
    QPainterPath shape;
    shape.addRoundedRect(body, 8, 8);
    p.fillPath(shape, bg);
    QColor rim = Theme::Cyan;
    rim.setAlpha(110);
    p.setPen(QPen(rim, 1.0));
    p.drawPath(shape);

    QFont base = font();
    QColor dim = Theme::Cyan;
    dim.setAlpha(190);

    if (!m_hasTrack) {
        // Idle: the CyanBeat waveform mark + name, or a status line.
        const double h = 26.0;
        const double barW = 3.0, gap = 2.0;
        const double heights[] = {0.35, 0.7, 1.0, 0.55, 0.85, 0.45};
        QFont big = base;
        big.setPointSizeF(base.pointSizeF() * 1.5);
        big.setBold(true);
        const QString name = m_status.isEmpty() ? QStringLiteral("CyanBeat") : m_status;
        const QFont &f = m_status.isEmpty() ? big : base;
        const double textW = QFontMetricsF(f).horizontalAdvance(name);
        const double markW = 6 * barW + 5 * gap;
        const double total = markW + 10 + textW;
        double x = (width() - total) / 2.0;
        const double cy = height() / 2.0;
        for (double hf : heights) {
            const double bh = h * hf;
            p.fillRect(QRectF(x, cy - bh / 2, barW, bh), Theme::Cyan);
            x += barW + gap;
        }
        p.setFont(f);
        p.setPen(m_status.isEmpty() ? Theme::Cyan : dim);
        p.drawText(QRectF(x + 8, 0, textW + 4, height()), Qt::AlignVCenter | Qt::AlignLeft, name);
        return;
    }

    // Spectrum, very faint, behind everything else.
    if (!m_bars.isEmpty()) {
        p.save();
        p.setClipPath(shape);
        QColor bar = Theme::Cyan;
        bar.setAlpha(38);
        const double area = width() - 2 * kPad;
        const double slot = area / m_bars.size();
        const double maxH = height() - 10.0;
        for (int i = 0; i < m_bars.size(); ++i) {
            const double h = m_bars[i] * maxH;
            if (h < 0.5)
                continue;
            p.fillRect(QRectF(kPad + i * slot + 0.5, height() - 4 - h, qMax(1.0, slot - 1.5), h), bar);
        }
        p.restore();
    }

    // Two text lines.
    const double textW = width() - 2 * kPad;
    QFont titleFont = base;
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(Theme::Cyan);
    const QFontMetricsF tfm(titleFont);
    p.drawText(QRectF(kPad, 5, textW, tfm.height()), Qt::AlignHCenter | Qt::AlignTop,
               tfm.elidedText(m_title, Qt::ElideRight, textW));

    QString sub = m_artist;
    if (!m_album.isEmpty())
        sub += (sub.isEmpty() ? QString() : QStringLiteral(" — ")) + m_album;
    p.setFont(base);
    p.setPen(dim);
    const QFontMetricsF bfm(base);
    p.drawText(QRectF(kPad, 5 + tfm.height(), textW, bfm.height()), Qt::AlignHCenter | Qt::AlignTop,
               bfm.elidedText(sub, Qt::ElideRight, textW));

    // Time row.
    const qint64 shownPos = m_dragging ? m_dragMs : m_pos;
    const QRectF bar = barRect();
    QFont small = base;
    small.setPointSizeF(base.pointSizeF() * 0.9);
    p.setFont(small);
    p.setPen(dim);
    p.drawText(QRectF(kPad, height() - 22.0, kTimeW - 6, 18.0), Qt::AlignVCenter | Qt::AlignRight,
               formatDuration(qMax<qint64>(shownPos, 1)));  // 1 ms → "0:00", not "--:--"
    const QString right = m_dur <= 0 ? QStringLiteral("--:--")
                        : m_showRemaining ? QStringLiteral("-") + formatDuration(qMax<qint64>(m_dur - shownPos, 1000))
                                          : formatDuration(m_dur);
    p.drawText(remainingRect().adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, right);

    QPainterPath track;
    track.addRoundedRect(bar, 3, 3);
    p.fillPath(track, QColor(0x14, 0x24, 0x30));
    QColor edge = Theme::Cyan;
    edge.setAlpha(120);
    p.setPen(QPen(edge, 1.0));
    p.drawPath(track);
    if (m_dur > 0) {
        const double f = qBound(0.0, double(shownPos) / double(m_dur), 1.0);
        QRectF fill = bar;
        fill.setWidth(bar.width() * f);
        QPainterPath fp;
        fp.addRoundedRect(fill, 3, 3);
        p.fillPath(fp, Theme::Cyan);
        // Diamond playhead.
        const QPointF c(bar.left() + bar.width() * f, bar.center().y());
        QPainterPath diamond;
        diamond.moveTo(c.x(), c.y() - 6);
        diamond.lineTo(c.x() + 6, c.y());
        diamond.lineTo(c.x(), c.y() + 6);
        diamond.lineTo(c.x() - 6, c.y());
        diamond.closeSubpath();
        p.setPen(QPen(Theme::DarkAnchor, 1.0));
        p.setBrush(Theme::Cyan);
        p.drawPath(diamond);
    }
}

void LcdDisplay::mousePressEvent(QMouseEvent *e)
{
    if (!m_hasTrack || e->button() != Qt::LeftButton)
        return;
    const QPointF pt = e->position();
    if (remainingRect().contains(pt)) {
        m_showRemaining = !m_showRemaining;
        update();
        return;
    }
    if (m_dur > 0 && barRect().adjusted(-6, -8, 6, 8).contains(pt)) {
        m_dragging = true;
        m_dragMs = msAt(pt.x());
        update();
    }
}

void LcdDisplay::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    m_dragMs = msAt(e->position().x());
    update();
}

void LcdDisplay::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging || e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_dragMs = msAt(e->position().x());
    m_pos = m_dragMs;
    update();
    Q_EMIT seekRequested(m_dragMs);
}
