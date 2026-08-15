#pragma once

#include <QMainWindow>
#include <QRect>
#include <QSize>
#include <QCloseEvent>
class QTimer;
class QToolBar;

class VectorscopeWidget;
class VideoEngine;
class VideoWidget;
class WaveformWidget;
class ScopeWorkspace;
class SettingsService;
class PerformanceWidget;

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
    QToolBar* instrumentToolBar_ = nullptr;

    QSize videoRenderSize_;
    QSize waveformRenderSize_;
    QSize vectorscopeRenderSize_;

    RenderView activeRenderView_ =
        RenderView::Matrix;

    void updateVideoFullscreenUi(
        bool fullscreen);

    void updateRenderResolutionTitle();
};