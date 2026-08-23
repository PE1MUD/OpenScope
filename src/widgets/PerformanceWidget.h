#pragma once

#include <deque>

#include <QWidget>

#include "util/PerformanceStats.h"

class QCloseEvent;

class PerformanceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PerformanceWidget(
        QWidget* parent = nullptr);

    void setPerformanceSnapshot(
        const PerformanceSnapshot& snapshot);
    QSize sizeHint() const override;

signals:
    void visibilityChanged(
        bool visible);

protected:
    void paintEvent(
        QPaintEvent* event) override;
    void mouseMoveEvent(
        QMouseEvent* event) override;
    void mousePressEvent(
        QMouseEvent* event) override;
    void closeEvent(
        QCloseEvent* event) override;

private:
    struct TraceHistorySample
    {
        double traceMs = 0.0;
        double megaPixels = 0.0;
        bool parallel = false;
    };

    PerformanceSnapshot snapshot_;
    std::deque<TraceHistorySample> traceHistory_;
    int pinnedDetailBarIndex_ = 4; // PC waveform by default
};