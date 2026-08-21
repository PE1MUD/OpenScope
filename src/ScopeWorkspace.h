#pragma once

#include "settings/OpenScopeSettings.h"

#include <QPoint>
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
        const OpenScopeSettings& settings,
        QWidget* parent = nullptr);

    void setWorkspaceView(
        OpenScopeSettings::WorkspaceView view);

    void setPerformanceVisible(
        bool visible);

    void setLineNumber(
        int lineNumber);

    void setWaveformZoomFactor(
        int zoomFactor);

    void setAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

    bool isVideoMaximized() const;

    QPoint floatingSettingsPosition() const;
    bool hasFloatingSettingsPosition() const;
    void homeFloatingSettings(const QPoint& position);

signals:
    void lineNumberChanged(int lineNumber);
    void waveformZoomChanged(int zoomFactor);
    void waveformPersistenceChanged(int persistence);
    void vectorscopeGlowChanged(int glow);

    void waveformChromaFillIntensityChanged(int intensity);
    void waveformColorChanged(bool enabled);

    void workspaceViewChanged(
        OpenScopeSettings::WorkspaceView view);

    void performanceVisibilityChanged(bool visible);
    void floatiesHomeRequested();
    void spoutVideoEnabledChanged(bool enabled);
    void spoutWaveformEnabledChanged(bool enabled);
    void spoutVectorscopeEnabledChanged(bool enabled);

    void noiseReductionChanged(bool enabled);
    void noiseReductionIntensityChanged(int intensity);

    void legacyAspectRatioChanged(bool legacyEnabled);

    void videoMaximizedChanged(bool maximized);
    void exportHighResolutionPngRequested();
    void exportHighResolutionPngQuickRequested();

private:
    void showGrid();
    void showMaximized(ScopeViewport* viewport);

    void floatSettings();
    void dockSettings();
    void resizeFloatingSettings();

    QGridLayout* layout_ = nullptr;

    ScopeViewport* videoViewport_ = nullptr;
    ScopeViewport* waveformViewport_ = nullptr;
    ScopeViewport* vectorscopeViewport_ = nullptr;
    ScopeViewport* settingsViewport_ = nullptr;

    ScopeViewport* maximizedViewport_ = nullptr;
    ControlWidget* controlWidget_ = nullptr;

    OpenScopeSettings::AspectRatio aspectRatio_ =
        OpenScopeSettings::AspectRatio::Ratio16x9;

    bool settingsFloating_ = false;

    QPoint settingsFloatingPosition_;
    bool settingsFloatingPositionValid_ = false;

private slots:
    void toggleMaximized(ScopeViewport* viewport);
};
