#pragma once

#include "widgets/VideoWidget.h"

#include <QPointF>
#include <QRect>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class WaveformWidget final : public VideoWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    bool isZoomed() const;
    int zoomFactor() const;

    void setZoomed(bool zoomed);
    void setZoomFactor(int factor);
    void setZoomEnabled(bool enabled);
    void setScrollPosition(double position);

signals:
    void zoomChanged(bool zoomed);
    void zoomFactorChanged(int factor);
    void scrollPositionChanged(double position);

protected:
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect imageRect() const;
    void updateHover(const QPointF& position);

    int zoomFactor_ = 1;
    bool zoomEnabled_ = true;
    double scrollPosition_ = 0.0;

    bool panActive_ = false;
    double panStartX_ = 0.0;
    double panStartScrollPosition_ = 0.0;

    bool hoverActive_ = false;
    QPointF hoverPosition_;

    bool measureActive_ = false;
    QPointF measureStartPosition_;
    QPointF measureCurrentPosition_;
};
