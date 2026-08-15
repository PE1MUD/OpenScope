#pragma once
#include "settings/OpenScopeSettings.h"
#include <QWidget>

class QGridLayout;
class ScopeViewport;
class ControlWidget;

class ScopeWorkspace final : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeWorkspace(
        QWidget* videoWidget,
        QWidget* waveformWidget,
        QWidget* vectorscopeWidget,
        bool vintageLook,
        int chromaRenderIntensity,
        QWidget* parent = nullptr);
    void setWorkspaceView(
        OpenScopeSettings::WorkspaceView view);
    void setPerformanceVisible(
        bool visible);

    bool isVideoMaximized() const;

signals:
    void waveformChromaFillIntensityChanged(
        int intensity);
    void waveformColorChanged(bool enabled);
signals:
    void workspaceViewChanged(
        OpenScopeSettings::WorkspaceView view);
    void performanceVisibilityChanged(
        bool visible);

    void noiseReductionChanged(
        bool enabled);

    void videoMaximizedChanged(
        bool maximized);

private:
    void showGrid();
    void showMaximized(
        ScopeViewport* viewport);

    QGridLayout* layout_ = nullptr;

    ScopeViewport* videoViewport_ = nullptr;
    ScopeViewport* waveformViewport_ = nullptr;
    ScopeViewport* vectorscopeViewport_ = nullptr;
    ScopeViewport* yuvViewport_ = nullptr;

    ScopeViewport* maximizedViewport_ = nullptr;
    ControlWidget* controlWidget_ = nullptr;

private slots:
    void toggleMaximized(
        ScopeViewport* viewport);
};