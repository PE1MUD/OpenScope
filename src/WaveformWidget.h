#pragma once

#include "VideoWidget.h"
#include <QElapsedTimer>

class QPaintEvent;
class QResizeEvent;
class QSlider;

class WaveformWidget final : public VideoWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setDisplayBandwidthMHz(
        double bandwidthMHz);

    void notifyFrameRendered();

    bool isZoomed() const;
    void setZoomed(bool zoomed);

    void setScrollPosition(double position);

signals:
    void outputSizeChanged(
        int width,
        int height);

    void zoomChanged(
        bool zoomed);

    void scrollPositionChanged(
        double position);
private:
    double displayBandwidthMHz_ = 6.75;

    QElapsedTimer fpsTimer_;
    QSlider* scrollSlider_ = nullptr;
    int frameCounter_ = 0;
    double fps_ = 0.0;

    bool zoomed_ = false;
    double scrollPosition_ = 0.0;


protected:
    void paintEvent(
        QPaintEvent* event) override;

    void resizeEvent(
        QResizeEvent* event) override;
};