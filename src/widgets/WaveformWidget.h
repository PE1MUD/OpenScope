#pragma once

#include "widgets/VideoWidget.h"

class QPaintEvent;
class QResizeEvent;
class QSlider;

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
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateScrollSliderGeometry();

    QSlider* scrollSlider_ = nullptr;
    int zoomFactor_ = 1;
    bool zoomEnabled_ = true;
    double scrollPosition_ = 0.0;
};
