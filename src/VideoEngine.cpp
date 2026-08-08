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
        kReconstructedLumaWidth,
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
        worker.reconstructedLine.resize(
            kReconstructedLumaWidth);
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
        kReconstructedLumaWidth,
        frame.height);

    const int workerCount =
        static_cast<int>(lumaWorkers_.size());

    const int linesPerWorker =
        (frame.height + workerCount - 1) /
        workerCount;

    std::vector<std::jthread> threads;
    threads.reserve(
        static_cast<std::size_t>(workerCount));

    std::vector<qint64> copyInputUs(
        static_cast<std::size_t>(workerCount),
        0);

    std::vector<qint64> resampleUs(
        static_cast<std::size_t>(workerCount),
        0);

    std::vector<qint64> copyOutputUs(
        static_cast<std::size_t>(workerCount),
        0);

    QElapsedTimer timer;
    timer.start();

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
                        static_cast<std::size_t>(
                            workerIndex)];

                qint64 localCopyInputUs = 0;
                qint64 localResampleUs = 0;
                qint64 localCopyOutputUs = 0;

                QElapsedTimer workerTimer;

                for (int line = firstLine;
                    line < lastLine;
                    ++line)
                {
                    const std::size_t sourceOffset =
                        static_cast<std::size_t>(line) *
                        static_cast<std::size_t>(frame.width);

                    workerTimer.restart();

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

                    localCopyInputUs +=
                        workerTimer.nsecsElapsed() / 1000;

                    workerTimer.restart();

                    worker.reconstructor.resample(
                        worker.sourceLine,
                        worker.reconstructedLine);

                    localResampleUs +=
                        workerTimer.nsecsElapsed() / 1000;

                    const std::size_t destinationOffset =
                        static_cast<std::size_t>(line) *
                        kReconstructedLumaWidth;

                    workerTimer.restart();

                    for (std::size_t x = 0;
                        x < kReconstructedLumaWidth;
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

                    localCopyOutputUs +=
                        workerTimer.nsecsElapsed() / 1000;
                }

                copyInputUs[
                    static_cast<std::size_t>(workerIndex)] =
                    localCopyInputUs;

                    resampleUs[
                        static_cast<std::size_t>(workerIndex)] =
                        localResampleUs;

                        copyOutputUs[
                            static_cast<std::size_t>(workerIndex)] =
                            localCopyOutputUs;
            });
    }

    const qint64 threadLaunchUs =
        timer.nsecsElapsed() / 1000;

    timer.restart();

    threads.clear();

    const qint64 threadJoinUs =
        timer.nsecsElapsed() / 1000;

    qint64 totalCopyInputUs = 0;
    qint64 totalResampleUs = 0;
    qint64 totalCopyOutputUs = 0;

    for (int workerIndex = 0;
        workerIndex < workerCount;
        ++workerIndex)
    {
        const std::size_t index =
            static_cast<std::size_t>(workerIndex);

        totalCopyInputUs +=
            copyInputUs[index];

        totalResampleUs +=
            resampleUs[index];

        totalCopyOutputUs +=
            copyOutputUs[index];
    }

    qDebug()
        << "Luma threads:"
        << "launch =" << threadLaunchUs / 1000.0 << "ms"
        << "join =" << threadJoinUs / 1000.0 << "ms"
        << "copy input =" << totalCopyInputUs / 1000.0 << "ms"
        << "resample =" << totalResampleUs / 1000.0 << "ms"
        << "copy output =" << totalCopyOutputUs / 1000.0 << "ms";
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