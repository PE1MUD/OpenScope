#include <QMetaObject>
#include "VideoEngine.h"

VideoEngine::VideoEngine(QObject* parent)
    : QObject(parent)
{
    setSelectedLine(88);
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
    waveformRenderer_.analyze(frame);
    emit waveformChanged(waveformRenderer_.image());
    setFrame(displayConverter_.convert(frame));
}

void VideoEngine::cancelWriteFrame()
{
    framePending_.store(false, std::memory_order_release);
}

void VideoEngine::setSelectedLine(int line)
{
    waveformRenderer_.setSelectedLine(line);
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

double VideoEngine::traceBandwidthMHz() const
{
    return waveformRenderer_.traceBandwidthMHz();
}