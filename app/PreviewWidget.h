// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QImage>
#include <QWidget>

// The black box that shows the current frame, sized to fit with bars if needed.
class PreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void setAspect(double aspect);

    // The size (in real screen pixels) a frame should be rendered at to fill the box
    QSize renderSize() const;

    // Waits for one click on the picture, then sends where it landed as `picked`
    void pickSpot();
    // Same idea, but sends the colour that was clicked as `colorPicked`
    void pickColor();

signals:
    void resized();
    // x, y from the middle of the picture: -0.5 = left/top edge, 0.5 = right/bottom edge
    void picked(QPointF spot);
    void colorPicked(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect frameRect() const;

    QImage m_frame;
    double m_aspect = 16.0 / 9.0;
    bool m_picking = false;
    bool m_pickingColor = false; // (what the click is for)
};
