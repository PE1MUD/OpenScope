#pragma once

#include <QWidget>

class QCheckBox;

class ControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ControlWidget(
        bool vintageLook,
        int chromaRenderIntensity,
        QWidget* parent = nullptr);
    void setPerformanceVisible(
        bool visible);

signals:
    void vintageLookChanged(
        bool enabled);

    void chromaRenderIntensityChanged(
        int intensity);
    void performanceVisibilityChanged(
        bool visible);

    void noiseReductionChanged(
        bool enabled);

    void noiseReductionIntensityChanged(
        int intensity);

private:
    QCheckBox* performanceCheckBox_ = nullptr;
};