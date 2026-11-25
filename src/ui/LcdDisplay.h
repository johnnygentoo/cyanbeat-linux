// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

// The status display in the middle of the top bar: what's playing, elapsed /
// remaining time and a scrubbable progress bar.
class LcdDisplay : public QWidget {
    Q_OBJECT
public:
    explicit LcdDisplay(QWidget *parent = nullptr);

    void setTrack(const QString &title, const QString &artist, const QString &album);
    void clearTrack();
    void setPosition(qint64 positionMs, qint64 durationMs);
    void setStatusText(const QString &text);  // shown when idle, e.g. "Scanning library…"

    QSize sizeHint() const override { return {520, 60}; }
    QSize minimumSizeHint() const override { return {300, 60}; }

Q_SIGNALS:
    void seekRequested(qint64 ms);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QRectF barRect() const;
    QRectF remainingRect() const;
    qint64 msAt(double x) const;

    bool m_hasTrack = false;
    QString m_title, m_artist, m_album, m_status;
    qint64 m_pos = 0, m_dur = 0;
    bool m_showRemaining = true;
    bool m_dragging = false;
    qint64 m_dragMs = 0;
};
