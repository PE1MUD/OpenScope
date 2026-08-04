#include <QSplitter>
#include "MainWindow.h"
#include "TestPatternGenerator.h"
#include "VideoEngine.h"
#include "VideoWidget.h"
#include "WaveformWidget.h"
#include "DeckLinkProbe.h"

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