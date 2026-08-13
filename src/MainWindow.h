#pragma once

#include <QMainWindow>
#include <QRect>
#include <QCloseEvent>
class QTimer;

class VectorscopeWidget;
class VideoEngine;
class VideoWidget;
class WaveformWidget;
class ScopeWorkspace;
class SettingsService;
class PerformanceWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    VideoWidget* videoWidget() const;
    VideoEngine* videoEngine() const;

protected:
    void closeEvent(QCloseEvent* event) override;
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
    QRect restoreWindowGeometry_;
    bool customMaximized_ = false;
    SettingsService* settingsService_ = nullptr;
    PerformanceWidget* performanceWidget_ = nullptr;
    QTimer* performanceTimer_ = nullptr;
};