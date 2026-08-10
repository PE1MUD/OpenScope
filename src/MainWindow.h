#pragma once

#include <QMainWindow>
class VectorscopeWidget;
class QResizeEvent;

class VideoEngine;
class VideoWidget;
class WaveformWidget;
class ScopeWorkspace;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    VideoWidget* videoWidget() const;
    VideoEngine* videoEngine() const;

protected:

    bool nativeEvent(
        const QByteArray& eventType,
        void* message,
        qintptr* result) override;

private:
    VideoWidget* videoWidget_ = nullptr;
    VideoEngine* videoEngine_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    ScopeWorkspace* workspace_ = nullptr;
    VectorscopeWidget* vectorscopeWidget_ = nullptr;



};