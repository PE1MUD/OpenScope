#include "MainWindow.h"
#include "ScopeWorkspace.h"
#include "TestPatternGenerator.h"
#include "VideoEngine.h"
#include "VideoWidget.h"
#include "WaveformWidget.h"
#include "DeckLinkProbe.h"
#include "VectorscopeWidget.h"
#include <QSpinBox>
#include <QToolBar>
#include <QSlider>
#include <QLabel>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoEngine_(new VideoEngine(this))
    , videoWidget_(new VideoWidget)
{
    setWindowTitle("OpenScope");
    resize(900, 720);

    waveformWidget_ = new WaveformWidget;

    vectorscopeWidget_ =
        new VectorscopeWidget;

    workspace_ = new ScopeWorkspace(
        videoWidget_,
        waveformWidget_,
        vectorscopeWidget_,
        this);

    setCentralWidget(workspace_);

    auto* toolbar = addToolBar("Line selector");

    auto* lineSelector = new QSpinBox(toolbar);
    lineSelector->setRange(-1, 575);
    lineSelector->setValue(88);
    lineSelector->setSpecialValueText("All");

    toolbar->addWidget(lineSelector);

    connect(
        lineSelector,
        &QSpinBox::valueChanged,
        videoEngine_,
        &VideoEngine::setSelectedLine);
    auto* persistenceLabel = new QLabel("Pers", toolbar);
    toolbar->addWidget(persistenceLabel);

    auto* persistenceSlider = new QSlider(Qt::Horizontal, toolbar);
    persistenceSlider->setRange(0, 255);
    persistenceSlider->setValue(0);
    persistenceSlider->setFixedWidth(140);

    toolbar->addWidget(persistenceSlider);
    connect(
        videoEngine_,
        &VideoEngine::vectorscopeChanged,
        vectorscopeWidget_,
        &VectorscopeWidget::setImage);
    connect(
        persistenceSlider,
        &QSlider::valueChanged,
        videoEngine_,
        &VideoEngine::setWaveformPersistence);
    connect(
        waveformWidget_,
        &WaveformWidget::outputSizeChanged,
        videoEngine_,
        &VideoEngine::setWaveformOutputSize);
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
            waveformWidget_->setImage(image);
            waveformWidget_->notifyFrameRendered();
        });
    connect(
        waveformWidget_,
        &WaveformWidget::outputSizeChanged,
        this,
        [this](int w, int h)
        {
            Q_UNUSED(h);

            videoEngine_->setWaveformOutputSize(
                w,
                waveformWidget_->height());

            constexpr double captureSampleRateMHz = 13.5;
            constexpr double captureSamplesPerLine = 720.0;
            constexpr double pixelsPerCycleForAccurateTrace = 8.0;

            const double waveformBandwidthMHz =
                captureSampleRateMHz *
                static_cast<double>(w) /
                (captureSamplesPerLine *
                    pixelsPerCycleForAccurateTrace);

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