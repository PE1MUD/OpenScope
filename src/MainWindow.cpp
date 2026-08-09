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

#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoEngine_(new VideoEngine(this))
    , videoWidget_(new VideoWidget)
{
    setWindowTitle("OpenScope");
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