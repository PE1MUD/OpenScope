#pragma once

#include <QObject>
#include <QImage>
#include <atomic>

#include "video/DisplayConverter.h"
#include "video/Yuv444Frame.h"
#include "video/FrameBufferPool.h"
#include "rendering/WaveformRenderer.h"
#include "processing/SignalReconstructor.h"

class VideoEngine : public QObject
{
    Q_OBJECT

public:
    explicit VideoEngine(QObject* parent = nullptr);

    void setFrame(const QImage& frame);
    void setFrame(const Yuv444Frame& frame);

    Yuv444Frame* tryAcquireWriteFrame();
    void submitWriteFrame();
    void cancelWriteFrame();

    void setSelectedLine(int line);
    
    void setWaveformOutputSize(
        int width,
        int height);

    double displayBandwidthMHz() const;
    double traceBandwidthMHz() const;
    const QImage& currentFrame() const;
    void setWaveformPersistence(int persistence);

signals:
    void frameChanged(const QImage& image);
    void waveformChanged(const QImage& image);

private:
    QImage currentFrame_;
    DisplayConverter displayConverter_;
    FrameBufferPool frameBufferPool_{ 720, 576 };
    std::atomic_bool framePending_{ false };
    WaveformRenderer waveformRenderer_;
    LineResampler lineResampler_;
};