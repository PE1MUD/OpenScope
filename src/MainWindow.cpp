#include "MainWindow.h"

#include "TestPatternGenerator.h"
#include "VideoEngine.h"
#include "VideoWidget.h"
#include "DeckLinkProbe.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoEngine_(new VideoEngine(this))
    , videoWidget_(new VideoWidget(this))
{
    setWindowTitle("OpenScope");
    resize(900, 720);

    setCentralWidget(videoWidget_);

    connect(
        videoEngine_,
        &VideoEngine::frameChanged,
        videoWidget_,
        &VideoWidget::setImage);

    videoEngine_->setFrame(
        TestPatternGenerator::generate(720, 576));
}

VideoWidget* MainWindow::videoWidget() const
{
    return videoWidget_;
}

MainWindow::~MainWindow()
{
    deckLinkStop();
}