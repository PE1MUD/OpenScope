#include "ScopeWorkspace.h"
#include "widgets/WaveformWidget.h"
#include "ScopeViewport.h"
#include "widgets/ControlWidget.h"

#include <QGridLayout>
#include <QPoint>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kFloatingSettingsWidth = 360;
}

ScopeWorkspace::ScopeWorkspace(
    QWidget* videoWidget,
    QWidget* waveformWidget,
    QWidget* vectorscopeWidget,
    const OpenScopeSettings& settings,
    QWidget* parent)
    : QWidget(parent)
    , aspectRatio_(
        settings.local.display.aspectRatio)
    , settingsFloatingPosition_(
        settings.local.floaties.settings.x,
        settings.local.floaties.settings.y)
    , settingsFloatingPositionValid_(
        settings.local.floaties.settings.positionValid)
{
    layout_ =
        new QGridLayout(this);

    setMinimumSize(
        768,
        432);

    layout_->setContentsMargins(
        0,
        0,
        0,
        0);

    layout_->setSpacing(4);

    videoViewport_ =
        new ScopeViewport(
            videoWidget,
            this);

    waveformViewport_ =
        new ScopeViewport(
            waveformWidget,
            this);

    vectorscopeViewport_ =
        new ScopeViewport(
            vectorscopeWidget,
            this);

    controlWidget_ =
        new ControlWidget(
            settings,
            this);

    settingsViewport_ =
        new ScopeViewport(
            controlWidget_,
            this);

    layout_->addWidget(
        videoViewport_,
        0,
        0);

    layout_->addWidget(
        waveformViewport_,
        0,
        1);

    layout_->addWidget(
        vectorscopeViewport_,
        1,
        0);

    layout_->addWidget(
        settingsViewport_,
        1,
        1);

    layout_->setRowStretch(
        0,
        1);

    layout_->setRowStretch(
        1,
        1);

    layout_->setColumnStretch(
        0,
        1);

    layout_->setColumnStretch(
        1,
        1);

    connect(
        videoViewport_,
        &ScopeViewport::doubleClicked,
        this,
        &ScopeWorkspace::toggleMaximized);

    connect(
        waveformViewport_,
        &ScopeViewport::doubleClicked,
        this,
        &ScopeWorkspace::toggleMaximized);

    connect(
        vectorscopeViewport_,
        &ScopeViewport::doubleClicked,
        this,
        &ScopeWorkspace::toggleMaximized);

    connect(
        controlWidget_,
        &ControlWidget::lineNumberChanged,
        this,
        [this, waveformWidget](int lineNumber)
        {
            if (auto* waveform =
                    qobject_cast<WaveformWidget*>(
                        waveformWidget))
            {
                waveform->clearMeasurements();
            }

            emit lineNumberChanged(
                lineNumber);
        });

    connect(
        controlWidget_,
        &ControlWidget::waveformZoomChanged,
        this,
        &ScopeWorkspace::waveformZoomChanged);

    connect(
        controlWidget_,
        &ControlWidget::waveformPersistenceChanged,
        this,
        &ScopeWorkspace::waveformPersistenceChanged);

    connect(
        controlWidget_,
        &ControlWidget::vectorscopeGlowChanged,
        this,
        &ScopeWorkspace::vectorscopeGlowChanged);

    connect(
        controlWidget_,
        &ControlWidget::vintageLookChanged,
        this,
        [this](bool enabled)
        {
            emit waveformColorChanged(
                !enabled);
        });

    connect(
        controlWidget_,
        &ControlWidget::chromaRenderIntensityChanged,
        this,
        &ScopeWorkspace::waveformChromaFillIntensityChanged);

    connect(
        controlWidget_,
        &ControlWidget::performanceVisibilityChanged,
        this,
        &ScopeWorkspace::performanceVisibilityChanged);

    connect(
        controlWidget_,
        &ControlWidget::floatiesHomeRequested,
        this,
        &ScopeWorkspace::floatiesHomeRequested);

    connect(
        controlWidget_,
        &ControlWidget::spoutVideoEnabledChanged,
        this,
        &ScopeWorkspace::spoutVideoEnabledChanged);

    connect(
        controlWidget_,
        &ControlWidget::spoutWaveformEnabledChanged,
        this,
        &ScopeWorkspace::spoutWaveformEnabledChanged);

    connect(
        controlWidget_,
        &ControlWidget::spoutVectorscopeEnabledChanged,
        this,
        &ScopeWorkspace::spoutVectorscopeEnabledChanged);

    connect(
        controlWidget_,
        &ControlWidget::noiseReductionChanged,
        this,
        &ScopeWorkspace::noiseReductionChanged);

    connect(
        controlWidget_,
        &ControlWidget::noiseReductionIntensityChanged,
        this,
        &ScopeWorkspace::noiseReductionIntensityChanged);

    connect(
        controlWidget_,
        &ControlWidget::legacyAspectRatioChanged,
        this,
        &ScopeWorkspace::legacyAspectRatioChanged);

    connect(
        controlWidget_,
        &ControlWidget::exportHighResolutionPngRequested,
        this,
        &ScopeWorkspace::exportHighResolutionPngRequested);

    connect(
        controlWidget_,
        &ControlWidget::exportHighResolutionPngQuickRequested,
        this,
        &ScopeWorkspace::exportHighResolutionPngQuickRequested);
}

void ScopeWorkspace::toggleMaximized(
    ScopeViewport* viewport)
{
    if (maximizedViewport_ == viewport)
    {
        const bool leavingVideo =
            viewport == videoViewport_;

        showGrid();

        if (leavingVideo)
        {
            emit videoMaximizedChanged(false);
        }

        emit workspaceViewChanged(
            OpenScopeSettings::WorkspaceView::Matrix);

        return;
    }

    const bool videoWasMaximized =
        maximizedViewport_ == videoViewport_;

    showMaximized(viewport);

    const bool videoIsMaximized =
        viewport == videoViewport_;

    if (videoWasMaximized != videoIsMaximized)
    {
        emit videoMaximizedChanged(
            videoIsMaximized);
    }

    if (viewport == videoViewport_)
    {
        emit workspaceViewChanged(
            OpenScopeSettings::WorkspaceView::Video);
    }
    else if (viewport == waveformViewport_)
    {
        emit workspaceViewChanged(
            OpenScopeSettings::WorkspaceView::Waveform);
    }
    else if (viewport == vectorscopeViewport_)
    {
        emit workspaceViewChanged(
            OpenScopeSettings::WorkspaceView::Vectorscope);
    }
}

void ScopeWorkspace::showMaximized(
    ScopeViewport* viewport)
{
    controlWidget_->setHelpTabVisible(
        false);

    videoViewport_->hide();
    waveformViewport_->hide();
    vectorscopeViewport_->hide();
    settingsViewport_->hide();

    layout_->removeWidget(
        viewport);

    layout_->addWidget(
        viewport,
        0,
        0,
        2,
        2);

    viewport->show();
    viewport->focusContent();

    maximizedViewport_ =
        viewport;

    floatSettings();
}

void ScopeWorkspace::showGrid()
{
    controlWidget_->setHelpTabVisible(
        true);

    dockSettings();

    layout_->removeWidget(
        videoViewport_);

    layout_->removeWidget(
        waveformViewport_);

    layout_->removeWidget(
        vectorscopeViewport_);

    layout_->addWidget(
        videoViewport_,
        0,
        0);

    layout_->addWidget(
        waveformViewport_,
        0,
        1);

    layout_->addWidget(
        vectorscopeViewport_,
        1,
        0);

    layout_->addWidget(
        settingsViewport_,
        1,
        1);

    videoViewport_->show();
    waveformViewport_->show();
    vectorscopeViewport_->show();
    settingsViewport_->show();

    maximizedViewport_ =
        nullptr;
}

void ScopeWorkspace::floatSettings()
{
    if (settingsFloating_)
    {
        resizeFloatingSettings();
        settingsViewport_->show();
        return;
    }

    const QPoint floatingPosition =
        settingsFloatingPositionValid_
        ? settingsFloatingPosition_
        : mapToGlobal(
            QPoint(
                (std::max)(
                    width() -
                    kFloatingSettingsWidth -
                    20,
                    20),
                20));

    QWindow* mainWindowHandle = nullptr;

    if (QWidget* const mainWindow = window())
    {
        // Force creation of the native main-window handle before
        // the settings viewport becomes a top-level tool window.
        mainWindow->winId();
        mainWindowHandle =
            mainWindow->windowHandle();
    }

    layout_->removeWidget(
        settingsViewport_);

    settingsViewport_->hide();
    settingsViewport_->setParent(nullptr);

    settingsViewport_->setWindowFlags(
        Qt::Tool |
        Qt::CustomizeWindowHint |
        Qt::WindowTitleHint);

    settingsViewport_->setWindowTitle(
        "OpenScope Settings");

    // Force creation of the tool-window handle so we can make it
    // transient for the OpenScope main window. This keeps Settings
    // above OpenScope without making it system-wide always-on-top.
    settingsViewport_->winId();

    if (QWindow* const settingsWindowHandle =
        settingsViewport_->windowHandle())
    {
        settingsWindowHandle->setTransientParent(
            mainWindowHandle);
    }

    resizeFloatingSettings();
    settingsViewport_->move(
        floatingPosition);

    settingsFloatingPosition_ =
        floatingPosition;

    settingsFloatingPositionValid_ =
        true;

    settingsFloating_ = true;
    settingsViewport_->show();
    settingsViewport_->raise();
}

void ScopeWorkspace::dockSettings()
{
    if (!settingsFloating_)
    {
        return;
    }

    settingsFloatingPosition_ =
        settingsViewport_->pos();

    settingsFloatingPositionValid_ =
        true;

    settingsViewport_->hide();
    settingsViewport_->setParent(this);
    settingsViewport_->setWindowFlags(
        Qt::Widget);

    settingsFloating_ = false;
}

void ScopeWorkspace::resizeFloatingSettings()
{
    const double aspectRatio =
        OpenScopeSettings::aspectRatioValue(
            aspectRatio_);

    const int height =
        static_cast<int>(
            std::lround(
                static_cast<double>(
                    kFloatingSettingsWidth) /
                aspectRatio));

    settingsViewport_->resize(
        kFloatingSettingsWidth,
        height);
}

QPoint ScopeWorkspace::floatingSettingsPosition() const
{
    if (settingsFloating_)
    {
        return settingsViewport_->pos();
    }

    return settingsFloatingPosition_;
}

void ScopeWorkspace::homeFloatingSettings(
    const QPoint& position)
{
    settingsFloatingPosition_ =
        position;

    settingsFloatingPositionValid_ =
        true;

    if (settingsFloating_ &&
        settingsViewport_ != nullptr)
    {
        settingsViewport_->move(
            position);
    }
}

bool ScopeWorkspace::hasFloatingSettingsPosition() const
{
    return
        settingsFloating_ ||
        settingsFloatingPositionValid_;
}

void ScopeWorkspace::setWorkspaceView(
    OpenScopeSettings::WorkspaceView view)
{
    const bool videoWasMaximized =
        maximizedViewport_ == videoViewport_;

    switch (view)
    {
    case OpenScopeSettings::WorkspaceView::Video:
        showMaximized(videoViewport_);
        break;

    case OpenScopeSettings::WorkspaceView::Waveform:
        showMaximized(waveformViewport_);
        break;

    case OpenScopeSettings::WorkspaceView::Vectorscope:
        showMaximized(vectorscopeViewport_);
        break;

    case OpenScopeSettings::WorkspaceView::Matrix:
    case OpenScopeSettings::WorkspaceView::Headless:
        showGrid();
        break;
    }

    const bool videoIsMaximized =
        maximizedViewport_ == videoViewport_;

    if (videoWasMaximized != videoIsMaximized)
    {
        emit videoMaximizedChanged(
            videoIsMaximized);
    }
}

void ScopeWorkspace::setLineNumber(
    int lineNumber)
{
    controlWidget_->setLineNumber(
        lineNumber);
}

void ScopeWorkspace::setWaveformZoomFactor(
    int zoomFactor)
{
    controlWidget_->setWaveformZoomFactor(
        zoomFactor);
}

void ScopeWorkspace::setPerformanceVisible(
    bool visible)
{
    controlWidget_->setPerformanceVisible(
        visible);
}

void ScopeWorkspace::setAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    if (aspectRatio_ == aspectRatio)
    {
        return;
    }

    aspectRatio_ = aspectRatio;

    controlWidget_->setAspectRatio(
        aspectRatio);

    if (settingsFloating_)
    {
        resizeFloatingSettings();
    }
}

bool ScopeWorkspace::isVideoMaximized() const
{
    return
        maximizedViewport_ ==
        videoViewport_;
}
