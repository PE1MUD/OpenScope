#pragma once

#include "settings/OpenScopeSettings.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QRect>
#include <QSize>

#include <memory>

class QTimer;

class VectorscopeWidget;
class VideoEngine;
class VideoWidget;
class WaveformWidget;
class ScopeWorkspace;
class SettingsService;
class PerformanceWidget;
class QAction;
class PhilipsPatternRomSource;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    VideoWidget* videoWidget() const;
    VideoEngine* videoEngine() const;

protected:
    void closeEvent(QCloseEvent* event) override;

    bool nativeEvent(
        const QByteArray& eventType,
        void* message,
        qintptr* result) override;

private:
    enum class RenderView
    {
        Matrix,
        Video,
        Waveform,
        Vectorscope
    };

    VideoWidget* videoWidget_ = nullptr;
    VideoEngine* videoEngine_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    ScopeWorkspace* workspace_ = nullptr;
    VectorscopeWidget* vectorscopeWidget_ = nullptr;

    QRect restoreWindowGeometry_;
    bool customMaximized_ = false;

    SettingsService* settingsService_ = nullptr;
    PerformanceWidget* performanceWidget_ = nullptr;
    QTimer* performanceTimer_ = nullptr;

    std::unique_ptr<PhilipsPatternRomSource>
        philipsPatternRomSource_;

    QAction* blackmagicSourceAction_ = nullptr;
    QAction* philipsPatternRomSourceAction_ = nullptr;
    QAction* reloadPhilipsPatternRomAction_ = nullptr;

    QSize videoRenderSize_;
    QSize waveformRenderSize_;
    QSize vectorscopeRenderSize_;

    RenderView activeRenderView_ =
        RenderView::Matrix;

    int preVideoClickLineNumber_ = -1;
    double preVideoClickScrollPosition_ = 0.0;
    bool preVideoClickStateValid_ = false;

    double windowAspectRatio() const;

    void applyDisplayAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio,
        bool resizeWindow);

    void updateVideoFullscreenUi(
        bool fullscreen);

    void updateRenderResolutionTitle();

    void createSourceMenu();
    void selectBlackmagicSource();
    void selectPhilipsPatternRomSource();
    void reloadPhilipsPatternRomSource();
};
