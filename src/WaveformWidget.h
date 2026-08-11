#pragma once

#include "VideoWidget.h"

class QPaintEvent;
class QResizeEvent;
class QSlider;

class WaveformWidget final : public VideoWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    bool isZoomed() const;
    void setZoomed(bool zoomed);
    void setScrollPosition(double position);

signals:
    void zoomChanged(bool zoomed);
    void scrollPositionChanged(double position);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QSlider* scrollSlider_ = nullptr;
    bool zoomed_ = false;
    double scrollPosition_ = 0.0;
};