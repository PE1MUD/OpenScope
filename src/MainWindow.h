#pragma once

#include <QMainWindow>

class VideoEngine;
class VideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    VideoWidget* videoWidget() const;

private:
    VideoWidget* videoWidget_;
    VideoEngine* videoEngine_;
};