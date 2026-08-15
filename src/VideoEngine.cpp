#include <QMetaObject>
#include "VideoEngine.h"
#include "VectorscopeSettings.h"
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <thread>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <avrt.h>

#pragma comment(lib, "Avrt.lib")

VideoEngine::VideoEngine(QObject* parent)
    : QObject(parent)
{
    vectorscopeAnalyzer_.setScale(
        VectorscopeSettings::scale);

    for (DisplayConverter& converter :
        displayConverters_)
    {
        converter.setImplementation(
            DisplayConversionImplementation::Avx2);
    }

    for (CapturedFrameSlot& slot : captureSlots_)
    {
        slot.frame.resize(
            kCaptureWidth,
            kCaptureHeight);
    }

    for (std::size_t workerIndex = 0;
        workerIndex < displayPhaseWorkers_.size();
        ++workerIndex)
    {
        displayPhaseWorkers_[workerIndex] =
            std::jthread(
                [this, workerIndex]()
                {
                    displayPhaseWorkerLoop(
                        workerIndex);
                });
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

    displayPresenterThread_ =
        std::thread(
            [this]()
            {
                displayPresenterLoop();
            });

}


VideoEngine::~VideoEngine()
{
    {
        std::lock_guard<std::mutex> lock(
            displayPresenterMutex_);

        displayPresenterStop_ = true;
    }

    displayPresenterCondition_.notify_one();

    if (displayPresenterThread_.joinable())
    {
        displayPresenterThread_.join();
    }

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
            displayPhaseMutex_);

        displayPhaseStop_ = true;
    }

    displayPhaseCondition_.notify_all();

    for (std::jthread& worker :
        displayPhaseWorkers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

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
    const auto captureTickTime =
        std::chrono::steady_clock::now();

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

    {
        std::lock_guard<std::mutex> lock(
            displayPresenterMutex_);

        latestCaptureTickGeneration_ =
            generation;

        latestCaptureTickTime_ =
            captureTickTime;
    }

    displayPresenterCondition_.notify_one();

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


void VideoEngine::setVideoHighlightEnabled(
    bool enabled)
{
    videoHighlightEnabled_.store(
        enabled,
        std::memory_order_release);
}


void VideoEngine::setNoiseReductionEnabled(
    bool enabled)
{
    noiseReductionEnabled_.store(
        enabled,
        std::memory_order_release);
}


void VideoEngine::setNoiseReductionIntensity(
    int intensity)
{
    noiseReductionIntensity_.store(
        std::clamp(
            intensity,
            0,
            100),
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
        (std::max)(width, kMinimumOutputSize),
        std::memory_order_release);

    waveformOutputHeight_.store(
        (std::max)(height, kMinimumOutputSize),
        std::memory_order_release);
}


void VideoEngine::setVectorscopeOutputSize(
    int width,
    int height)
{
    vectorscopeOutputWidth_.store(
        (std::max)(width, kMinimumOutputSize),
        std::memory_order_release);

    vectorscopeOutputHeight_.store(
        (std::max)(height, kMinimumOutputSize),
        std::memory_order_release);
}


void VideoEngine::setVideoOutputSize(
    int width,
    int height)
{
    videoOutputWidth_.store(
        (std::max)(width, kMinimumOutputSize),
        std::memory_order_release);

    videoOutputHeight_.store(
        (std::max)(height, kMinimumOutputSize),
        std::memory_order_release);
}


void VideoEngine::setWaveformZoomed(
    bool zoomed)
{
    setWaveformZoomFactor(
        zoomed
        ? 10
        : 1);
}

void VideoEngine::setWaveformZoomFactor(
    int factor)
{
    if (factor != 1 &&
        factor != 5 &&
        factor != 10)
    {
        factor = 1;
    }

    waveformZoomFactor_.store(
        factor,
        std::memory_order_release);

    waveformRenderer_.setZoomFactor(
        factor);
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

void VideoEngine::displayPhaseWorkerLoop(
    std::size_t workerIndex)
{
    std::uint64_t lastPhaseGeneration = 0;

    for (;;)
    {
        DisplayPhase phase =
            DisplayPhase::Idle;

        const Yuv444Frame* frame =
            nullptr;

        const std::uint16_t* luma =
            nullptr;

        QRgb* outputPixels =
            nullptr;

        int outputStridePixels = 0;
        int outputWidth = 0;
        int outputHeight = 0;

        {
            std::unique_lock<std::mutex> lock(
                displayPhaseMutex_);

            displayPhaseCondition_.wait(
                lock,
                [this, lastPhaseGeneration]()
                {
                    return
                        displayPhaseStop_ ||
                        displayPhaseGeneration_ !=
                        lastPhaseGeneration;
                });

            if (displayPhaseStop_)
            {
                return;
            }

            lastPhaseGeneration =
                displayPhaseGeneration_;

            phase =
                displayPhase_;

            frame =
                displayPhaseFrame_;

            luma =
                displayPhaseLuma_;

            outputPixels =
                displayPhaseOutputPixels_;

            outputStridePixels =
                displayPhaseOutputStridePixels_;

            outputWidth =
                displayPhaseOutputWidth_;

            outputHeight =
                displayPhaseOutputHeight_;
        }

        if (phase ==
            DisplayPhase::Deinterlace)
        {
            QElapsedTimer workerTimer;
            workerTimer.start();

            const int height =
                frame != nullptr
                ? frame->height
                : 0;

            for (;;)
            {
                const int firstLine =
                    displayDeinterlaceNextLine_.fetch_add(
                        kDeinterlaceChunkLines,
                        std::memory_order_relaxed);

                if (firstLine >= height)
                {
                    break;
                }

                const int lastLine =
                    std::min(
                        firstLine +
                        kDeinterlaceChunkLines,
                        height);

                videoDeinterlacer_.processRange(
                    firstLine,
                    lastLine);
            }

            displayDeinterlaceWorkerUs_[
                workerIndex].store(
                    static_cast<std::uint64_t>(
                        workerTimer.nsecsElapsed() /
                        1000),
                    std::memory_order_relaxed);
        }
        else if (
            phase ==
            DisplayPhase::ConvertFirst ||
            phase ==
            DisplayPhase::ConvertSecond)
        {
            const int firstOutputY =
                static_cast<int>(
                    workerIndex) *
                outputHeight /
                static_cast<int>(
                    displayPhaseWorkers_.size());

            const int lastOutputY =
                static_cast<int>(
                    workerIndex + 1) *
                outputHeight /
                static_cast<int>(
                    displayPhaseWorkers_.size());

            displayConverters_[workerIndex].
                convertRange(
                    *frame,
                    luma,
                    outputPixels,
                    outputStridePixels,
                    outputWidth,
                    outputHeight,
                    firstOutputY,
                    lastOutputY,
                    displayPhasePerformance_[
                        workerIndex]);
        }

        {
            std::lock_guard<std::mutex> lock(
                displayPhaseMutex_);

            if (displayPhaseWorkersRemaining_ > 0)
            {
                --displayPhaseWorkersRemaining_;
            }
        }

        displayPhaseDoneCondition_.
            notify_one();
    }
}

void VideoEngine::runDisplayPhase(
    DisplayPhase phase)
{
    {
        std::lock_guard<std::mutex> lock(
            displayPhaseMutex_);

        displayPhase_ =
            phase;

        if (phase ==
            DisplayPhase::Deinterlace)
        {
            displayDeinterlaceNextLine_.store(
                0,
                std::memory_order_relaxed);

            for (auto& workerUs :
                displayDeinterlaceWorkerUs_)
            {
                workerUs.store(
                    0,
                    std::memory_order_relaxed);
            }
        }

        displayPhaseWorkersRemaining_ =
            displayPhaseWorkers_.size();

        for (DisplayPerformance& performance :
            displayPhasePerformance_)
        {
            performance = {};
        }

        ++displayPhaseGeneration_;
    }

    displayPhaseCondition_.notify_all();

    std::unique_lock<std::mutex> lock(
        displayPhaseMutex_);

    displayPhaseDoneCondition_.wait(
        lock,
        [this]()
        {
            return
                displayPhaseWorkersRemaining_ ==
                0 ||
                displayPhaseStop_;
        });
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

                    if (latestSlot ==
                        kInvalidSlotIndex)
                    {
                        return false;
                    }

                    const auto& slot =
                        captureSlots_[
                            static_cast<std::size_t>(
                                latestSlot)];

                    const std::uint64_t
                        latestGeneration =
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

            const std::uint64_t
                previousGeneration =
                displayLastGeneration_;

            displayLastGeneration_ =
                generation;

            if (previousGeneration != 0 &&
                generation >
                previousGeneration + 1)
            {
                qDebug()
                    << "Display skipped"
                    << (generation -
                        previousGeneration -
                        1)
                    << "frame(s)";
            }
        }

        if (captureSlotIndex ==
            kInvalidSlotIndex)
        {
            continue;
        }

        const auto& captureSlot =
            captureSlots_[
                static_cast<std::size_t>(
                    captureSlotIndex)];

        QElapsedTimer totalDisplayTimer;
        totalDisplayTimer.start();

        const bool noiseReductionEnabled =
            noiseReductionEnabled_.load(
                std::memory_order_acquire);

        const int noiseReductionIntensity =
            noiseReductionIntensity_.load(
                std::memory_order_acquire);

        const Yuv444Frame* displayFrame =
            &captureSlot.frame;

        QElapsedTimer noiseReductionTimer;
        noiseReductionTimer.start();

        if (noiseReductionEnabled)
        {
            noiseReducer_.process(
                captureSlot.frame,
                noiseReducedFrame_,
                noiseReductionIntensity);

            displayFrame =
                &noiseReducedFrame_;
        }

        const std::uint64_t noiseReductionUs =
            noiseReductionEnabled
            ? static_cast<std::uint64_t>(
                noiseReductionTimer.nsecsElapsed() /
                1000)
            : 0u;

        performanceStats_.noiseReduction.update(
            noiseReductionUs);

        const int outputWidth =
            videoOutputWidth_.load(
                std::memory_order_acquire);

        const int outputHeight =
            videoOutputHeight_.load(
                std::memory_order_acquire);

        const int selectedLine =
            selectedLine_.load(
                std::memory_order_acquire);

        const bool videoHighlightEnabled =
            videoHighlightEnabled_.load(
                std::memory_order_acquire);

        const int waveformZoomFactor =
            waveformZoomFactor_.load(
                std::memory_order_acquire);

        const double waveformScrollPosition =
            waveformScrollPosition_.load(
                std::memory_order_acquire);

        int highlightStartX = 0;
        int highlightEndX = -1;

        if (waveformZoomFactor > 1)
        {
            const int visibleWidth =
                displayFrame->width /
                waveformZoomFactor;

            const int maximumStart =
                displayFrame->width -
                visibleWidth;

            highlightStartX =
                static_cast<int>(
                    waveformScrollPosition *
                    static_cast<double>(
                        maximumStart));

            highlightEndX =
                highlightStartX +
                visibleWidth;
        }

        for (DisplayConverter& converter :
            displayConverters_)
        {
            if (videoHighlightEnabled)
            {
                converter.setHighlightedRange(
                    highlightStartX,
                    highlightEndX);

                converter.setHighlightedLine(
                    selectedLine);
            }
            else
            {
                converter.setHighlightedRange(
                    0,
                    -1);

                converter.setHighlightedLine(
                    -1);
            }
        }

        QElapsedTimer phaseTimer;
        phaseTimer.start();

        if (!videoDeinterlacer_.beginFrame(
            displayFrame->y.data(),
            displayFrame->width,
            displayFrame->height,
            progressiveLuma_))
        {
            continue;
        }

        displayPhaseFrame_ =
            displayFrame;

        displayPhaseLuma_ =
            nullptr;

        displayPhaseOutputPixels_ =
            nullptr;

        displayPhaseOutputStridePixels_ = 0;
        displayPhaseOutputWidth_ = 0;
        displayPhaseOutputHeight_ = 0;

        runDisplayPhase(
            DisplayPhase::Deinterlace);

        videoDeinterlacer_.endFrame();

        performanceStats_.deinterlace.update(
            static_cast<std::uint64_t>(
                phaseTimer.nsecsElapsed() /
                1000));

        performanceStats_.deinterlaceWorker0.update(
            displayDeinterlaceWorkerUs_[0].load(
                std::memory_order_relaxed));

        performanceStats_.deinterlaceWorker1.update(
            displayDeinterlaceWorkerUs_[1].load(
                std::memory_order_relaxed));

        QImage firstDisplayImage(
            outputWidth,
            outputHeight,
            QImage::Format_RGB32);

        firstDisplayImage.detach();

        displayPhaseFrame_ =
            displayFrame;

        displayPhaseLuma_ =
            progressiveLuma_.first.y.data();

        displayPhaseOutputPixels_ =
            reinterpret_cast<QRgb*>(
                firstDisplayImage.bits());

        displayPhaseOutputStridePixels_ =
            firstDisplayImage.bytesPerLine() /
            static_cast<int>(
                sizeof(QRgb));

        displayPhaseOutputWidth_ =
            outputWidth;

        displayPhaseOutputHeight_ =
            outputHeight;

        phaseTimer.restart();

        runDisplayPhase(
            DisplayPhase::ConvertFirst);

        const std::uint64_t
            firstConvertUs =
            static_cast<std::uint64_t>(
                phaseTimer.nsecsElapsed() /
                1000);

        performanceStats_.displayFirst.update(
            firstConvertUs);

        const std::uint64_t
            firstReadyUs =
            static_cast<std::uint64_t>(
                totalDisplayTimer.nsecsElapsed() /
                1000);

        {
            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            DisplayFrameSlot& slot =
                displayFrameSlots_[
                    static_cast<std::size_t>(
                        generation %
                        kFrameSlotCount)];

            slot.first =
                firstDisplayImage;

            slot.second =
                QImage{};

            slot.generation =
                generation;

            slot.firstReady =
                true;

            slot.secondReady =
                false;
        }

        displayPresenterCondition_.
            notify_one();

        const auto firstPerformance =
            displayPhasePerformance_;

        QImage secondDisplayImage(
            outputWidth,
            outputHeight,
            QImage::Format_RGB32);

        secondDisplayImage.detach();

        displayPhaseLuma_ =
            progressiveLuma_.second.y.data();

        displayPhaseOutputPixels_ =
            reinterpret_cast<QRgb*>(
                secondDisplayImage.bits());

        displayPhaseOutputStridePixels_ =
            secondDisplayImage.bytesPerLine() /
            static_cast<int>(
                sizeof(QRgb));

        phaseTimer.restart();

        runDisplayPhase(
            DisplayPhase::ConvertSecond);

        const std::uint64_t
            secondConvertUs =
            static_cast<std::uint64_t>(
                phaseTimer.nsecsElapsed() /
                1000);

        performanceStats_.displaySecond.update(
            secondConvertUs);

        const std::uint64_t
            secondReadyUs =
            static_cast<std::uint64_t>(
                totalDisplayTimer.nsecsElapsed() /
                1000);

        {
            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            DisplayFrameSlot& slot =
                displayFrameSlots_[
                    static_cast<std::size_t>(
                        generation %
                        kFrameSlotCount)];

            if (slot.generation ==
                generation)
            {
                slot.second =
                    secondDisplayImage;

                slot.secondReady =
                    true;
            }
        }

        displayPresenterCondition_.
            notify_one();

        const auto secondPerformance =
            displayPhasePerformance_;

        std::uint64_t allocationUs = 0;
        std::uint64_t setupUs = 0;
        std::uint64_t composeUs = 0;
        std::uint64_t interpolationUs = 0;
        std::uint64_t colorConversionUs = 0;
        std::uint64_t outputUs = 0;

        for (std::size_t workerIndex = 0;
            workerIndex <
            displayPhaseWorkers_.size();
            ++workerIndex)
        {
            allocationUs +=
                firstPerformance[
                    workerIndex].
                allocationUs +
                        secondPerformance[
                            workerIndex].
                        allocationUs;

                            setupUs +=
                                firstPerformance[
                                    workerIndex].
                                setupUs +
                                        secondPerformance[
                                            workerIndex].
                                        setupUs;

                                            composeUs +=
                                                firstPerformance[
                                                    workerIndex].
                                                composeUs +
                                                        secondPerformance[
                                                            workerIndex].
                                                        composeUs;

                                                            interpolationUs +=
                                                                firstPerformance[
                                                                    workerIndex].
                                                                interpolationUs +
                                                                        secondPerformance[
                                                                            workerIndex].
                                                                        interpolationUs;

                                                                            colorConversionUs +=
                                                                                firstPerformance[
                                                                                    workerIndex].
                                                                                colorConversionUs +
                                                                                        secondPerformance[
                                                                                            workerIndex].
                                                                                        colorConversionUs;

                                                                                            outputUs +=
                                                                                                firstPerformance[
                                                                                                    workerIndex].
                                                                                                outputUs +
                                                                                                        secondPerformance[
                                                                                                            workerIndex].
                                                                                                        outputUs;
        }

        performanceStats_.displayAllocation.update(
            allocationUs);

        performanceStats_.displaySetup.update(
            setupUs);

        performanceStats_.displayCompose.update(
            composeUs);

        performanceStats_.displayInterpolation.update(
            interpolationUs);

        performanceStats_.displayColorConversion.update(
            colorConversionUs);

        performanceStats_.displayOutput.update(
            outputUs);

        constexpr std::uint64_t
            kFirstFieldDeadlineUs =
            40000u;

        constexpr std::uint64_t
            kSecondFieldDeadlineUs =
            60000u;

        performanceStats_.field1Ready.update(
            firstReadyUs);

        performanceStats_.field2Ready.update(
            secondReadyUs);

        const std::uint64_t field1MarginUs =
            firstReadyUs <
            kFirstFieldDeadlineUs
            ? kFirstFieldDeadlineUs -
            firstReadyUs
            : 0u;

        const std::uint64_t field2MarginUs =
            secondReadyUs <
            kSecondFieldDeadlineUs
            ? kSecondFieldDeadlineUs -
            secondReadyUs
            : 0u;

        performanceStats_.field1Margin.update(
            field1MarginUs);

        performanceStats_.field2Margin.update(
            field2MarginUs);

        if (firstReadyUs >
            kFirstFieldDeadlineUs)
        {
            performanceStats_.
                field1DeadlineMisses.fetch_add(
                    1u,
                    std::memory_order_relaxed);
        }

        if (secondReadyUs >
            kSecondFieldDeadlineUs)
        {
            performanceStats_.
                field2DeadlineMisses.fetch_add(
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

void VideoEngine::displayPresenterLoop()
{
    using Clock =
        std::chrono::steady_clock;

    constexpr auto fieldPeriod =
        std::chrono::milliseconds(20);

    DWORD mmcssTaskIndex = 0;

    HANDLE mmcssHandle =
        AvSetMmThreadCharacteristicsW(
            L"Playback",
            &mmcssTaskIndex);

    if (mmcssHandle != nullptr)
    {
        AvSetMmThreadPriority(
            mmcssHandle,
            AVRT_PRIORITY_HIGH);
    }

    HANDLE waitableTimer =
        CreateWaitableTimerExW(
            nullptr,
            nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);

    if (waitableTimer == nullptr)
    {
        waitableTimer =
            CreateWaitableTimerW(
                nullptr,
                FALSE,
                nullptr);
    }

    const auto waitUntil =
        [waitableTimer](
            Clock::time_point targetTime)
        {
            if (waitableTimer == nullptr)
            {
                std::this_thread::sleep_until(
                    targetTime);

                return;
            }

            for (;;)
            {
                const Clock::time_point now =
                    Clock::now();

                if (now >= targetTime)
                {
                    return;
                }

                const auto remaining =
                    std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                        targetTime - now);

                LONGLONG due100ns =
                    static_cast<LONGLONG>(
                        (remaining.count() + 99) /
                        100);

                due100ns =
                    std::max<LONGLONG>(
                        due100ns,
                        1);

                LARGE_INTEGER dueTime;
                dueTime.QuadPart =
                    -due100ns;

                if (!SetWaitableTimer(
                    waitableTimer,
                    &dueTime,
                    0,
                    nullptr,
                    nullptr,
                    FALSE))
                {
                    std::this_thread::sleep_until(
                        targetTime);

                    return;
                }

                WaitForSingleObject(
                    waitableTimer,
                    INFINITE);
            }
        };

    Clock::time_point previousPresentTime{};

    const auto recordPresentInterval =
        [this, &previousPresentTime]()
        {
            const Clock::time_point now =
                Clock::now();

            if (previousPresentTime !=
                Clock::time_point{})
            {
                const auto intervalUs =
                    static_cast<std::uint64_t>(
                        std::chrono::duration_cast<
                        std::chrono::microseconds>(
                            now -
                            previousPresentTime)
                        .count());

                performanceStats_.presentInterval.update(
                    intervalUs);
            }

            previousPresentTime =
                now;
        };

    for (;;)
    {
        std::uint64_t captureGeneration = 0;
        Clock::time_point captureTickTime;

        QImage firstImage;
        QImage secondImage;

        std::uint64_t presentedGeneration = 0;
        bool presentingNewGeneration = false;
        bool haveFirst = false;

        {
            std::unique_lock<std::mutex> lock(
                displayPresenterMutex_);

            displayPresenterCondition_.wait(
                lock,
                [this]()
                {
                    return
                        displayPresenterStop_ ||
                        latestCaptureTickGeneration_ !=
                        presenterLastTickGeneration_;
                });

            if (displayPresenterStop_)
            {
                break;
            }

            captureGeneration =
                latestCaptureTickGeneration_;

            captureTickTime =
                latestCaptureTickTime_;

            presenterLastTickGeneration_ =
                captureGeneration;

            if (captureGeneration > 1)
            {
                const std::uint64_t
                    expectedGeneration =
                    captureGeneration - 1;

                DisplayFrameSlot& slot =
                    displayFrameSlots_[
                        static_cast<std::size_t>(
                            expectedGeneration %
                            kFrameSlotCount)];

                if (slot.generation ==
                    expectedGeneration &&
                    slot.firstReady)
                {
                    firstImage =
                        slot.first;

                    presentedGeneration =
                        expectedGeneration;

                    presentingNewGeneration =
                        true;

                    haveFirst =
                        true;

                    lastPresentedFirst_ =
                        firstImage;
                }
                else if (
                    lastPresentedPairValid_)
                {
                    firstImage =
                        lastPresentedFirst_;

                    secondImage =
                        lastPresentedSecond_;

                    haveFirst =
                        true;
                }
            }
        }

        if (!haveFirst)
        {
            continue;
        }

        recordPresentInterval();

        {
            const Clock::time_point now =
                Clock::now();

            const auto offsetUs =
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                        now - captureTickTime)
                .count();

            const std::uint64_t presentUs =
                40000u +
                static_cast<std::uint64_t>(
                    std::max<std::int64_t>(
                        offsetUs,
                        0));

            performanceStats_.field1Present.update(
                presentUs);
        }

        emit frameChanged(
            firstImage);

        const Clock::time_point secondFieldTime =
            captureTickTime +
            fieldPeriod;

        waitUntil(
            secondFieldTime);

        {
            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            if (displayPresenterStop_)
            {
                break;
            }

            if (presentingNewGeneration)
            {
                DisplayFrameSlot& slot =
                    displayFrameSlots_[
                        static_cast<std::size_t>(
                            presentedGeneration %
                            kFrameSlotCount)];

                if (slot.generation ==
                    presentedGeneration &&
                    slot.secondReady)
                {
                    secondImage =
                        slot.second;

                    lastPresentedSecond_ =
                        secondImage;

                    lastPresentedPairValid_ =
                        true;
                }
                else if (
                    lastPresentedPairValid_)
                {
                    secondImage =
                        lastPresentedSecond_;
                }
                else
                {
                    secondImage =
                        firstImage;
                }
            }
        }

        if (!secondImage.isNull())
        {
            recordPresentInterval();

            {
                const Clock::time_point now =
                    Clock::now();

                const auto offsetUs =
                    std::chrono::duration_cast<
                        std::chrono::microseconds>(
                            now - captureTickTime)
                    .count();

                const std::uint64_t presentUs =
                    40000u +
                    static_cast<std::uint64_t>(
                        std::max<std::int64_t>(
                            offsetUs,
                            0));

                performanceStats_.field2Present.update(
                    presentUs);
            }

            emit frameChanged(
                secondImage);
        }
    }

    if (waitableTimer != nullptr)
    {
        CancelWaitableTimer(
            waitableTimer);

        CloseHandle(
            waitableTimer);
    }

    if (mmcssHandle != nullptr)
    {
        AvRevertMmThreadCharacteristics(
            mmcssHandle);
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
    for (DisplayConverter& converter :
        displayConverters_)
    {
        converter.setGamma(
            gamma);
    }
}

PerformanceSnapshot VideoEngine::performanceSnapshot() const
{
    return performanceStats_.snapshot();
}

void VideoEngine::setWaveformAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    waveformRenderer_.setAspectRatio(
        aspectRatio);
}
