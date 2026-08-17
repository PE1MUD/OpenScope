#include "MainWindow.h"
#include "Version.h"
#include "ScopeWorkspace.h"
#include "VideoEngine.h"
#include "widgets/VideoWidget.h"
#include "widgets/WaveformWidget.h"
#include "DeckLinkProbe.h"
#include "widgets/VectorscopeWidget.h"
#include "settings/SettingsService.h"
#include "widgets/PerformanceWidget.h"
#include "standards/VideoStandard.h"
#include "output/SpoutOutput.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>
#include <QImage>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

namespace
{
class WaveformVideoPreview final : public QWidget
{
public:
    explicit WaveformVideoPreview(QWidget* parent = nullptr)
        : QWidget(parent, Qt::Tool)
    {
        setWindowTitle("Waveform Video Out");

        constexpr VideoStandard videoStandard =
            VideoStandard::pal625();

        setFixedSize(
            videoStandard.outputWidth,
            videoStandard.outputHeight);
    }

    void setImage(const QImage& image)
    {
        image_ = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);

        if (image_.isNull())
        {
            return;
        }

        // 1:1 inspection of the actual video-out raster.
        painter.drawImage(
            QPoint(0, 0),
            image_);
    }

private:
    QImage image_;
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoWidget_(new VideoWidget)
    , videoEngine_(new VideoEngine(this))
{
    setWindowTitle("OpenScope V" OPENSCOPE_VERSION);

    SetThreadPriority(
        GetCurrentThread(),
        THREAD_PRIORITY_HIGHEST);

    waveformWidget_ =
        new WaveformWidget;

    vectorscopeWidget_ =
        new VectorscopeWidget;

    settingsService_ =
        new SettingsService(this);

    const OpenScopeSettings& initialSettings =
        settingsService_->settings();

    const auto displayAspectRatio =
        initialSettings.local
            .display
            .aspectRatio;

    const auto& windowSettings =
        initialSettings.local.window;

    const double initialAspectRatio =
        OpenScopeSettings::aspectRatioValue(
            displayAspectRatio);

    const int initialWindowWidth =
        (std::max)(windowSettings.width, 640);

    const int initialWindowHeight =
        static_cast<int>(
            std::lround(
                static_cast<double>(initialWindowWidth) /
                initialAspectRatio));

    resize(
        initialWindowWidth,
        initialWindowHeight);

    move(
        windowSettings.x,
        windowSettings.y);

    workspace_ =
        new ScopeWorkspace(
            videoWidget_,
            waveformWidget_,
            vectorscopeWidget_,
            initialSettings,
            this);

    setCentralWidget(workspace_);

    connect(
        workspace_,
        &ScopeWorkspace::exportHighResolutionPngRequested,
        this,
        [this]()
        {
            const QString settingsFileName =
                QDir(
                    QCoreApplication::
                        applicationDirPath())
                    .filePath(
                        QStringLiteral(
                            "OpenScope.ini"));

            QSettings settings(
                settingsFileName,
                QSettings::IniFormat);

            QString exportDirectory =
                settings.value(
                    QStringLiteral(
                        "Local/Export/LastDirectory"))
                    .toString();

            if (exportDirectory.isEmpty() ||
                !QDir(exportDirectory).exists())
            {
                exportDirectory =
                    QStandardPaths::writableLocation(
                        QStandardPaths::PicturesLocation);
            }

            if (exportDirectory.isEmpty() ||
                !QDir(exportDirectory).exists())
            {
                exportDirectory =
                    QCoreApplication::
                        applicationDirPath();
            }

            QDir directory(
                exportDirectory);

            int sequenceNumber = 1;
            QString suggestedFileName;

            for (;;)
            {
                suggestedFileName =
                    QStringLiteral(
                        "capture_%1.png")
                        .arg(
                            sequenceNumber,
                            4,
                            10,
                            QLatin1Char('0'));

                if (!QFileInfo::exists(
                        directory.filePath(
                            suggestedFileName)))
                {
                    break;
                }

                ++sequenceNumber;
            }

            QString fileName =
                QFileDialog::getSaveFileName(
                    this,
                    tr("Export high-resolution PNG"),
                    directory.filePath(
                        suggestedFileName),
                    tr("PNG image (*.png)"));

            if (fileName.isEmpty())
            {
                return;
            }

            if (!fileName.endsWith(
                    QStringLiteral(".png"),
                    Qt::CaseInsensitive))
            {
                fileName +=
                    QStringLiteral(".png");
            }

            settings.setValue(
                QStringLiteral(
                    "Local/Export/LastDirectory"),
                QFileInfo(
                    fileName)
                    .absolutePath());

            const QImage image =
                videoEngine_->
                    captureHighResolutionSnapshot();

            if (image.isNull() ||
                !image.save(
                    fileName,
                    "PNG"))
            {
                QMessageBox::warning(
                    this,
                    tr("Export failed"),
                    tr("Could not capture or save the high-resolution PNG."));
            }
        });

    connect(
        workspace_,
        &ScopeWorkspace::exportHighResolutionPngQuickRequested,
        this,
        [this]()
        {
            const QString settingsFileName =
                QDir(
                    QCoreApplication::
                        applicationDirPath())
                    .filePath(
                        QStringLiteral(
                            "OpenScope.ini"));

            QSettings settings(
                settingsFileName,
                QSettings::IniFormat);

            QString exportDirectory =
                settings.value(
                    QStringLiteral(
                        "Local/Export/LastDirectory"))
                    .toString();

            if (exportDirectory.isEmpty() ||
                !QDir(exportDirectory).exists())
            {
                exportDirectory =
                    QStandardPaths::writableLocation(
                        QStandardPaths::PicturesLocation);
            }

            if (exportDirectory.isEmpty() ||
                !QDir(exportDirectory).exists())
            {
                exportDirectory =
                    QCoreApplication::
                        applicationDirPath();
            }

            QDir directory(
                exportDirectory);

            int sequenceNumber = 1;
            QString fileName;

            for (;;)
            {
                const QString suggestedFileName =
                    QStringLiteral(
                        "capture_%1.png")
                        .arg(
                            sequenceNumber,
                            4,
                            10,
                            QLatin1Char('0'));

                const QString candidatePath =
                    directory.filePath(
                        suggestedFileName);

                if (!QFileInfo::exists(
                        candidatePath))
                {
                    fileName = candidatePath;
                    break;
                }

                ++sequenceNumber;
            }

            settings.setValue(
                QStringLiteral(
                    "Local/Export/LastDirectory"),
                directory.absolutePath());

            const QImage image =
                videoEngine_->
                    captureHighResolutionSnapshot();

            if (image.isNull() ||
                !image.save(
                    fileName,
                    "PNG"))
            {
                QMessageBox::warning(
                    this,
                    tr("Export failed"),
                    tr("Could not capture or save the high-resolution PNG."));
            }
        });

    // Render-output wiring first. Aspect-ratio changes can now safely
    // emit fresh output sizes into the engine.
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
        videoWidget_,
        &VideoWidget::leftInteractionStarted,
        this,
        [this]()
        {
            if (activeRenderView_ !=
                RenderView::Matrix)
            {
                preVideoClickStateValid_ =
                    false;
                return;
            }

            const OpenScopeSettings& settings =
                settingsService_->settings();

            preVideoClickLineNumber_ =
                settings.control
                    .instrument
                    .lineNumber;

            preVideoClickScrollPosition_ =
                settings.control
                    .instrument
                    .waveform
                    .scrollPosition;

            preVideoClickStateValid_ =
                true;
        });

    connect(
        videoWidget_,
        &VideoWidget::doubleClickRestoreRequested,
        this,
        [this]()
        {
            if (!preVideoClickStateValid_ ||
                activeRenderView_ !=
                    RenderView::Matrix)
            {
                return;
            }

            workspace_->setLineNumber(
                preVideoClickLineNumber_);

            waveformWidget_->setScrollPosition(
                preVideoClickScrollPosition_);

            preVideoClickStateValid_ =
                false;
        });

    connect(
        videoWidget_,
        &VideoWidget::imageClicked,
        this,
        [this](double normalizedX,
               double normalizedY)
        {
            // Point-and-measure is intentionally a matrix-only action.
            // A single click in a maximized video viewer remains inert.
            if (activeRenderView_ !=
                RenderView::Matrix)
            {
                return;
            }

            constexpr int kPalLineCount = 576;

            const int selectedLine =
                std::clamp(
                    static_cast<int>(
                        std::lround(
                            normalizedY *
                            static_cast<double>(
                                kPalLineCount - 1))),
                    0,
                    kPalLineCount - 1);

            // Updating the ControlWidget deliberately goes through its
            // existing valueChanged path. That keeps the spin box,
            // settings and VideoEngine selected-line state in sync.
            workspace_->setLineNumber(
                selectedLine);

            const int zoomFactor =
                waveformWidget_->zoomFactor();

            if (zoomFactor <= 1)
            {
                return;
            }

            // Centre the clicked source-X position in the visible
            // waveform window. The waveform renderer's scroll value is
            // 0..1 over the legal start-position range, not over the
            // complete source line.
            const double visibleFraction =
                1.0 /
                static_cast<double>(zoomFactor);

            const double maximumStart =
                1.0 -
                visibleFraction;

            const double requestedStart =
                normalizedX -
                (visibleFraction * 0.5);

            const double scrollPosition =
                maximumStart > 0.0
                ? std::clamp(
                    requestedStart /
                        maximumStart,
                    0.0,
                    1.0)
                : 0.0;

            waveformWidget_->setScrollPosition(
                scrollPosition);
        });

    connect(
        videoWidget_,
        &VideoWidget::rightClicked,
        this,
        [this]()
        {
            if (activeRenderView_ !=
                RenderView::Matrix)
            {
                return;
            }

            const int currentZoom =
                waveformWidget_->zoomFactor();

            const int nextZoom =
                currentZoom <= 1
                ? 5
                : (currentZoom <= 5
                    ? 10
                    : 1);

            workspace_->setWaveformZoomFactor(
                nextZoom);
        });

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
        videoEngine_,
        &VideoEngine::waveformMeasurementDataChanged,
        waveformWidget_,
        &WaveformWidget::setMeasurementLuma);

    auto* waveformVideoPreview =
        new WaveformVideoPreview(this);

    connect(
        videoEngine_,
        &VideoEngine::waveformVideoChanged,
        waveformVideoPreview,
        [waveformVideoPreview](const QImage& image)
        {
            waveformVideoPreview->setImage(image);
        });

    // Spout output is deliberately independent of the preview widget.
    // The first received waveform frame lazily opens the sender.
    auto* waveformSpoutOutput =
        new SpoutOutput(
            QStringLiteral("OpenScope Waveform"),
            this);

    connect(
        videoEngine_,
        &VideoEngine::waveformVideoChanged,
        waveformSpoutOutput,
        &SpoutOutput::submitImage,
        Qt::QueuedConnection);

    waveformVideoPreview->setVisible(
        initialSettings.local
            .floaties
            .waveformVideoVisible);

    connect(
        videoEngine_,
        &VideoEngine::vectorscopeChanged,
        vectorscopeWidget_,
        &VectorscopeWidget::setImage);

    // Initial processing/instrument state.
    videoEngine_->setDisplayGamma(
        initialSettings.local
            .display
            .gamma);

    videoEngine_->setNoiseReductionEnabled(
        initialSettings.control
            .processing
            .noiseFilter
            .enabled);

    videoEngine_->setNoiseReductionIntensity(
        initialSettings.control
            .processing
            .noiseFilter
            .strength);

    videoEngine_->setWaveformColor(
        !initialSettings.control
            .instrument
            .waveform
            .vintageLook);

    videoEngine_->setWaveformChromaFillIntensity(
        initialSettings.control
            .instrument
            .waveform
            .chromaRenderIntensity);

    videoEngine_->setSelectedLine(
        initialSettings.control
            .instrument
            .lineNumber);

    videoEngine_->setWaveformPersistence(
        initialSettings.control
            .instrument
            .waveform
            .persistenceFrames);

    videoEngine_->setWaveformVideoContentScale(
        initialSettings.control
            .videoOut
            .underscan);

    videoEngine_->setWaveformVideoAspectRatio(
        initialSettings.control
            .videoOut
            .aspectRatio);

    videoEngine_->setVectorscopeGlow(
        initialSettings.control
            .instrument
            .vectorscope
            .glow);

    videoEngine_->setWaveformScrollPosition(
        initialSettings.control
            .instrument
            .waveform
            .scrollPosition);

    waveformWidget_->setScrollPosition(
        initialSettings.control
            .instrument
            .waveform
            .scrollPosition);

    waveformWidget_->setZoomEnabled(
        initialSettings.control
            .instrument
            .lineNumber >= 0);

    waveformWidget_->setZoomFactor(
        initialSettings.control
            .instrument
            .waveform
            .zoom);

    videoEngine_->setWaveformZoomFactor(
        initialSettings.control
            .instrument
            .waveform
            .zoom);

    applyDisplayAspectRatio(
        displayAspectRatio,
        false);

    // Control-panel wiring.
    connect(
        workspace_,
        &ScopeWorkspace::lineNumberChanged,
        this,
        [this](int lineNumber)
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

            waveformWidget_->setZoomEnabled(
                lineNumber >= 0);
        });

    connect(
        workspace_,
        &ScopeWorkspace::waveformZoomChanged,
        this,
        [this](int zoomFactor)
        {
            settingsService_->update(
                [zoomFactor](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .waveform
                        .zoom =
                        zoomFactor;
                });

            waveformWidget_->setZoomFactor(
                zoomFactor);

            videoEngine_->setWaveformZoomFactor(
                zoomFactor);
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

    connect(
        workspace_,
        &ScopeWorkspace::waveformPersistenceChanged,
        this,
        [this](int persistence)
        {
            settingsService_->update(
                [persistence](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .waveform
                        .persistenceFrames =
                        persistence;
                });

            videoEngine_->setWaveformPersistence(
                persistence);
        });

    connect(
        workspace_,
        &ScopeWorkspace::vectorscopeGlowChanged,
        this,
        [this](int glow)
        {
            settingsService_->update(
                [glow](OpenScopeSettings& settings)
                {
                    settings.control
                        .instrument
                        .vectorscope
                        .glow =
                        glow;
                });

            videoEngine_->setVectorscopeGlow(
                glow);
        });

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

            videoEngine_->setWaveformChromaFillIntensity(
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
        &ScopeWorkspace::noiseReductionChanged,
        this,
        [this](bool enabled)
        {
            settingsService_->update(
                [enabled](OpenScopeSettings& settings)
                {
                    settings.control
                        .processing
                        .noiseFilter
                        .enabled =
                        enabled;
                });

            videoEngine_->setNoiseReductionEnabled(
                enabled);
        });

    connect(
        workspace_,
        &ScopeWorkspace::noiseReductionIntensityChanged,
        this,
        [this](int strength)
        {
            settingsService_->update(
                [strength](OpenScopeSettings& settings)
                {
                    settings.control
                        .processing
                        .noiseFilter
                        .strength =
                        strength;
                });

            videoEngine_->setNoiseReductionIntensity(
                strength);
        });

    connect(
        workspace_,
        &ScopeWorkspace::legacyAspectRatioChanged,
        this,
        [this](bool legacyEnabled)
        {
            const auto aspectRatio =
                legacyEnabled
                ? OpenScopeSettings::AspectRatio::Ratio4x3
                : OpenScopeSettings::AspectRatio::Ratio16x9;

            settingsService_->update(
                [aspectRatio](OpenScopeSettings& settings)
                {
                    settings.local
                        .display
                        .aspectRatio =
                        aspectRatio;
                });

            applyDisplayAspectRatio(
                aspectRatio,
                true);
        });

    performanceWidget_ =
        new PerformanceWidget(this);

    performanceWidget_->setWindowTitle(
        "OpenScope Performance");

    performanceWidget_->setWindowFlag(
        Qt::Tool);

    performanceWidget_->setFixedSize(
        performanceWidget_->sizeHint());

    performanceWidget_->setFixedSize(
        performanceWidget_->size());

    if (initialSettings.local
        .floaties
        .performance
        .positionValid)
    {
        performanceWidget_->move(
            initialSettings.local
                .floaties
                .performance
                .x,
            initialSettings.local
                .floaties
                .performance
                .y);
    }

    connect(
        performanceWidget_,
        &PerformanceWidget::visibilityChanged,
        this,
        [this](bool visible)
        {
            workspace_->setPerformanceVisible(
                visible);

            settingsService_->update(
                [visible](OpenScopeSettings& settings)
                {
                    settings.local
                        .floaties
                        .performanceVisible =
                        visible;
                });
        });

    connect(
        workspace_,
        &ScopeWorkspace::performanceVisibilityChanged,
        this,
        [this](bool visible)
        {
            settingsService_->update(
                [visible](OpenScopeSettings& settings)
                {
                    settings.local
                        .floaties
                        .performanceVisible =
                        visible;
                });

            performanceWidget_->setVisible(
                visible);
        });

    performanceWidget_->setVisible(
        initialSettings.local
            .floaties
            .performanceVisible);

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
                activeRenderView_ = RenderView::Video;
                break;

            case OpenScopeSettings::WorkspaceView::Waveform:
                activeRenderView_ = RenderView::Waveform;
                break;

            case OpenScopeSettings::WorkspaceView::Vectorscope:
                activeRenderView_ = RenderView::Vectorscope;
                break;

            case OpenScopeSettings::WorkspaceView::Matrix:
            case OpenScopeSettings::WorkspaceView::Headless:
            default:
                activeRenderView_ = RenderView::Matrix;
                break;
            }

            updateRenderResolutionTitle();
        });

    connect(
        workspace_,
        &ScopeWorkspace::videoMaximizedChanged,
        this,
        [this](bool maximized)
        {
            updateVideoFullscreenUi(
                maximized);
        });

    const OpenScopeSettings::WorkspaceView initialWorkspaceView =
        initialSettings.local
            .workspace
            .view;

    switch (initialWorkspaceView)
    {
    case OpenScopeSettings::WorkspaceView::Video:
        activeRenderView_ = RenderView::Video;
        break;
    case OpenScopeSettings::WorkspaceView::Waveform:
        activeRenderView_ = RenderView::Waveform;
        break;
    case OpenScopeSettings::WorkspaceView::Vectorscope:
        activeRenderView_ = RenderView::Vectorscope;
        break;
    case OpenScopeSettings::WorkspaceView::Matrix:
    case OpenScopeSettings::WorkspaceView::Headless:
    default:
        activeRenderView_ = RenderView::Matrix;
        break;
    }

    workspace_->setWorkspaceView(
        initialWorkspaceView);

    if (windowSettings.maximized)
    {
        showMaximized();
    }

    performanceTimer_ =
        new QTimer(this);

    performanceTimer_->setInterval(50);

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

    updateVideoFullscreenUi(
        workspace_->isVideoMaximized());

    updateRenderResolutionTitle();
}

double MainWindow::windowAspectRatio() const
{
    if (settingsService_ != nullptr &&
        settingsService_->settings()
            .local
            .display
            .aspectRatio ==
        OpenScopeSettings::AspectRatio::Ratio4x3)
    {
        return
            OpenScopeSettings::aspectRatioValue(
                OpenScopeSettings::AspectRatio::Ratio4x3);
    }

    return
        OpenScopeSettings::aspectRatioValue(
            OpenScopeSettings::AspectRatio::Ratio16x9);
}

void MainWindow::applyDisplayAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio,
    bool resizeWindow)
{
    videoWidget_->setAspectRatio(
        aspectRatio);

    waveformWidget_->setAspectRatio(
        aspectRatio);

    vectorscopeWidget_->setAspectRatio(
        aspectRatio);

    videoEngine_->setWaveformAspectRatio(
        aspectRatio);

    if (workspace_ != nullptr)
    {
        workspace_->setAspectRatio(
            aspectRatio);
    }

    if (!resizeWindow)
    {
        return;
    }

    const double aspect =
        OpenScopeSettings::aspectRatioValue(
            aspectRatio);

    // If the OpenScope window is in our aspect-ratio-constrained
    // custom maximized state, two geometries must be updated:
    //
    // 1. The saved normal geometry, otherwise restoring from
    //    maximized uses the aspect ratio that was active before
    //    the switch.
    // 2. The currently maximized geometry itself. It must be fit
    //    to the monitor work area using the new aspect ratio.
    if (customMaximized_)
    {
        if (restoreWindowGeometry_.isValid())
        {
            const int restoredHeight =
                static_cast<int>(
                    std::lround(
                        static_cast<double>(
                            restoreWindowGeometry_.width()) /
                        aspect));

            restoreWindowGeometry_.setHeight(
                restoredHeight);
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
                        static_cast<double>(
                            windowWidth) /
                        aspect));

            if (windowHeight > availableHeight)
            {
                windowHeight =
                    availableHeight;

                windowWidth =
                    static_cast<int>(
                        std::lround(
                            static_cast<double>(
                                windowHeight) *
                            aspect));
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

            return;
        }
    }

    const int newHeight =
        static_cast<int>(
            std::lround(
                static_cast<double>(width()) /
                aspect));

    resize(
        width(),
        newHeight);
}

void MainWindow::updateVideoFullscreenUi(
    bool fullscreen)
{
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
                "OpenScope V" OPENSCOPE_VERSION " - %1x%2")
            .arg(renderSize.width())
            .arg(renderSize.height()));
    }
    else
    {
        setWindowTitle(
            "OpenScope V" OPENSCOPE_VERSION);
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
                        windowAspectRatio()));

            if (windowHeight > availableHeight)
            {
                windowHeight =
                    availableHeight;

                windowWidth =
                    static_cast<int>(
                        std::lround(
                            windowHeight *
                            windowAspectRatio()));
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
                        windowAspectRatio()));

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
                        windowAspectRatio()));

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
                        windowAspectRatio()));

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
                        windowAspectRatio()));
        }

        if (currentHeight > maxHeight)
        {
            currentHeight =
                maxHeight;

            currentWidth =
                static_cast<int>(
                    std::lround(
                        currentHeight *
                        windowAspectRatio()));
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

    const QPoint performancePosition =
        performanceWidget_ != nullptr
        ? performanceWidget_->pos()
        : QPoint();

    const bool performancePositionValid =
        performanceWidget_ != nullptr;

    const QPoint settingsPosition =
        workspace_ != nullptr
        ? workspace_->floatingSettingsPosition()
        : QPoint();

    const bool settingsPositionValid =
        workspace_ != nullptr &&
        workspace_->hasFloatingSettingsPosition();

    settingsService_->update(
        [&geometry,
         this,
         performancePosition,
         performancePositionValid,
         settingsPosition,
         settingsPositionValid](OpenScopeSettings& settings)
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

            if (performancePositionValid)
            {
                settings.local
                    .floaties
                    .performance
                    .x =
                    performancePosition.x();

                settings.local
                    .floaties
                    .performance
                    .y =
                    performancePosition.y();

                settings.local
                    .floaties
                    .performance
                    .positionValid =
                    true;
            }

            if (settingsPositionValid)
            {
                settings.local
                    .floaties
                    .settings
                    .x =
                    settingsPosition.x();

                settings.local
                    .floaties
                    .settings
                    .y =
                    settingsPosition.y();

                settings.local
                    .floaties
                    .settings
                    .positionValid =
                    true;
            }
        });

    QMainWindow::closeEvent(event);
}