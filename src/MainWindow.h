#pragma once

#include "settings/OpenScopeSettings.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QRect>
#include <QSize>
#include <QString>

#include <memory>

class QTimer;
class QThread;

class VectorscopeWidget;
class VideoEngine;
class VideoWidget;
class WaveformWidget;
class YSpectrumWindow;
class ScopeWorkspace;
class SettingsService;
class PerformanceWidget;
class QAction;
class PhilipsPatternRomSource;
class SpoutOutput;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    VideoWidget* videoWidget() const;
    VideoEngine* videoEngine() const;
    void setBlackmagicDeviceName(const QString& deviceName);

protected:
    void closeEvent(QCloseEvent* event) override;

    bool eventFilter(QObject* watched, QEvent* event) override;

    bool nativeEvent(
        const QByteArray& eventType,
        void* message,
        qintptr* result) override;

private:
    enum class RenderView
    {
        Matrix,
        Video,
        Waveform,
        Vectorscope
    };

    VideoWidget* videoWidget_ = nullptr;
    VideoEngine* videoEngine_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    YSpectrumWindow* ySpectrumWindow_ = nullptr;
    ScopeWorkspace* workspace_ = nullptr;
    VectorscopeWidget* vectorscopeWidget_ = nullptr;

    QRect restoreWindowGeometry_;
    bool customMaximized_ = false;

    // F11 clean fullscreen state. This is intentionally separate from
    // the custom aspect-ratio maximize handling above.
    QRect f11RestoreWindowGeometry_;
    bool f11FullScreen_ = false;
    bool f11MenuBarWasVisible_ = true;

    SettingsService* settingsService_ = nullptr;
    PerformanceWidget* performanceWidget_ = nullptr;
    QTimer* performanceTimer_ = nullptr;

    // Persist selected-line changes only after navigation has settled.
    // Mouse drag / wheel / key repeat can otherwise cause a synchronous
    // OpenScope.ini write for every intermediate line.
    QTimer* lineNumberPersistTimer_ = nullptr;
    int pendingLineNumber_ = -1;

    QThread* videoSpoutThread_ = nullptr;
    SpoutOutput* videoSpoutOutput_ = nullptr;

    std::unique_ptr<PhilipsPatternRomSource>
        philipsPatternRomSource_;

    QAction* blackmagicSourceAction_ = nullptr;
    QAction* philipsPatternRomSourceAction_ = nullptr;
    QAction* reloadPhilipsPatternRomAction_ = nullptr;

    QSize videoRenderSize_;
    QSize waveformRenderSize_;
    QSize vectorscopeRenderSize_;
    QString blackmagicDeviceName_ = QStringLiteral("BMD");

    RenderView activeRenderView_ =
        RenderView::Matrix;

    int preVideoClickLineNumber_ = -1;
    double preVideoClickScrollPosition_ = 0.0;
    bool preVideoClickStateValid_ = false;

    double windowAspectRatio() const;

    void applyDisplayAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio,
        bool resizeWindow);

    void updateVideoFullscreenUi(
        bool fullscreen);

    void updateRenderResolutionTitle();
    void updateScreenRenderDemand();
    void homeFloaties();

    void createSourceMenu();
    void selectBlackmagicSource();
    void selectPhilipsPatternRomSource();
    void reloadPhilipsPatternRomSource();
};
