#include "MainWindow.h"
#include "VideoWidget.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("OpenScope");
    resize(1280, 720);
    setCentralWidget(new VideoWidget(this));
}