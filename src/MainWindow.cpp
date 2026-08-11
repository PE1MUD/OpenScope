#include "MainWindow.h"
#include "ScopeWorkspace.h"
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
        &WaveformWidget::setImage);

    connect(
        waveformWidget_,
        &WaveformWidget::outputSizeChanged,
        videoEngine_,
        &VideoEngine::setWaveformOutputSize);
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