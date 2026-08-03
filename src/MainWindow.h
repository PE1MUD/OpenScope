#pragma once

#include <QMainWindow>

class VideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
private:
    VideoWidget* videoWidget_;
public:
    explicit MainWindow(QWidget* parent = nullptr);

};
