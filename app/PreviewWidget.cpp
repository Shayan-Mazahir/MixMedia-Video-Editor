#include "PreviewWidget.h"

#include <QPainter>

PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PreviewWidget::setFrame(const QImage& frame)
{
    m_frame = frame;
    update();
}

void PreviewWidget::setAspect(double aspect)
{
    if (aspect > 0 && aspect != m_aspect) {
        m_aspect = aspect;
        update();
        emit resized();
    }
}

QRect PreviewWidget::frameRect() const
{
    int w = width();
    int h = int(w / m_aspect);
    if (h > height()) {
        h = height();
        w = int(h * m_aspect);
    }
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

QSize PreviewWidget::renderSize() const
{
    return frameRect().size() * devicePixelRatioF();
}

void PreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x0b, 0x0b, 0x0c));

    QRect r = frameRect();
    if (m_frame.isNull()) {
        p.fillRect(r, Qt::black);
        p.setPen(QColor(0x55, 0x55, 0x55));
        p.drawText(r, Qt::AlignCenter, "Drop something on the timeline to see it here");
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, m_frame);
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    emit resized();
}
