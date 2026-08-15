#include "MainWindow.h"
#include "ScopeWorkspace.h"
#include "VideoEngine.h"
#include "widgets/VideoWidget.h"
#include "widgets/WaveformWidget.h"
#include "DeckLinkProbe.h"
#include "widgets/VectorscopeWidget.h"
#include "settings/SettingsService.h"
#include "widgets/PerformanceWidget.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <cmath>

namespace
{
    constexpr double kWindowAspectRatio =
        5.0 / 4.0;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoWidget_(new VideoWidget)
    , videoEngine_(new VideoEngine(this))
{
    setWindowTitle("OpenScope V0.31");
    resize(900, 720);
    SetThreadPriority(
        GetCurrentThread(),
        THREAD_PRIORITY_HIGHEST);

    waveformWidget_ =
        new WaveformWidget;

    vectorscopeWidget_ =
        new VectorscopeWidget;

    settingsService_ =
        new SettingsService(this);

    const auto& windowSettings =
        settingsService_->settings().local.window;

    resize(
        windowSettings.width,
        windowSettings.height);

    move(
        windowSettings.x,
        windowSettings.y);

    if (windowSettings.maximized)
    {
        showMaximized();
    }

    workspace_ =
        new ScopeWorkspace(
            videoWidget_,
            waveformWidget_,
            vectorscopeWidget_,
            settingsService_
            ->settings()
            .control
            .instrument
            .waveform
            .vintageLook,
            settingsService_
            ->settings()
            .control
            .instrument
            .waveform
            .chromaRenderIntensity,
            this);

    const OpenScopeSettings::WorkspaceView
        initialWorkspaceView =
        settingsService_->settings()
        .local
        .workspace
        .view;

    workspace_->setWorkspaceView(
        initialWorkspaceView);

    switch (initialWorkspaceView)
    {
    case OpenScopeSettings::WorkspaceView::Video:
        activeRenderView_ =
            RenderView::Video;
        break;

    case OpenScopeSettings::WorkspaceView::Waveform:
        activeRenderView_ =
            RenderView::Waveform;
        break;

    case OpenScopeSettings::WorkspaceView::Vectorscope:
        activeRenderView_ =
            RenderView::Vectorscope;
        break;

    case OpenScopeSettings::WorkspaceView::Matrix:
    case OpenScopeSettings::WorkspaceView::Headless:
    default:
        activeRenderView_ =
            RenderView::Matrix;
        break;
    }

    performanceWidget_ =
        new PerformanceWidget(this);

    connect(
        performanceWidget_,
        &PerformanceWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            workspace_->setPerformanceVisible(
                visible);
        });

    performanceWidget_->setWindowTitle(
        "OpenScope Performance");

    performanceWidget_->setWindowFlag(
        Qt::Tool);

    performanceWidget_->setFixedSize(
        performanceWidget_->sizeHint());

    performanceWidget_->setFixedSize(
        performanceWidget_->size());

    performanceWidget_->show();
    // Set the values.
    videoEngine_->setDisplayGamma(
        settingsService_
        ->settings()
        .local
        .display
        .gamma);

    videoEngine_->setWaveformColor(
        !settingsService_
        ->settings()
        .control
        .instrument
        .waveform
        .vintageLook);

    videoEngine_->setWaveformChromaFillIntensity(
        settingsService_
        ->settings()
        .control
        .instrument
        .waveform
        .chromaRenderIntensity);

    connect(
        workspace_,
        &ScopeWorkspace::waveformChromaFillIntensityChanged,
        this,
        [this](int intensity)
        {
            settingsService_->update(
                [intensity](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .waveform
                        .chromaRenderIntensity =
                        intensity;
                });

            videoEngine_->
                setWaveformChromaFillIntensity(
                    intensity);
        });

    connect(
        workspace_,
        &ScopeWorkspace::waveformColorChanged,
        this,
        [this](bool colorEnabled)
        {
            settingsService_->update(
                [colorEnabled](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .waveform
                        .vintageLook =
                        !colorEnabled;
                });

            videoEngine_->setWaveformColor(
                colorEnabled);
        });

    connect(
        workspace_,
        &ScopeWorkspace::workspaceViewChanged,
        this,
        [this](OpenScopeSettings::WorkspaceView view)
        {
            settingsService_->update(
                [view](OpenScopeSettings& settings)
                {
                    settings.local.workspace.view =
                        view;
                });

            switch (view)
            {
            case OpenScopeSettings::WorkspaceView::Video:
                activeRenderView_ =
                    RenderView::Video;
                break;

            case OpenScopeSettings::WorkspaceView::Waveform:
                activeRenderView_ =
                    RenderView::Waveform;
                break;

            case OpenScopeSettings::WorkspaceView::Vectorscope:
                activeRenderView_ =
                    RenderView::Vectorscope;
                break;

            case OpenScopeSettings::WorkspaceView::Matrix:
            case OpenScopeSettings::WorkspaceView::Headless:
            default:
                activeRenderView_ =
                    RenderView::Matrix;
                break;
            }

            updateRenderResolutionTitle();
        });

    connect(
        workspace_,
        &ScopeWorkspace::performanceVisibilityChanged,
        this,
        [this](bool visible)
        {
            performanceWidget_->setVisible(
                visible);
        });

    connect(
        workspace_,
        &ScopeWorkspace::noiseReductionChanged,
        videoEngine_,
        &VideoEngine::setNoiseReductionEnabled);

    connect(
        workspace_,
        &ScopeWorkspace::videoMaximizedChanged,
        this,
        [this](bool maximized)
        {
            updateVideoFullscreenUi(
                maximized);
        });

    setCentralWidget(workspace_);

    performanceTimer_ =
        new QTimer(this);

    performanceTimer_->setInterval(
        50);

    connect(
        performanceTimer_,
        &QTimer::timeout,
        this,
        [this]()
        {
            performanceWidget_->setPerformanceSnapshot(
                videoEngine_->performanceSnapshot());
        });

    performanceTimer_->start();

    instrumentToolBar_ =
        addToolBar("Line selector");

    QToolBar* const toolbar =
        instrumentToolBar_;

    const bool waveformZoomed =
        settingsService_->settings()
        .control
        .instrument
        .waveform
        .zoom == 10;

    auto* waveformZoomButton =
        new QPushButton(
            waveformZoomed
            ? "X10"
            : "X1",
            toolbar);

    waveformZoomButton->setCheckable(true);

    waveformZoomButton->setChecked(
        waveformZoomed);

    toolbar->addWidget(
        waveformZoomButton);

    const double waveformScrollPosition =
        settingsService_->settings()
        .control
        .instrument
        .waveform
        .scrollPosition;

    waveformWidget_->setScrollPosition(
        waveformScrollPosition);

    videoEngine_->setWaveformScrollPosition(
        waveformScrollPosition);

    waveformWidget_->setZoomed(
        waveformZoomed);

    videoEngine_->setWaveformZoomed(
        waveformZoomed);

    connect(
        waveformZoomButton,
        &QPushButton::toggled,
        this,
        [this, waveformZoomButton](bool zoomed)
        {
            settingsService_->update(
                [zoomed](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .waveform
                        .zoom =
                        zoomed
                        ? 10
                        : 1;
                });

            waveformWidget_->setZoomed(
                zoomed);

            videoEngine_->setWaveformZoomed(
                zoomed);

            waveformZoomButton->setText(
                zoomed
                ? "X10"
                : "X1");
        });

    connect(
        waveformWidget_,
        &WaveformWidget::scrollPositionChanged,
        this,
        [this](double position)
        {
            settingsService_->update(
                [position](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .waveform
                        .scrollPosition =
                        position;
                });

            videoEngine_->setWaveformScrollPosition(
                position);
        });

    auto* lineSelector =
        new QSpinBox(toolbar);

    lineSelector->setRange(
        -1,
        575);

    const int lineNumber =
        settingsService_->settings()
        .control
        .instrument
        .lineNumber;

    lineSelector->setValue(
        lineNumber);

    lineSelector->setSpecialValueText(
        "All");

    toolbar->addWidget(
        lineSelector);

    videoEngine_->setSelectedLine(
        lineNumber);

    const bool waveformZoomEnabled =
        lineNumber >= 0;

    waveformZoomButton->setEnabled(
        waveformZoomEnabled);

    waveformWidget_->setZoomEnabled(
        waveformZoomEnabled);

    if (!waveformZoomEnabled &&
        waveformZoomButton->isChecked())
    {
        waveformZoomButton->setChecked(
            false);
    }

    connect(
        lineSelector,
        &QSpinBox::valueChanged,
        this,
        [this, waveformZoomButton](int lineNumber)
        {
            settingsService_->update(
                [lineNumber](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .lineNumber =
                        lineNumber;
                });

            videoEngine_->setSelectedLine(
                lineNumber);

            const bool waveformZoomEnabled =
                lineNumber >= 0;

            waveformZoomButton->setEnabled(
                waveformZoomEnabled);

            waveformWidget_->setZoomEnabled(
                waveformZoomEnabled);

            if (!waveformZoomEnabled &&
                waveformZoomButton->isChecked())
            {
                waveformZoomButton->setChecked(
                    false);
            }
        });

    auto* persistenceLabel =
        new QLabel(
            "Scopephor",
            toolbar);

    toolbar->addWidget(
        persistenceLabel);

    auto* persistenceSlider =
        new QSlider(
            Qt::Horizontal,
            toolbar);

    persistenceSlider->setRange(
        0,
        255);

    const int persistenceFrames =
        settingsService_->settings()
        .control
        .instrument
        .waveform
        .persistenceFrames;

    persistenceSlider->setValue(
        persistenceFrames);

    persistenceSlider->setFixedWidth(
        140);

    toolbar->addWidget(
        persistenceSlider);

    videoEngine_->setWaveformPersistence(
        persistenceFrames);

    connect(
        persistenceSlider,
        &QSlider::valueChanged,
        this,
        [this](int persistenceFrames)
        {
            settingsService_->update(
                [persistenceFrames](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .waveform
                        .persistenceFrames =
                        persistenceFrames;
                });

            videoEngine_->setWaveformPersistence(
                persistenceFrames);
        });

    connect(
        videoEngine_,
        &VideoEngine::vectorscopeChanged,
        vectorscopeWidget_,
        &VectorscopeWidget::setImage);

    connect(
        vectorscopeWidget_,
        &VectorscopeWidget::renderSizeChanged,
        videoEngine_,
        &VideoEngine::setVectorscopeOutputSize);

    connect(
        vectorscopeWidget_,
        &VectorscopeWidget::renderSizeChanged,
        this,
        [this](int width, int height)
        {
            vectorscopeRenderSize_ =
                QSize(width, height);

            if (activeRenderView_ ==
                RenderView::Vectorscope)
            {
                updateRenderResolutionTitle();
            }
        });

    connect(
        videoWidget_,
        &VideoWidget::outputSizeChanged,
        videoEngine_,
        &VideoEngine::setVideoOutputSize);

    connect(
        videoWidget_,
        &VideoWidget::outputSizeChanged,
        this,
        [this](int width, int height)
        {
            videoRenderSize_ =
                QSize(width, height);

            if (activeRenderView_ ==
                RenderView::Video ||
                activeRenderView_ ==
                RenderView::Matrix)
            {
                updateRenderResolutionTitle();
            }
        });

    connect(
        videoEngine_,
        &VideoEngine::frameChanged,
        videoWidget_,
        &VideoWidget::setImage);

    connect(
        videoEngine_,
        &VideoEngine::waveformChanged,
        waveformWidget_,
        &WaveformWidget::setImage);

    connect(
        waveformWidget_,
        &WaveformWidget::outputSizeChanged,
        videoEngine_,
        &VideoEngine::setWaveformOutputSize);

    connect(
        waveformWidget_,
        &WaveformWidget::outputSizeChanged,
        this,
        [this](int width, int height)
        {
            waveformRenderSize_ =
                QSize(width, height);

            if (activeRenderView_ ==
                RenderView::Waveform)
            {
                updateRenderResolutionTitle();
            }
        });

    updateVideoFullscreenUi(
        workspace_->isVideoMaximized());

    updateRenderResolutionTitle();
}

void MainWindow::updateVideoFullscreenUi(
    bool fullscreen)
{
    if (instrumentToolBar_ != nullptr)
    {
        instrumentToolBar_->setVisible(
            !fullscreen);
    }

    videoEngine_->setVideoHighlightEnabled(
        !fullscreen);
}

void MainWindow::updateRenderResolutionTitle()
{
    QSize renderSize;

    switch (activeRenderView_)
    {
    case RenderView::Video:
        renderSize =
            videoRenderSize_;
        break;

    case RenderView::Waveform:
        renderSize =
            waveformRenderSize_;
        break;

    case RenderView::Vectorscope:
        renderSize =
            vectorscopeRenderSize_;
        break;

    case RenderView::Matrix:
    default:
        // In matrix mode the video viewport is our
        // render-resolution reference.
        renderSize =
            videoRenderSize_;
        break;
    }

    if (renderSize.isValid())
    {
        setWindowTitle(
            QString(
                "OpenScope V0.31 - %1x%2")
            .arg(renderSize.width())
            .arg(renderSize.height()));
    }
    else
    {
        setWindowTitle(
            "OpenScope V0.31");
    }
}

VideoWidget* MainWindow::videoWidget() const
{
    return videoWidget_;
}

MainWindow::~MainWindow()
{
    deckLinkStop();
}

VideoEngine* MainWindow::videoEngine() const
{
    return videoEngine_;
}

bool MainWindow::nativeEvent(
    const QByteArray& eventType,
    void* message,
    qintptr* result)
{
    const MSG* msg =
        static_cast<const MSG*>(
            message);

    if (msg->message == WM_SYSCOMMAND &&
        (msg->wParam & 0xFFF0) == SC_MAXIMIZE)
    {
        if (customMaximized_)
        {
            setGeometry(
                restoreWindowGeometry_);

            customMaximized_ = false;

            *result = 0;
            return true;
        }
        const HWND hwnd =
            reinterpret_cast<HWND>(
                winId());

        const HMONITOR monitor =
            MonitorFromWindow(
                hwnd,
                MONITOR_DEFAULTTONEAREST);

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize =
            sizeof(MONITORINFO);

        if (GetMonitorInfo(
            monitor,
            &monitorInfo))
        {
            const RECT& workArea =
                monitorInfo.rcWork;

            const int availableWidth =
                workArea.right -
                workArea.left;

            const int availableHeight =
                workArea.bottom -
                workArea.top;

            int windowWidth =
                availableWidth;

            int windowHeight =
                static_cast<int>(
                    std::lround(
                        windowWidth /
                        kWindowAspectRatio));

            if (windowHeight > availableHeight)
            {
                windowHeight =
                    availableHeight;

                windowWidth =
                    static_cast<int>(
                        std::lround(
                            windowHeight *
                            kWindowAspectRatio));
            }

            const int x =
                workArea.left +
                (availableWidth -
                    windowWidth) / 2;

            const int y =
                workArea.top +
                (availableHeight -
                    windowHeight) / 2;

            restoreWindowGeometry_ =
                geometry();

            customMaximized_ = true;

            SetWindowPos(
                hwnd,
                nullptr,
                x,
                y,
                windowWidth,
                windowHeight,
                SWP_NOZORDER |
                SWP_NOACTIVATE);
        }

        *result = 0;
        return true;
    }

    if (msg->message == WM_SIZING)
    {
        RECT* const rect =
            reinterpret_cast<RECT*>(
                msg->lParam);
        const HMONITOR monitor =
            MonitorFromRect(
                rect,
                MONITOR_DEFAULTTONEAREST);

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize =
            sizeof(MONITORINFO);

        if (!GetMonitorInfo(
            monitor,
            &monitorInfo))
        {
            return QMainWindow::nativeEvent(
                eventType,
                message,
                result);
        }

        const RECT& workArea =
            monitorInfo.rcWork;

        const int width =
            rect->right -
            rect->left;

        const int height =
            rect->bottom -
            rect->top;

        switch (msg->wParam)
        {
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
        {
            const int newHeight =
                static_cast<int>(
                    std::lround(
                        width /
                        kWindowAspectRatio));

            rect->bottom =
                rect->top +
                newHeight;

            break;
        }

        case WMSZ_TOP:
        case WMSZ_BOTTOM:
        {
            const int newWidth =
                static_cast<int>(
                    std::lround(
                        height *
                        kWindowAspectRatio));

            rect->right =
                rect->left +
                newWidth;

            break;
        }

        case WMSZ_TOPLEFT:
        case WMSZ_TOPRIGHT:
        case WMSZ_BOTTOMLEFT:
        case WMSZ_BOTTOMRIGHT:
        {
            const int newHeight =
                static_cast<int>(
                    std::lround(
                        width /
                        kWindowAspectRatio));

            if (msg->wParam == WMSZ_TOPLEFT ||
                msg->wParam == WMSZ_TOPRIGHT)
            {
                rect->top =
                    rect->bottom -
                    newHeight;
            }
            else
            {
                rect->bottom =
                    rect->top +
                    newHeight;
            }

            break;
        }

        default:
            break;
        }
        const int maxWidth =
            workArea.right -
            workArea.left;

        const int maxHeight =
            workArea.bottom -
            workArea.top;

        int currentWidth =
            rect->right -
            rect->left;

        int currentHeight =
            rect->bottom -
            rect->top;

        if (currentWidth > maxWidth)
        {
            currentWidth =
                maxWidth;

            currentHeight =
                static_cast<int>(
                    std::lround(
                        currentWidth /
                        kWindowAspectRatio));
        }

        if (currentHeight > maxHeight)
        {
            currentHeight =
                maxHeight;

            currentWidth =
                static_cast<int>(
                    std::lround(
                        currentHeight *
                        kWindowAspectRatio));
        }

        switch (msg->wParam)
        {
        case WMSZ_LEFT:
        case WMSZ_TOPLEFT:
        case WMSZ_BOTTOMLEFT:
            rect->left =
                rect->right -
                currentWidth;
            break;

        default:
            rect->right =
                rect->left +
                currentWidth;
            break;
        }

        switch (msg->wParam)
        {
        case WMSZ_TOP:
        case WMSZ_TOPLEFT:
        case WMSZ_TOPRIGHT:
            rect->top =
                rect->bottom -
                currentHeight;
            break;

        default:
            rect->bottom =
                rect->top +
                currentHeight;
            break;
        }
        *result = TRUE;
        return true;
    }

    return QMainWindow::nativeEvent(
        eventType,
        message,
        result);
}

void MainWindow::closeEvent(
    QCloseEvent* event)
{
    const QRect geometry =
        normalGeometry();

    settingsService_->update(
        [&geometry, this](OpenScopeSettings& settings)
        {
            settings.local.window.x =
                geometry.x();

            settings.local.window.y =
                geometry.y();

            settings.local.window.width =
                geometry.width();

            settings.local.window.height =
                geometry.height();

            settings.local.window.maximized =
                isMaximized();
        });

    QMainWindow::closeEvent(event);
}