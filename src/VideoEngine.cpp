#include <QMetaObject>
#include "VideoEngine.h"
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
#include <vector>
#include <thread>

VideoEngine::VideoEngine(QObject* parent)
    : QObject(parent)
{
    setSelectedLine(320);
    reconstructedLuma_.resize(
        2880,
        576);
    const unsigned int hardwareThreads =
        std::max(
            1u,
            std::thread::hardware_concurrency());

    const unsigned int workerCount =
        std::max(
            1u,
            hardwareThreads / 4u);

    lumaWorkers_.resize(workerCount);

    for (LumaWorker& worker : lumaWorkers_)
    {
        worker.sourceLine.resize(720u);
        worker.reconstructedLine.resize(2880u);
    }

}
void VideoEngine::reconstructLuma(
    const Yuv444Frame& frame)
{
    if (frame.width <= 0 ||
        frame.height <= 0)
    {
        return;
    }

    reconstructedLuma_.resize(
        2880,
        frame.height);

    const int workerCount =
        static_cast<int>(lumaWorkers_.size());

    const int linesPerWorker =
        (frame.height + workerCount - 1) /
        workerCount;

    std::vector<std::jthread> threads;
    threads.reserve(
        static_cast<std::size_t>(workerCount));

    for (int workerIndex = 0;
        workerIndex < workerCount;
        ++workerIndex)
    {
        const int firstLine =
            workerIndex * linesPerWorker;

        const int lastLine =
            std::min(
                firstLine + linesPerWorker,
                frame.height);

        if (firstLine >= lastLine)
        {
            continue;
        }

        threads.emplace_back(
            [&, workerIndex, firstLine, lastLine]()
            {
                LumaWorker& worker =
                    lumaWorkers_[
                        static_cast<std::size_t>(workerIndex)];

                for (int line = firstLine;
                    line < lastLine;
                    ++line)
                {
                    const std::size_t sourceOffset =
                        static_cast<std::size_t>(line) *
                        static_cast<std::size_t>(frame.width);

                    for (int x = 0;
                        x < frame.width;
                        ++x)
                    {
                        worker.sourceLine[
                            static_cast<std::size_t>(x)] =
                            static_cast<float>(
                                frame.y[
                                    sourceOffset +
                                        static_cast<std::size_t>(x)]);
                    }

                    worker.reconstructor.resample(
                        worker.sourceLine,
                        worker.reconstructedLine);

                    const std::size_t destinationOffset =
                        static_cast<std::size_t>(line) *
                        2880u;

                    for (std::size_t x = 0;
                        x < 2880u;
                        ++x)
                    {
                        reconstructedLuma_.y[
                            destinationOffset + x] =
                            static_cast<std::uint16_t>(
                                std::clamp(
                                    worker.reconstructedLine[x],
                                    0.0f,
                                    65535.0f));
                    }
                }
            });
    }
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

    reconstructLuma(frame);

    const qint64 reconstructUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

    waveformRenderer_.analyze(
        frame,
        reconstructedLuma_);

    const qint64 waveformUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

    vectorscopeAnalyzer_.analyze(frame);

    const qint64 vectorscopeUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

    emit waveformChanged(
        waveformRenderer_.image());

    emit vectorscopeChanged(
        vectorscopeAnalyzer_.image());

    const qint64 scopeEmitUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

    const QImage displayImage =
        displayConverter_.convert(
            frame,
            reconstructedLuma_,
            videoOutputWidth_,
            frame.height);

    const qint64 displayConvertUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

    setFrame(displayImage);

    const qint64 frameEmitUs =
        timer.nsecsElapsed() / 1000;

    static int timingFrameCounter = 0;

    if (++timingFrameCounter >= 25)
    {
        timingFrameCounter = 0;

        const double totalMs =
            (
                reconstructUs +
                waveformUs +
                vectorscopeUs +
                scopeEmitUs +
                displayConvertUs +
                frameEmitUs
                ) /
            1000.0;

        qDebug()
            << "VideoEngine:"
            << "Reconstruct =" << reconstructUs / 1000.0 << "ms"
            << "Waveform =" << waveformUs / 1000.0 << "ms"
            << "Vectorscope =" << vectorscopeUs / 1000.0 << "ms"
            << "Scope emits =" << scopeEmitUs / 1000.0 << "ms"
            << "Display convert =" << displayConvertUs / 1000.0 << "ms"
            << "Frame emit =" << frameEmitUs / 1000.0 << "ms"
            << "Total =" << totalMs << "ms";
    }
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