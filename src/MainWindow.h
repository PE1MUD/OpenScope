#pragma once

#include <QMainWindow>

class VideoEngine;
class VideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    VideoWidget* videoWidget() const;
    VideoEngine* videoEngine() const;

private:
    VideoWidget* videoWidget_;
    VideoEngine* videoEngine_;
};