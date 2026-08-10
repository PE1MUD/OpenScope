#include "MainWindow.h"
#include "ScopeWorkspace.h"
#include "TestPatternGenerator.h"
#include "VideoEngine.h"
#include "VideoWidget.h"
#include "WaveformWidget.h"
#include "DeckLinkProbe.h"
#include "VectorscopeWidget.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoEngine_(new VideoEngine(this))
    , videoWidget_(new VideoWidget)
{
    setWindowTitle("OpenScope V0.1");
    resize(900, 720);
    SetThreadPriority(
        GetCurrentThread(),
        THREAD_PRIORITY_HIGHEST);

    waveformWidget_ =
        new WaveformWidget;

    vectorscopeWidget_ =
        new VectorscopeWidget;

    workspace_ =
        new ScopeWorkspace(
            videoWidget_,
            waveformWidget_,
            vectorscopeWidget_,
            this);

    setCentralWidget(workspace_);

    auto* toolbar =
        addToolBar("Line selector");

    auto* waveformZoomButton =
        new QPushButton(
            "X1",
            toolbar);

    waveformZoomButton->setCheckable(true);

    toolbar->addWidget(
        waveformZoomButton);

    connect(
        waveformZoomButton,
        &QPushButton::toggled,
        this,
        [this, waveformZoomButton](bool zoomed)
        {
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
        videoEngine_,
        &VideoEngine::setWaveformScrollPosition);

    auto* lineSelector =
        new QSpinBox(toolbar);

    lineSelector->setRange(
        -1,
        575);

    lineSelector->setValue(320);

    lineSelector->setSpecialValueText(
        "All");

    toolbar->addWidget(
        lineSelector);

    connect(
        lineSelector,
        &QSpinBox::valueChanged,
        videoEngine_,
        &VideoEngine::setSelectedLine);

    auto* persistenceLabel =
        new QLabel(
            "Pers",
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

    persistenceSlider->setValue(0);

    persistenceSlider->setFixedWidth(
        140);

    toolbar->addWidget(
        persistenceSlider);

    connect(
        persistenceSlider,
        &QSlider::valueChanged,
        videoEngine_,
        &VideoEngine::setWaveformPersistence);

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

    const int vectorscopeSize =
        std::min(
            vectorscopeWidget_->width(),
            vectorscopeWidget_->height());

    videoEngine_->setVectorscopeOutputSize(
        vectorscopeSize,
        vectorscopeSize);

    connect(
        videoWidget_,
        &VideoWidget::outputSizeChanged,
        videoEngine_,
        &VideoEngine::setVideoOutputSize);

    connect(
        videoEngine_,
        &VideoEngine::frameChanged,
        videoWidget_,
        &VideoWidget::setImage);

    connect(
        videoEngine_,
        &VideoEngine::waveformChanged,
        waveformWidget_,
        [this](const QImage& image)
        {
            waveformWidget_->setImage(
                image);

            waveformWidget_->notifyFrameRendered();
        });

    connect(
        waveformWidget_,
        &WaveformWidget::outputSizeChanged,
        this,
        [this](int width, int height)
        {
            videoEngine_->setWaveformOutputSize(
                width,
                height);

            waveformWidget_->setDisplayBandwidthMHz(
                videoEngine_->traceBandwidthMHz());
        });

    videoEngine_->setVideoOutputSize(
        videoWidget_->width(),
        videoWidget_->height());
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
    MSG* msg =
        static_cast<MSG*>(
            message);

    if (msg->message == WM_SYSCOMMAND &&
        (msg->wParam & 0xFFF0) == SC_MAXIMIZE)
    {
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

            constexpr double aspectRatio =
                5.0 / 4.0;

            int windowWidth =
                availableWidth;

            int windowHeight =
                static_cast<int>(
                    std::lround(
                        windowWidth /
                        aspectRatio));

            if (windowHeight > availableHeight)
            {
                windowHeight =
                    availableHeight;

                windowWidth =
                    static_cast<int>(
                        std::lround(
                            windowHeight *
                            aspectRatio));
            }

            const int x =
                workArea.left +
                (availableWidth -
                    windowWidth) / 2;

            const int y =
                workArea.top +
                (availableHeight -
                    windowHeight) / 2;

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
        RECT* rect =
            reinterpret_cast<RECT*>(
                msg->lParam);
        HMONITOR monitor =
            MonitorFromWindow(
                reinterpret_cast<HWND>(winId()),
                MONITOR_DEFAULTTONEAREST);

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize =
            sizeof(MONITORINFO);

        GetMonitorInfo(
            monitor,
            &monitorInfo);

        const RECT& workArea =
            monitorInfo.rcWork;
        constexpr double aspectRatio =
            5.0 / 4.0;

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
                        aspectRatio));

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
                        aspectRatio));

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
                        aspectRatio));

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
                        aspectRatio));
        }

        if (currentHeight > maxHeight)
        {
            currentHeight =
                maxHeight;

            currentWidth =
                static_cast<int>(
                    std::lround(
                        currentHeight *
                        aspectRatio));
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
        }        *result = TRUE;
        return true;
    }

    return QMainWindow::nativeEvent(
        eventType,
        message,
        result);
}