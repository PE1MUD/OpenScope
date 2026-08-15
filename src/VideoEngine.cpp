#include <QMetaObject>
#include "VideoEngine.h"
#include "VectorscopeSettings.h"
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <thread>
#include <QTimer>

VideoEngine::VideoEngine(QObject* parent)
    : QObject(parent)
{
    vectorscopeAnalyzer_.setScale(
        VectorscopeSettings::scale);

    displayConverter_.setImplementation(
        DisplayConversionImplementation::Avx2);

    for (CapturedFrameSlot& slot : captureSlots_)
    {
        slot.frame.resize(
            kCaptureWidth,
            kCaptureHeight);
    }

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

    displayPresenterTimer_ =
        new QTimer(this);

    displayPresenterTimer_->setTimerType(
        Qt::PreciseTimer);

    displayPresenterTimer_->setInterval(
        20);

    connect(
        displayPresenterTimer_,
        &QTimer::timeout,
        this,
        [this]()
        {
            QImage imageToPresent;

            {
                std::lock_guard<std::mutex> lock(
                    displayPresenterMutex_);

                if (!displayPairReady_.load(
                    std::memory_order_acquire))
                {
                    return;
                }

                const int fieldIndex =
                    displayFieldIndex_.load(
                        std::memory_order_acquire);

                if (fieldIndex == 0)
                {
                    imageToPresent =
                        pendingDisplayFirst_;

                    displayFieldIndex_.store(
                        1,
                        std::memory_order_release);
                }
                else
                {
                    imageToPresent =
                        pendingDisplaySecond_;

                    displayFieldIndex_.store(
                        0,
                        std::memory_order_release);

                    displayPairReady_.store(
                        false,
                        std::memory_order_release);
                }
            }

            emit frameChanged(
                imageToPresent);
        });

    displayPresenterTimer_->start();

}


VideoEngine::~VideoEngine()
{
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


Yuv444Frame* VideoEngine::tryAcquireWriteFrame()
{
    const std::size_t slotIndex =
        acquireNextCaptureWriteSlot();

    activeCaptureWriteSlot_ =
        slotIndex;

    CapturedFrameSlot& slot =
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

    CapturedFrameSlot& slot =
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

    displayCondition_.notify_one();
    vectorscopeCondition_.notify_one();
    waveformCondition_.notify_one();
}


void VideoEngine::cancelWriteFrame()
{
    CapturedFrameSlot& slot =
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
    waveformPersistence_.store(
        std::clamp(persistence, 0, 255),
        std::memory_order_release);
}


void VideoEngine::setWaveformOutputSize(
    int width,
    int height)
{
    waveformOutputWidth_.store(
        std::max(width, kMinimumOutputSize),
        std::memory_order_release);

    waveformOutputHeight_.store(
        std::max(height, kMinimumOutputSize),
        std::memory_order_release);
}


void VideoEngine::setVectorscopeOutputSize(
    int width,
    int height)
{
    vectorscopeOutputWidth_.store(
        std::max(width, kMinimumOutputSize),
        std::memory_order_release);

    vectorscopeOutputHeight_.store(
        std::max(height, kMinimumOutputSize),
        std::memory_order_release);
}


void VideoEngine::setVideoOutputSize(
    int width,
    int height)
{
    videoOutputWidth_.store(
        std::max(width, kMinimumOutputSize),
        std::memory_order_release);

    videoOutputHeight_.store(
        std::max(height, kMinimumOutputSize),
        std::memory_order_release);
}


void VideoEngine::setWaveformZoomed(
    bool zoomed)
{
    waveformZoomed_.store(
        zoomed,
        std::memory_order_release);

    waveformRenderer_.setZoomed(
        zoomed);
}


void VideoEngine::setWaveformScrollPosition(
    double position)
{
    waveformScrollPosition_.store(
        position,
        std::memory_order_release);

    waveformRenderer_.setScrollPosition(
        position);
}

void VideoEngine::displayWorkerLoop()
{
    for (;;)
    {
        std::uint64_t generation = 0;

        int captureSlotIndex =
            kInvalidSlotIndex;

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
                        latestCaptureSlot_.load(
                            std::memory_order_acquire);

                    if (latestSlot == kInvalidSlotIndex)
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
                        displayLastGeneration_;
                });

            if (displayStop_)
            {
                return;
            }

            captureSlotIndex =
                latestCaptureSlot_.load(
                    std::memory_order_acquire);

            generation =
                captureSlots_[
                    static_cast<std::size_t>(
                        captureSlotIndex)]
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
                    << (generation -
                        previousGeneration -
                        1)
                    << "frame(s)";
            }
        }

        if (captureSlotIndex == kInvalidSlotIndex)
        {
            continue;
        }

        const auto& captureSlot =
            captureSlots_[
                static_cast<std::size_t>(
                    captureSlotIndex)];

        const int outputWidth =
            videoOutputWidth_.load(
                std::memory_order_acquire);

        const int outputHeight =
            videoOutputHeight_.load(
                std::memory_order_acquire);

        const int selectedLine =
            selectedLine_.load(
                std::memory_order_acquire);

        const bool waveformZoomed =
            waveformZoomed_.load(
                std::memory_order_acquire);

        const double waveformScrollPosition =
            waveformScrollPosition_.load(
                std::memory_order_acquire);

        if (waveformZoomed)
        {
            const int visibleWidth =
                captureSlot.frame.width / 10;

            const int maximumStart =
                captureSlot.frame.width -
                visibleWidth;

            const int startX =
                static_cast<int>(
                    waveformScrollPosition *
                    static_cast<double>(
                        maximumStart));

            const int endX =
                startX +
                visibleWidth;

            displayConverter_.setHighlightedRange(
                startX,
                endX);
        }
        else
        {
            displayConverter_.setHighlightedRange(
                0,
                -1);
        }

        displayConverter_.setHighlightedLine(
            selectedLine);

        displayConverter_.setHighlightedLine(
            selectedLine);

        QElapsedTimer totalDisplayTimer;
        totalDisplayTimer.start();

        QElapsedTimer deinterlaceTimer;
        deinterlaceTimer.start();

        videoDeinterlacer_.deinterlace(
            captureSlot.frame.y.data(),
            captureSlot.frame.width,
            captureSlot.frame.height,
            progressiveLuma_);

        performanceStats_.deinterlace.update(
            static_cast<std::uint64_t>(
                deinterlaceTimer.nsecsElapsed() /
                1000));

        QElapsedTimer fieldTimer;
        fieldTimer.start();

        DisplayPerformance displayPerformance;

        const QImage firstDisplayImage =
            displayConverter_.convert(
                captureSlot.frame,
                progressiveLuma_.first.y.data(),
                outputWidth,
                outputHeight,
                displayPerformance);

        performanceStats_.displayFirst.update(
            static_cast<std::uint64_t>(
                fieldTimer.nsecsElapsed() /
                1000));

        fieldTimer.restart();

        DisplayPerformance secondDisplayPerformance;

        const QImage secondDisplayImage =
            displayConverter_.convert(
                captureSlot.frame,
                progressiveLuma_.second.y.data(),
                outputWidth,
                outputHeight,
                secondDisplayPerformance);

        performanceStats_.displaySecond.update(
            static_cast<std::uint64_t>(
                fieldTimer.nsecsElapsed() /
                1000));

        {
            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            pendingDisplayFirst_ =
                firstDisplayImage;

            pendingDisplaySecond_ =
                secondDisplayImage;

            displayFieldIndex_.store(
                0,
                std::memory_order_release);

            displayPairReady_.store(
                true,
                std::memory_order_release);
        }

        performanceStats_.displayAllocation.update(
            displayPerformance.allocationUs +
            secondDisplayPerformance.allocationUs);

        performanceStats_.displaySetup.update(
            displayPerformance.setupUs +
            secondDisplayPerformance.setupUs);

        performanceStats_.displayCompose.update(
            displayPerformance.composeUs +
            secondDisplayPerformance.composeUs);

        performanceStats_.displayInterpolation.update(
            displayPerformance.interpolationUs +
            secondDisplayPerformance.interpolationUs);

        performanceStats_.displayColorConversion.update(
            displayPerformance.colorConversionUs +
            secondDisplayPerformance.colorConversionUs);

        performanceStats_.displayOutput.update(
            displayPerformance.outputUs +
            secondDisplayPerformance.outputUs);

        const std::uint64_t displayTotalUs =
            static_cast<std::uint64_t>(
                totalDisplayTimer.nsecsElapsed() /
                1000);

        performanceStats_.displayTotal.update(
            displayTotalUs);

        constexpr std::uint64_t kRxFrameBudgetUs =
            40000u;

        const std::uint64_t rxMarginUs =
            displayTotalUs < kRxFrameBudgetUs
            ? kRxFrameBudgetUs - displayTotalUs
            : 0u;

        performanceStats_.rxMargin.update(
            rxMarginUs);

        if (displayTotalUs > kRxFrameBudgetUs)
        {
            performanceStats_.displayDeadlineMisses.fetch_add(
                1u,
                std::memory_order_relaxed);
        }

        const bool captureValid =
            isCaptureSlotValid(
                static_cast<std::size_t>(
                    captureSlotIndex),
                generation);

        if (!captureValid)
        {
            qDebug()
                << "Display lagging:"
                << "generation ="
                << generation;
        }
    }
}

void VideoEngine::waveformWorkerLoop()
{
    for (;;)
    {
        std::uint64_t generation = 0;

        int captureSlotIndex =
            kInvalidSlotIndex;

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
                        latestCaptureSlot_.load(
                            std::memory_order_acquire);

                    if (latestSlot == kInvalidSlotIndex)
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
                        waveformLastGeneration_;
                });

            if (waveformStop_)
            {
                return;
            }

            captureSlotIndex =
                latestCaptureSlot_.load(
                    std::memory_order_acquire);

            generation =
                captureSlots_[
                    static_cast<std::size_t>(
                        captureSlotIndex)]
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
                    << (generation -
                        previousGeneration -
                        1)
                    << "frame(s)";
            }
        }

        if (captureSlotIndex == kInvalidSlotIndex)
        {
            continue;
        }

        const auto& captureSlot =
            captureSlots_[
                static_cast<std::size_t>(
                    captureSlotIndex)];

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

        const int waveformPersistence =
            waveformPersistence_.load(
                std::memory_order_acquire);

        waveformRenderer_.setPersistence(
            waveformPersistence);

        QElapsedTimer timer;
        timer.start();

        //
        // Tijdelijk nog alleen capture frame.
        // De waveform-specifieke Y reconstructie
        // voegen we hierna binnen dit pad toe.
        //
        waveformRenderer_.analyze(
            captureSlot.frame);

        performanceStats_.waveform.update(
            static_cast<std::uint64_t>(
                timer.nsecsElapsed() /
                1000));

        const bool captureValid =
            isCaptureSlotValid(
                static_cast<std::size_t>(
                    captureSlotIndex),
                generation);

        if (!captureValid)
        {
            qDebug()
                << "Waveform lagging:"
                << "generation ="
                << generation;
        }

        emit waveformChanged(
            waveformRenderer_.image());
    }
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

                    if (latestSlot == kInvalidSlotIndex)
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

        QElapsedTimer timer;
        timer.start();

        vectorscopeAnalyzer_.analyze(
            captureSlots_[slotIndex].frame);

        performanceStats_.vectorscope.update(
            static_cast<std::uint64_t>(
                timer.nsecsElapsed() / 1000));

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



int VideoEngine::findCaptureSlotByGeneration(
    std::uint64_t generation) const
{
    for (std::size_t slotIndex = 0;
        slotIndex < captureSlots_.size();
        ++slotIndex)
    {
        const CapturedFrameSlot& slot =
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

    return kInvalidSlotIndex;
}


bool VideoEngine::isCaptureSlotValid(
    std::size_t slotIndex,
    std::uint64_t generation) const
{
    const CapturedFrameSlot& slot =
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




std::size_t VideoEngine::acquireNextCaptureWriteSlot()
{
    const std::size_t slot =
        nextCaptureWriteSlot_;

    nextCaptureWriteSlot_ =
        (nextCaptureWriteSlot_ + 1) %
        kFrameSlotCount;

    return slot;
}

void VideoEngine::setWaveformChromaFillIntensity(
    int intensity)
{
    waveformRenderer_.setChromaFillIntensity(
        intensity);
}

void VideoEngine::setWaveformColor(bool enabled)
{
    waveformRenderer_.setColor(enabled);
}

void VideoEngine::setDisplayGamma(double gamma)
{
    displayConverter_.setGamma(gamma);
}

PerformanceSnapshot VideoEngine::performanceSnapshot() const
{
    return performanceStats_.snapshot();
}