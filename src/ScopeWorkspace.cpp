#include "ScopeWorkspace.h"
#include "ScopeViewport.h"
#include "widgets/ControlWidget.h"

#include <QGridLayout>

ScopeWorkspace::ScopeWorkspace(
    QWidget* videoWidget,
    QWidget* waveformWidget,
    QWidget* vectorscopeWidget,
    bool vintageLook,
    int chromaRenderIntensity,
    QWidget* parent)
    : QWidget(parent)
{
    layout_ =
        new QGridLayout(this);

    setMinimumSize(
        768,
        576);

    layout_->setContentsMargins(
        0,
        0,
        0,
        0);

    layout_->setSpacing(
        4);

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
            vintageLook,
            chromaRenderIntensity,
            this);

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

    yuvViewport_ =
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
        yuvViewport_,
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
        &ControlWidget::performanceVisibilityChanged,
        this,
        &ScopeWorkspace::performanceVisibilityChanged);
}

void ScopeWorkspace::toggleMaximized(
    ScopeViewport* viewport)
{
    if (maximizedViewport_ == viewport)
    {
        showGrid();

        emit workspaceViewChanged(
            OpenScopeSettings::WorkspaceView::Matrix);

        return;
    }

    showMaximized(viewport);

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
    videoViewport_->hide();
    waveformViewport_->hide();
    vectorscopeViewport_->hide();
    yuvViewport_->hide();

    layout_->removeWidget(
        viewport);

    viewport->show();

    layout_->addWidget(
        viewport,
        0,
        0,
        2,
        2);

    maximizedViewport_ =
        viewport;
}

void ScopeWorkspace::showGrid()
{
    layout_->removeWidget(
        videoViewport_);

    layout_->removeWidget(
        waveformViewport_);

    layout_->removeWidget(
        vectorscopeViewport_);

    layout_->removeWidget(
        yuvViewport_);

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
        yuvViewport_,
        1,
        1);

    videoViewport_->show();
    waveformViewport_->show();
    vectorscopeViewport_->show();
    yuvViewport_->show();

    maximizedViewport_ =
        nullptr;
}

void ScopeWorkspace::setWorkspaceView(
    OpenScopeSettings::WorkspaceView view)
{
    switch (view)
    {
    case OpenScopeSettings::WorkspaceView::Video:
        showMaximized(
            videoViewport_);
        break;

    case OpenScopeSettings::WorkspaceView::Waveform:
        showMaximized(
            waveformViewport_);
        break;

    case OpenScopeSettings::WorkspaceView::Vectorscope:
        showMaximized(
            vectorscopeViewport_);
        break;

    case OpenScopeSettings::WorkspaceView::Matrix:
        showGrid();
        break;

    case OpenScopeSettings::WorkspaceView::Headless:
        showGrid();
        break;
    }
}

void ScopeWorkspace::setPerformanceVisible(
    bool visible)
{
    controlWidget_->setPerformanceVisible(
        visible);
}