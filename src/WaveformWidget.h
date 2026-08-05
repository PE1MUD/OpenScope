#pragma once

#include "VideoWidget.h"

class QPaintEvent;
class QResizeEvent;


class WaveformWidget final : public VideoWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);
    void setDisplayBandwidthMHz(double bandwidthMHz);


signals:
    void outputSizeChanged(
        int width,
        int height); 
private:
    double displayBandwidthMHz_ = 6.75;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
};