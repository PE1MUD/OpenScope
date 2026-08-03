#pragma once

#include <QMainWindow>
class VideoEngine;
class VideoWidget;

class VideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
private:
    VideoWidget* videoWidget_;
    VideoEngine* videoEngine_;
    VideoWidget* videoWidget_;
public:
    explicit MainWindow(QWidget* parent = nullptr);

};
