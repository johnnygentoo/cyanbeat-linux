// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class Visualizer;

// The big visualizer pane (View › Show Visualizer, Ctrl+T): spectrum bars in
// your cava colours with a soft reflection, and the song across the top.
class VisualizerView : public QWidget {
    Q_OBJECT
public:
    explicit VisualizerView(Visualizer *vis, QWidget *parent = nullptr);
    void setCaption(const QString &title, const QString &subtitle);

Q_SIGNALS:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    Visualizer *m_vis;
    QString m_title, m_subtitle;
};
