#pragma once

#include "VideoWidget.h"
#include <QElapsedTimer>

class QPaintEvent;
class QResizeEvent;


class WaveformWidget final : public VideoWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);
    void setDisplayBandwidthMHz(double bandwidthMHz);
    void notifyFrameRendered();


signals:
    void outputSizeChanged(
        int width,
        int height); 
private:
    double displayBandwidthMHz_ = 6.75;
    QElapsedTimer fpsTimer_;
    int frameCounter_ = 0;
    double fps_ = 0.0;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
};