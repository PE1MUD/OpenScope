#pragma once

#include "settings/OpenScopeSettings.h"

#include <QWidget>

class QCheckBox;
class QButtonGroup;
class QToolButton;
class QSpinBox;
class QTabWidget;
class QSlider;
class QLabel;

class ControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ControlWidget(
        const OpenScopeSettings& settings,
        QWidget* parent = nullptr);

    void setPerformanceVisible(
        bool visible);

    void setLineNumber(
        int lineNumber);

    void setWaveformZoomFactor(
        int zoomFactor);

    void setAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

    void setHelpTabVisible(
        bool visible);

    void setCompositeInputGainState(
        bool lumaAvailable,
        bool chromaAvailable,
        int minimumHundredthsDb,
        int maximumHundredthsDb,
        int lumaHundredthsDb,
        int chromaHundredthsDb);

signals:
    void lineNumberChanged(int lineNumber);
    void waveformZoomChanged(int zoomFactor);
    void waveformPersistenceChanged(int persistence);
    void vectorscopeGlowChanged(int glow);

    void vintageLookChanged(bool enabled);
    void chromaRenderIntensityChanged(int intensity);

    void noiseReductionChanged(bool enabled);
    void noiseReductionIntensityChanged(int intensity);
    void lumaCompensationChanged(bool enabled);
    void lumaCompensationGainChanged(int gainHundredthsDb);

    void compositeLumaGainChanged(int gainHundredthsDb);
    void compositeChromaGainChanged(int gainHundredthsDb);
    void compositeGainCommitRequested();

    void performanceVisibilityChanged(bool visible);
    void floatiesHomeRequested();
    void spoutVideoEnabledChanged(bool enabled);
    void spoutWaveformEnabledChanged(bool enabled);
    void spoutVectorscopeEnabledChanged(bool enabled);
    void legacyAspectRatioChanged(bool legacyEnabled);
    void exportHighResolutionPngRequested();
    void exportHighResolutionPngQuickRequested();

private:
    QCheckBox* performanceCheckBox_ = nullptr;
    QCheckBox* legacyAspectRatioCheckBox_ = nullptr;
    QButtonGroup* waveformZoomButtonGroup_ = nullptr;
    QToolButton* waveformZoom1Button_ = nullptr;
    QToolButton* waveformZoom5Button_ = nullptr;
    QToolButton* waveformZoom10Button_ = nullptr;
    QSpinBox* lineSelector_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QSlider* compositeLumaGainSlider_ = nullptr;
    QSlider* compositeChromaGainSlider_ = nullptr;
    QLabel* compositeGainStatusLabel_ = nullptr;
    int helpTabIndex_ = -1;
};
