#include <QMetaObject>
#include "VideoEngine.h"
#include "VectorscopeSettings.h"
#include <QElapsedTimer>
#include <QDebug>
#include <algorithm>
#include <vector>
#include <thread>
#include <memory>

VideoEngine::VideoEngine(QObject* parent)
    : QObject(parent)
{
    setSelectedLine(320);
    vectorscopeAnalyzer_.setScale(
        VectorscopeSettings::scale);
    for (CapturedFrame& slot : captureSlots_)
    {
        slot.frame.resize(
            720,
            576);

        slot.generation = 0;
    }
    for (ReconstructedFrameSlot& slot :
        reconstructedSlots_)
    {
        slot.frame.resize(
            kReconstructedLumaWidth,
            576);

        slot.generation.store(
            0,
            std::memory_order_relaxed);

        slot.writing.store(
            false,
            std::memory_order_relaxed);
    }
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

    lumaThreads_.reserve(
        lumaWorkers_.size());

    for (std::size_t workerIndex = 0;
        workerIndex < lumaWorkers_.size();
        ++workerIndex)
    {
        lumaThreads_.emplace_back(
            [this, workerIndex]()
            {
                lumaWorkerLoop(workerIndex);
            });
    }

    lumaCoordinatorThread_ =
        std::jthread(
            [this]()
            {
                lumaCoordinatorLoop();
            });

    vectorscopeAnalyzer_.moveToThread(
        &vectorscopeThread_);

    connect(
        &vectorscopeThread_,
        &QThread::started,
        &vectorscopeAnalyzer_,
        [this]()
        {
            vectorscopeWorkerLoop();
        });

    vectorscopeThread_.start();

    connect(
        &waveformThread_,
        &QThread::started,
        this,
        [this]()
        {
            waveformWorkerLoop();
        },
        Qt::DirectConnection);

    waveformThread_.start();

    connect(
        &displayThread_,
        &QThread::started,
        this,
        [this]()
        {
            displayWorkerLoop();
        },
        Qt::DirectConnection);

    displayThread_.start();
    
}

void VideoEngine::reconstructLuma(
    const Yuv444Frame& frame,
    std::uint64_t generation)
{
    if (frame.width <= 0 ||
        frame.height <= 0)
    {
        return;
    }

    const int workerCount =
        static_cast<int>(
            lumaWorkers_.size());

    if (workerCount <= 0)
    {
        return;
    }

    const std::size_t outputSlotIndex =
        acquireNextReconstructedWriteSlot();

    ReconstructedFrameSlot& outputSlot =
        reconstructedSlots_[
            outputSlotIndex];

    outputSlot.writing.store(
        true,
        std::memory_order_release);

    outputSlot.frame.resize(
        kReconstructedLumaWidth,
        frame.height);

    {
        std::lock_guard<std::mutex> lock(
            lumaMutex_);

        lumaFrame_ =
            &frame;

        lumaOutputFrame_ =
            &outputSlot.frame;

        lumaWorkersRemaining_ =
            workerCount;

        ++lumaGeneration_;
    }

    lumaCondition_.notify_all();

    {
        std::unique_lock<std::mutex> lock(
            lumaMutex_);

        lumaDoneCondition_.wait(
            lock,
            [this]()
            {
                return
                    lumaWorkersRemaining_ == 0;
            });
    }

    outputSlot.frame.generation =
        generation;

    outputSlot.generation.store(
        generation,
        std::memory_order_release);

    outputSlot.writing.store(
        false,
        std::memory_order_release);


    latestReconstructedSlot_.store(
        static_cast<int>(
            outputSlotIndex),
        std::memory_order_release);

    waveformCondition_.notify_one();
    
    displayCondition_.notify_one();

}

Yuv444Frame* VideoEngine::tryAcquireWriteFrame()
{
    const std::size_t slotIndex =
        acquireNextCaptureWriteSlot();

    activeCaptureWriteSlot_ =
        slotIndex;

    CapturedFrame& slot =
        captureSlots_[
            slotIndex];

    slot.writing.store(
        true,
        std::memory_order_release);

    return &slot.frame;
}

void VideoEngine::submitWriteFrame()
{
    const std::size_t slotIndex =
        activeCaptureWriteSlot_;

    CapturedFrame& slot =
        captureSlots_[slotIndex];

    const std::uint64_t generation =
        captureGeneration_.fetch_add(
            1,
            std::memory_order_relaxed) + 1;

    slot.generation.store(
        generation,
        std::memory_order_release);

    slot.writing.store(
        false,
        std::memory_order_release);

    latestCaptureSlot_.store(
        static_cast<int>(slotIndex),
        std::memory_order_release);

    vectorscopeCondition_.notify_one();
    lumaInputCondition_.notify_one();
}

void VideoEngine::setFrame(const QImage& frame)
{
    currentFrame_ = frame;
    emit frameChanged(currentFrame_);
}


void VideoEngine::cancelWriteFrame()
{
    CapturedFrame& slot =
        captureSlots_[
            activeCaptureWriteSlot_];

    slot.writing.store(
        false,
        std::memory_order_release);
}

void VideoEngine::setSelectedLine(int line)
{
    selectedLine_.store(
        line,
        std::memory_order_release);
}
void VideoEngine::setWaveformPersistence(int persistence)
{
    waveformRenderer_.setPersistence(persistence);
}

void VideoEngine::setWaveformOutputSize(
    int width,
    int height)
{
    waveformOutputWidth_.store(
        std::max(width, 1),
        std::memory_order_release);

    waveformOutputHeight_.store(
        std::max(height, 1),
        std::memory_order_release);
}

void VideoEngine::setVectorscopeOutputSize(
    int width,
    int height)
{
    vectorscopeOutputWidth_.store(
        std::max(width, 1),
        std::memory_order_release);

    vectorscopeOutputHeight_.store(
        std::max(height, 1),
        std::memory_order_release);
}

double VideoEngine::traceBandwidthMHz() const
{
    return waveformRenderer_.traceBandwidthMHz();
}

void VideoEngine::setVideoOutputSize(
    int width,
    int height)
{
    videoOutputWidth_.store(
        std::max(width, 1),
        std::memory_order_release);

    videoOutputHeight_.store(
        std::max(height, 1),
        std::memory_order_release);
}

VideoEngine::~VideoEngine()
{
    {
        std::lock_guard<std::mutex> lock(
            lumaInputMutex_);

        lumaCoordinatorStop_ = true;
    }

    lumaInputCondition_.notify_one();

    if (lumaCoordinatorThread_.joinable())
    {
        lumaCoordinatorThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(
            lumaMutex_);

        lumaStop_ = true;
    }

    lumaCondition_.notify_all();
    lumaThreads_.clear();

    {
        std::lock_guard<std::mutex> lock(
            displayMutex_);

        displayStop_ = true;
    }

    displayCondition_.notify_one();

    displayThread_.quit();
    displayThread_.wait();

    {
        std::lock_guard<std::mutex> lock(
            waveformMutex_);

        waveformStop_ = true;
    }

    waveformCondition_.notify_one();

    waveformThread_.quit();
    waveformThread_.wait();

    {
        std::lock_guard<std::mutex> lock(
            vectorscopeMutex_);

        vectorscopeStop_ = true;
    }

    vectorscopeCondition_.notify_one();

    vectorscopeThread_.quit();
    vectorscopeThread_.wait();
}

void VideoEngine::vectorscopeWorkerLoop()
{
    for (;;)
    {
        std::size_t slotIndex = 0;
        std::uint64_t generation = 0;

        {
            std::unique_lock<std::mutex> lock(
                vectorscopeMutex_);

            vectorscopeCondition_.wait(
                lock,
                [this]()
                {
                    if (vectorscopeStop_)
                    {
                        return true;
                    }

                    const int latestSlot =
                        latestCaptureSlot_.load(
                            std::memory_order_acquire);

                    if (latestSlot < 0)
                    {
                        return false;
                    }

                    const auto& slot =
                        captureSlots_[
                            static_cast<std::size_t>(
                                latestSlot)];

                    const std::uint64_t latestGeneration =
                        slot.generation.load(
                            std::memory_order_acquire);

                    return
                        latestGeneration !=
                        vectorscopeLastGeneration_;
                });

            if (vectorscopeStop_)
            {
                return;
            }

            const int latestSlot =
                latestCaptureSlot_.load(
                    std::memory_order_acquire);

            slotIndex =
                static_cast<std::size_t>(
                    latestSlot);

            generation =
                captureSlots_[slotIndex]
                .generation.load(
                    std::memory_order_acquire);
            
            const std::uint64_t previousGeneration =
                vectorscopeLastGeneration_;

            vectorscopeLastGeneration_ =
                generation;

            if (previousGeneration != 0 &&
                generation > previousGeneration + 1)
            {
                qDebug()
                    << "Vectorscope skipped"
                    << (generation - previousGeneration - 1)
                    << "frame(s)";
            }
        }
        const int outputWidth =
            vectorscopeOutputWidth_.load(
                std::memory_order_acquire);

        const int outputHeight =
            vectorscopeOutputHeight_.load(
                std::memory_order_acquire);

        vectorscopeAnalyzer_.setOutputSize(
            outputWidth,
            outputHeight);

        const int selectedLine =
            selectedLine_.load(
                std::memory_order_acquire);

        vectorscopeAnalyzer_.setSelectedLine(
            selectedLine);

        vectorscopeAnalyzer_.analyze(
            captureSlots_[slotIndex].frame);

        const bool stillValid =
            isCaptureSlotValid(
                slotIndex,
                generation);

        if (!stillValid)
        {
            qDebug()
                << "Vectorscope missed capture deadline:"
                << generation;
        }

        emit vectorscopeChanged(
            vectorscopeAnalyzer_.image());
    }
}

void VideoEngine::lumaWorkerLoop(
    std::size_t workerIndex)
{
    std::uint64_t lastGeneration = 0;

    for (;;)
    {
        const Yuv444Frame* frame = nullptr;
        int firstLine = 0;
        int lastLine = 0;

        {
            std::unique_lock<std::mutex> lock(
                lumaMutex_);

            lumaCondition_.wait(
                lock,
                [this, &lastGeneration]()
                {
                    return
                        lumaStop_ ||
                        lumaGeneration_ != lastGeneration;
                });

            if (lumaStop_)
            {
                return;
            }

            lastGeneration =
                lumaGeneration_;

            frame =
                lumaFrame_;

            const int workerCount =
                static_cast<int>(
                    lumaWorkers_.size());

            const int linesPerWorker =
                (frame->height + workerCount - 1) /
                workerCount;

            firstLine =
                static_cast<int>(workerIndex) *
                linesPerWorker;

            lastLine =
                std::min(
                    firstLine + linesPerWorker,
                    frame->height);
        }

        if (firstLine < lastLine)
        {
            LumaWorker& worker =
                lumaWorkers_[workerIndex];

            for (int line = firstLine;
                line < lastLine;
                ++line)
            {
                const std::size_t sourceOffset =
                    static_cast<std::size_t>(line) *
                    static_cast<std::size_t>(
                        frame->width);

                for (int x = 0;
                    x < frame->width;
                    ++x)
                {
                    worker.sourceLine[
                        static_cast<std::size_t>(x)] =
                        static_cast<float>(
                            frame->y[
                                sourceOffset +
                                    static_cast<std::size_t>(x)]);
                }

                worker.reconstructor.resample(
                    worker.sourceLine,
                    worker.reconstructedLine);

                const std::size_t destinationOffset =
                    static_cast<std::size_t>(line) *
                    kReconstructedLumaWidth;

                for (std::size_t x = 0;
                    x < kReconstructedLumaWidth;
                    ++x)
                {
                    lumaOutputFrame_->y[
                        destinationOffset + x] =
                        static_cast<std::uint16_t>(
                            std::clamp(
                                worker.reconstructedLine[x],
                                0.0f,
                                65535.0f));
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(
                lumaMutex_);

            --lumaWorkersRemaining_;

            if (lumaWorkersRemaining_ == 0)
            {
                lumaDoneCondition_.notify_one();
            }
        }
    }
}

std::size_t VideoEngine::acquireNextCaptureWriteSlot()
{
    const std::size_t slot =
        nextCaptureWriteSlot_;

    nextCaptureWriteSlot_ =
        (nextCaptureWriteSlot_ + 1) %
        kCaptureSlotCount;

    return slot;
}

bool VideoEngine::isCaptureSlotValid(
    std::size_t slotIndex,
    std::uint64_t generation) const
{
    const CapturedFrame& slot =
        captureSlots_[slotIndex];

    if (slot.writing.load(
        std::memory_order_acquire))
    {
        return false;
    }

    return
        slot.generation.load(
            std::memory_order_acquire) ==
        generation;
}

std::size_t VideoEngine::acquireNextReconstructedWriteSlot()
{
    const std::size_t slot =
        nextReconstructedWriteSlot_;

    nextReconstructedWriteSlot_ =
        (nextReconstructedWriteSlot_ + 1) %
        kReconstructedSlotCount;

    return slot;
}

bool VideoEngine::isReconstructedSlotValid(
    std::size_t slotIndex,
    std::uint64_t generation) const
{
    const ReconstructedFrameSlot& slot =
        reconstructedSlots_[slotIndex];

    if (slot.writing.load(
        std::memory_order_acquire))
    {
        return false;
    }

    return
        slot.generation.load(
            std::memory_order_acquire) ==
        generation;
}

int VideoEngine::findCaptureSlotByGeneration(
    std::uint64_t generation) const
{
    for (std::size_t slotIndex = 0;
        slotIndex < captureSlots_.size();
        ++slotIndex)
    {
        const CapturedFrame& slot =
            captureSlots_[slotIndex];

        if (slot.writing.load(
            std::memory_order_acquire))
        {
            continue;
        }

        if (slot.generation.load(
            std::memory_order_acquire) ==
            generation)
        {
            return static_cast<int>(
                slotIndex);
        }
    }

    return -1;
}

void VideoEngine::waveformWorkerLoop()
{
    for (;;)
    {
        std::uint64_t generation = 0;
        int reconstructedSlotIndex = -1;
        int captureSlotIndex = -1;

        {
            std::unique_lock<std::mutex> lock(
                waveformMutex_);

            waveformCondition_.wait(
                lock,
                [this]()
                {
                    if (waveformStop_)
                    {
                        return true;
                    }

                    const int latestSlot =
                        latestReconstructedSlot_.load(
                            std::memory_order_acquire);

                    if (latestSlot < 0)
                    {
                        return false;
                    }

                    const auto& slot =
                        reconstructedSlots_[
                            static_cast<std::size_t>(
                                latestSlot)];

                    const std::uint64_t latestGeneration =
                        slot.generation.load(
                            std::memory_order_acquire);

                    return
                        latestGeneration !=
                        waveformLastGeneration_;
                });

            if (waveformStop_)
            {
                return;
            }

            reconstructedSlotIndex =
                latestReconstructedSlot_.load(
                    std::memory_order_acquire);

            generation =
                reconstructedSlots_[
                    static_cast<std::size_t>(
                        reconstructedSlotIndex)]
                .generation.load(
                    std::memory_order_acquire);

            const std::uint64_t previousGeneration =
                waveformLastGeneration_;

            waveformLastGeneration_ =
                generation;

            if (previousGeneration != 0 &&
                generation > previousGeneration + 1)
            {
                qDebug()
                    << "Waveform skipped"
                    << (generation - previousGeneration - 1)
                    << "frame(s)";
            };
        }

        captureSlotIndex =
            findCaptureSlotByGeneration(
                generation);

        if (captureSlotIndex < 0)
        {
            continue;
        }

        const auto& captureSlot =
            captureSlots_[
                static_cast<std::size_t>(
                    captureSlotIndex)];

        const auto& reconstructedSlot =
            reconstructedSlots_[
                static_cast<std::size_t>(
                    reconstructedSlotIndex)];

        const int outputWidth =
            waveformOutputWidth_.load(
                std::memory_order_acquire);

        const int outputHeight =
            waveformOutputHeight_.load(
                std::memory_order_acquire);

        waveformRenderer_.setOutputSize(
            outputWidth,
            outputHeight);

        const int selectedLine =
            selectedLine_.load(
                std::memory_order_acquire);

        waveformRenderer_.setSelectedLine(
            selectedLine);

        waveformRenderer_.analyze(
            captureSlot.frame,
            reconstructedSlot.frame);

        const bool captureValid =
            isCaptureSlotValid(
                static_cast<std::size_t>(
                    captureSlotIndex),
                generation);

        const bool reconstructedValid =
            isReconstructedSlotValid(
                static_cast<std::size_t>(
                    reconstructedSlotIndex),
                generation);

        if (!captureValid ||
            !reconstructedValid)
        {
            qDebug()
                << "Waveform lagging:"
                << "generation =" << generation;
        }

        emit waveformChanged(
            waveformRenderer_.image());
    }
}

void VideoEngine::displayWorkerLoop()
{
    for (;;)
    {
        std::uint64_t generation = 0;
        int reconstructedSlotIndex = -1;
        int captureSlotIndex = -1;

        {
            std::unique_lock<std::mutex> lock(
                displayMutex_);

            displayCondition_.wait(
                lock,
                [this]()
                {
                    if (displayStop_)
                    {
                        return true;
                    }

                    const int latestSlot =
                        latestReconstructedSlot_.load(
                            std::memory_order_acquire);

                    if (latestSlot < 0)
                    {
                        return false;
                    }

                    const auto& slot =
                        reconstructedSlots_[
                            static_cast<std::size_t>(
                                latestSlot)];

                    const std::uint64_t latestGeneration =
                        slot.generation.load(
                            std::memory_order_acquire);

                    return
                        latestGeneration !=
                        displayLastGeneration_;
                });

            if (displayStop_)
            {
                return;
            }

            reconstructedSlotIndex =
                latestReconstructedSlot_.load(
                    std::memory_order_acquire);

            generation =
                reconstructedSlots_[
                    static_cast<std::size_t>(
                        reconstructedSlotIndex)]
                .generation.load(
                    std::memory_order_acquire);

            const std::uint64_t previousGeneration =
                displayLastGeneration_;

            displayLastGeneration_ =
                generation;

            if (previousGeneration != 0 &&
                generation > previousGeneration + 1)
            {
                qDebug()
                    << "Display skipped"
                    << (generation - previousGeneration - 1)
                    << "frame(s)";
            }
        }

        captureSlotIndex =
            findCaptureSlotByGeneration(
                generation);

        if (captureSlotIndex < 0)
        {
            continue;
        }

        const auto& captureSlot =
            captureSlots_[
                static_cast<std::size_t>(
                    captureSlotIndex)];

        const auto& reconstructedSlot =
            reconstructedSlots_[
                static_cast<std::size_t>(
                    reconstructedSlotIndex)];

        const int outputWidth =
            videoOutputWidth_.load(
                std::memory_order_acquire);

        const int outputHeight =
            videoOutputHeight_.load(
                std::memory_order_acquire);

        const int selectedLine =
            selectedLine_.load(
                std::memory_order_acquire);

        displayConverter_.setHighlightedLine(
            selectedLine);

        const QImage displayImage =
            displayConverter_.convert(
                captureSlot.frame,
                reconstructedSlot.frame,
                outputWidth,
                outputHeight);

        const bool captureValid =
            isCaptureSlotValid(
                static_cast<std::size_t>(
                    captureSlotIndex),
                generation);

        const bool reconstructedValid =
            isReconstructedSlotValid(
                static_cast<std::size_t>(
                    reconstructedSlotIndex),
                generation);

        if (!captureValid ||
            !reconstructedValid)
        {
            qDebug()
                << "Display lagging:"
                << "generation =" << generation;
        }

        QMetaObject::invokeMethod(
            this,
            [this, displayImage]()
            {
                setFrame(displayImage);
            },
            Qt::QueuedConnection);
    }
}

void VideoEngine::lumaCoordinatorLoop()
{
    for (;;)
    {
        std::size_t slotIndex = 0;
        std::uint64_t generation = 0;

        {
            std::unique_lock<std::mutex> lock(
                lumaInputMutex_);

            lumaInputCondition_.wait(
                lock,
                [this]()
                {
                    if (lumaCoordinatorStop_)
                    {
                        return true;
                    }

                    const int latestSlot =
                        latestCaptureSlot_.load(
                            std::memory_order_acquire);

                    if (latestSlot < 0)
                    {
                        return false;
                    }

                    const std::uint64_t latestGeneration =
                        captureSlots_[
                            static_cast<std::size_t>(
                                latestSlot)]
                        .generation.load(
                            std::memory_order_acquire);

                    return
                        latestGeneration !=
                        lumaLastCaptureGeneration_;
                });

            if (lumaCoordinatorStop_)
            {
                return;
            }

            const int latestSlot =
                latestCaptureSlot_.load(
                    std::memory_order_acquire);

            slotIndex =
                static_cast<std::size_t>(
                    latestSlot);

            generation =
                captureSlots_[slotIndex]
                .generation.load(
                    std::memory_order_acquire);

            const std::uint64_t previousGeneration =
                lumaLastCaptureGeneration_;

            lumaLastCaptureGeneration_ =
                generation;

            if (previousGeneration != 0 &&
                generation > previousGeneration + 1)
            {
                qDebug()
                    << "Luma skipped"
                    << (generation - previousGeneration - 1)
                    << "frame(s)";
            }
        }

        reconstructLuma(
            captureSlots_[slotIndex].frame,
            generation);

        const bool stillValid =
            isCaptureSlotValid(
                slotIndex,
                generation);

        if (!stillValid)
        {
            qDebug()
                << "Luma lagging:"
                << "generation =" << generation;
        }

    }
}