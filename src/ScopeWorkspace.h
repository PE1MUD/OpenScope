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

    void setCompositeInputGainState(
        bool lumaAvailable,
        bool chromaAvailable,
        int minimumHundredthsDb,
        int maximumHundredthsDb,
        int lumaHundredthsDb,
        int chromaHundredthsDb);

    bool isVideoMaximized() const;

    QPoint floatingSettingsPosition() const;
    QSize floatingSettingsSize() const;
    bool hasFloatingSettingsSize() const;
    bool hasFloatingSettingsPosition() const;
    void homeFloatingSettings(const QPoint& position);

signals:
    void lineNumberChanged(int lineNumber);
    void waveformZoomChanged(int zoomFactor);
    void waveformPersistenceChanged(int persistence);
    void waveformCoreIntensityChanged(int intensity);
    void waveformCoreWidthChanged(int widthTenths);
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
    void antiAliasingChanged(bool enabled);
    void colorizeIllegalLuminanceChanged(bool enabled);
    void colorizeGamutErrorsChanged(bool enabled);
    void lineSelectorVisibleChanged(bool enabled);
    void safetyArea90Changed(bool enabled);
    void textSafetyArea80Changed(bool enabled);
    void lumaCompensationChanged(bool enabled);
    void lumaCompensationGainChanged(int gainHundredthsDb);

    void compositeLumaGainChanged(int gainHundredthsDb);
    void compositeChromaGainChanged(int gainHundredthsDb);
    void compositeGainCommitRequested();

    void legacyAspectRatioChanged(bool legacyEnabled);

    void videoMaximizedChanged(bool maximized);
    void exportHighResolutionPngRequested();
    void exportHighResolutionPngQuickRequested();
    void waveformRawCaptureRequested();

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
    QSize settingsFloatingSize_;
    bool settingsFloatingSizeValid_ = false;

private slots:
    void toggleMaximized(ScopeViewport* viewport);
};
