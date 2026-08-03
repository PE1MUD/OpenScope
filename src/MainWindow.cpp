#include "MainWindow.h"

#include "TestPatternGenerator.h"
#include "VideoWidget.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , videoWidget_(new VideoWidget(this))
{
    setWindowTitle("OpenScope");
    resize(900, 720);

    setCentralWidget(videoWidget_);

    videoWidget_->setImage(
        TestPatternGenerator::generate(720, 576));
}