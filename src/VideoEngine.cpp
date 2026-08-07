#include <QMetaObject>
#include "VideoEngine.h"
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>

VideoEngine::VideoEngine(QObject* parent)
    : QObject(parent)
{
    setSelectedLine(320);
}

Yuv444Frame* VideoEngine::tryAcquireWriteFrame()
{
    bool expected = false;

    if (!framePending_.compare_exchange_strong(
        expected,
        true,
        std::memory_order_acquire))
    {
        return nullptr;
    }

    return &frameBufferPool_.writeBuffer();
}

void VideoEngine::submitWriteFrame()
{
    frameBufferPool_.publish();

    QMetaObject::invokeMethod(
        this,
        [this]()
        {
            setFrame(frameBufferPool_.readBuffer());

            framePending_.store(
                false,
                std::memory_order_release);
        },
        Qt::QueuedConnection);
}

void VideoEngine::setFrame(const QImage& frame)
{
    currentFrame_ = frame;
    emit frameChanged(currentFrame_);
}

void VideoEngine::setFrame(const Yuv444Frame& frame)
{
    QElapsedTimer timer;
    timer.start();

    waveformRenderer_.analyze(frame);
    vectorscopeAnalyzer_.analyze(frame);

    const qint64 analyzeMs =
        timer.restart();

    emit waveformChanged(
        waveformRenderer_.image());

    emit vectorscopeChanged(
        vectorscopeAnalyzer_.image());

    const qint64 emitMs =
        timer.restart();

    static int timingFrameCounter = 0;

    if (++timingFrameCounter >= 25)
    {
        timingFrameCounter = 0;

        qDebug()
            << "Waveform analyze:" << analyzeMs << "ms"
            << "Emit:" << emitMs << "ms"
            << "Total:" << analyzeMs + emitMs << "ms";
    }

    setFrame(
        displayConverter_.convert(
            frame,
            videoOutputWidth_,
            frame.height));
}

void VideoEngine::cancelWriteFrame()
{
    framePending_.store(false, std::memory_order_release);
}

void VideoEngine::setSelectedLine(int line)
{
    waveformRenderer_.setSelectedLine(line);
    vectorscopeAnalyzer_.setSelectedLine(line);

    displayConverter_.setHighlightedLine(line);
}

void VideoEngine::setWaveformPersistence(int persistence)
{
    waveformRenderer_.setPersistence(persistence);
}

void VideoEngine::setWaveformOutputSize(
    int width,
    int height)
{
    waveformRenderer_.setOutputSize(
        width,
        height);
}
void VideoEngine::setVectorscopeOutputSize(
    int width,
    int height)
{
    vectorscopeAnalyzer_.setOutputSize(
        width,
        height);
}
double VideoEngine::traceBandwidthMHz() const
{
    return waveformRenderer_.traceBandwidthMHz();
}

void VideoEngine::setVideoOutputSize(
    int width,
    int height)
{
    videoOutputWidth_ = std::max(width, 1);
    videoOutputHeight_ = std::max(height, 1);
}