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

signals:
    void resized();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect frameRect() const;

    QImage m_frame;
    double m_aspect = 16.0 / 9.0;
};
