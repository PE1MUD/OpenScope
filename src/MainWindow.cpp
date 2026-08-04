#include <QSplitter>
#include "MainWindow.h"
#include "TestPatternGenerator.h"
#include "VideoEngine.h"
#include "VideoWidget.h"
#include "WaveformWidget.h"
#include "DeckLinkProbe.h"
#include <QSpinBox>
#include <QToolBar>
#include <QSlider>
#include <QLabel>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoEngine_(new VideoEngine(this))
    , videoWidget_(new VideoWidget(this))
{
    setWindowTitle("OpenScope");
    resize(1800, 720);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    splitter->addWidget(videoWidget_);

    waveformWidget_ = new WaveformWidget(this);
    splitter->addWidget(waveformWidget_);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    setCentralWidget(splitter);
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
        persistenceSlider,
        &QSlider::valueChanged,
        videoEngine_,
        &VideoEngine::setWaveformPersistence);
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