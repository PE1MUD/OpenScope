#include <QMetaObject>
#include "VideoEngine.h"
#include "standards/VideoStandard.h"
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
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

    for (DisplayConverter& converter :
        displayConverters_)
    {
        converter.setImplementation(
            DisplayConversionImplementation::Avx2);
    }

    spoutVideoConverter_.setImplementation(
        DisplayConversionImplementation::Avx2);

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

    vectorscopeScreenRenderer_.moveAnalyzerToThread(
        &vectorscopeThread_);

    vectorscopeVideoRenderer_.moveAnalyzerToThread(
        &vectorscopeThread_);

    connect(
        &vectorscopeThread_,
        &QThread::started,
        this,
        [this]()
        {
            vectorscopeWorkerLoop();
        },
        Qt::DirectConnection);

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

    // Each producer starts from a valid source state. Hardware capture can
    // explicitly override this before submitWriteFrame() (for example when
    // DeckLink reports bmdFrameHasNoInputSource).
    slot.frame.inputSignalValid = true;

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

    {
        std::lock_guard<std::mutex> lock(
            exportSnapshotMutex_);

        if (exportSnapshotRequested_)
        {
            exportSnapshotFrame_ =
                slot.frame;

            exportSnapshotRequested_ = false;
            exportSnapshotReady_ = true;

            exportSnapshotCondition_.notify_one();
        }
    }

    slot.writing.store(
        false,
        std::memory_order_release);

    latestCaptureSlot_.store(
        static_cast<int>(slotIndex),
        std::memory_order_release);

    emit inputSignalStateChanged(
        slot.frame.inputSignalValid);

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


void VideoEngine::requestWaveformFlatFieldSpectrum()
{
    flatFieldSpectrumRequested_.store(
        true,
        std::memory_order_release);

    waveformCondition_.notify_one();
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
    const int clampedPersistence =
        std::clamp(
            persistence,
            0,
            200);

    waveformPersistence_.store(
        clampedPersistence,
        std::memory_order_release);

    vectorscopePersistence_.store(
        clampedPersistence,
        std::memory_order_release);
}


void VideoEngine::setVectorscopeGlow(
    int glow)
{
    vectorscopeGlow_.store(
        std::clamp(
            glow,
            0,
            100),
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


void VideoEngine::setWaveformVideoContentScale(
    double scale)
{
    waveformVideoContentScale_.store(
        std::clamp(
            scale,
            0.1,
            1.0),
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


void VideoEngine::setVectorscopeVideoOutputSize(
    int width,
    int height)
{
    vectorscopeVideoOutputWidth_.store(
        (std::max)(width, kMinimumOutputSize),
        std::memory_order_release);

    vectorscopeVideoOutputHeight_.store(
        (std::max)(height, kMinimumOutputSize),
        std::memory_order_release);
}


void VideoEngine::setVectorscopeVideoContentScale(
    double horizontalScale,
    double verticalScale)
{
    vectorscopeVideoContentScaleX_.store(
        std::clamp(horizontalScale, 0.10, 1.0),
        std::memory_order_release);

    vectorscopeVideoContentScaleY_.store(
        std::clamp(verticalScale, 0.10, 1.0),
        std::memory_order_release);
}


void VideoEngine::setVectorscopeVideoEnabled(bool enabled)
{
    vectorscopeVideoEnabled_.store(
        enabled,
        std::memory_order_release);
}


void VideoEngine::setVectorscopePresentationInfo(
    const VectorscopePresentationInfo& info)
{
    std::lock_guard<std::mutex> lock(
        vectorscopePresentationMutex_);

    vectorscopePresentationInfo_ = info;
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

    waveformScreenRenderer_.setZoomFactor(
        factor);

    waveformVideoRenderer_.setZoomFactor(
        factor);
}


void VideoEngine::setWaveformScrollPosition(
    double position)
{
    waveformScrollPosition_.store(
        position,
        std::memory_order_release);

    waveformScreenRenderer_.setScrollPosition(
        position);

    waveformVideoRenderer_.setScrollPosition(
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

            slot.spoutFirst =
                QImage{};

            slot.spoutSecond =
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

        if (spoutVideoEnabled_.load(
            std::memory_order_acquire))
        {
            DisplayPerformance spoutPerformance;

            QImage firstSpoutImage =
                spoutVideoConverter_.convert(
                    *displayFrame,
                    progressiveLuma_.first.y.data(),
                    kCaptureWidth,
                    kCaptureHeight,
                    spoutPerformance);

            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            DisplayFrameSlot& slot =
                displayFrameSlots_[
                    static_cast<std::size_t>(
                        generation %
                        kFrameSlotCount)];

            if (slot.generation == generation)
            {
                slot.spoutFirst =
                    std::move(firstSpoutImage);
            }
        }

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

        if (spoutVideoEnabled_.load(
            std::memory_order_acquire))
        {
            DisplayPerformance spoutPerformance;

            QImage secondSpoutImage =
                spoutVideoConverter_.convert(
                    *displayFrame,
                    progressiveLuma_.second.y.data(),
                    kCaptureWidth,
                    kCaptureHeight,
                    spoutPerformance);

            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            DisplayFrameSlot& slot =
                displayFrameSlots_[
                    static_cast<std::size_t>(
                        generation %
                        kFrameSlotCount)];

            if (slot.generation == generation)
            {
                slot.spoutSecond =
                    std::move(secondSpoutImage);
            }
        }

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
        QImage firstSpoutImage;
        QImage secondSpoutImage;

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

                    firstSpoutImage =
                        slot.spoutFirst;

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

        if (spoutVideoEnabled_.load(
            std::memory_order_acquire) &&
            !firstSpoutImage.isNull())
        {
            emit videoSpoutChanged(
                firstSpoutImage);
        }

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

                    secondSpoutImage =
                        slot.spoutSecond;

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

            if (spoutVideoEnabled_.load(
                std::memory_order_acquire) &&
                !secondSpoutImage.isNull())
            {
                emit videoSpoutChanged(
                    secondSpoutImage);
            }
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

        const int screenOutputWidth =
            waveformOutputWidth_.load(
                std::memory_order_acquire);

        const int screenOutputHeight =
            waveformOutputHeight_.load(
                std::memory_order_acquire);

        const int selectedLine =
            selectedLine_.load(
                std::memory_order_acquire);

        const int waveformPersistence =
            waveformPersistence_.load(
                std::memory_order_acquire);

        const int glow =
            vectorscopeGlow_.load(
                std::memory_order_acquire);

        // Screen render: current widget canvas, always full-frame.
        waveformScreenRenderer_.setOutputSize(
            screenOutputWidth,
            screenOutputHeight);

        waveformScreenRenderer_.setFitAspectRatio(true);

        waveformScreenRenderer_.setContentScale(
            1.0,
            1.0);

        waveformScreenRenderer_.setSelectedLine(
            selectedLine);

        waveformScreenRenderer_.setPersistence(
            waveformPersistence);

        waveformScreenRenderer_.setGlow(
            glow);

        QElapsedTimer screenTimer;
        screenTimer.start();

        waveformScreenRenderer_.analyze(
            captureSlot.frame);

        performanceStats_.waveformScreen.update(
            static_cast<std::uint64_t>(
                screenTimer.nsecsElapsed() /
                1000));

        // Video render target is the actual output raster of the
        // selected video standard. PAL625 therefore remains 720x576
        // for both 4:3 and 16:9; aspect ratio is carried separately.
        constexpr VideoStandard videoStandard =
            VideoStandard::pal625();

        const auto videoAspectRatio =
            waveformVideoAspectRatio_.load(
                std::memory_order_acquire);

        waveformVideoRenderer_.setOutputSize(
            videoStandard.outputWidth,
            videoStandard.outputHeight);

        waveformVideoRenderer_.setAspectRatio(
            videoAspectRatio);

        // PAL 4:3 and 16:9 both use the complete 720x576
        // output raster. Aspect ratio is metadata / pixel aspect;
        // it must not letterbox the render canvas.
        waveformVideoRenderer_.setFitAspectRatio(false);

        waveformVideoRenderer_.setContentScale(
            videoStandard.safeWidthScale,
            videoStandard.safeHeightScale);

        waveformVideoRenderer_.setSelectedLine(
            selectedLine);

        waveformVideoRenderer_.setPersistence(
            waveformPersistence);

        waveformVideoRenderer_.setGlow(
            glow);

        QElapsedTimer videoTimer;
        videoTimer.start();

        waveformVideoRenderer_.analyze(
            captureSlot.frame);

        performanceStats_.waveformVideo.update(
            static_cast<std::uint64_t>(
                videoTimer.nsecsElapsed() /
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

        const auto& measurementSource =
            waveformScreenRenderer_.visibleLumaVolts();

        QVector<float> measurementSamples;
        measurementSamples.reserve(
            static_cast<qsizetype>(measurementSource.size()));

        for (float sample : measurementSource)
        {
            measurementSamples.append(sample);
        }

        emit waveformMeasurementDataChanged(
            measurementSamples);

        const auto& fullSpectrumSource =
            waveformScreenRenderer_.fullLumaVolts();

        QVector<float> fullSpectrumSamples;
        fullSpectrumSamples.reserve(
            static_cast<qsizetype>(fullSpectrumSource.size()));

        for (float sample : fullSpectrumSource)
        {
            fullSpectrumSamples.append(sample);
        }

        if (selectedLine >= 0)
        {
            emit waveformSpectrumDataChanged(
                fullSpectrumSamples,
                measurementSamples,
                captureSlot.frame.inputSignalValid);
        }
        else
        {
            emit waveformSpectrumDataChanged(
                QVector<float>{},
                QVector<float>{},
                captureSlot.frame.inputSignalValid);
        }

        if (flatFieldSpectrumRequested_.exchange(
                false,
                std::memory_order_acq_rel))
        {
            const int lineLength =
                captureSlot.frame.width;

            // The final PAL line in this capture contains the start of the
            // following sync structure.  Flat-field analysis deliberately
            // excludes that line and uses every preceding active raster line.
            const int lineCount = std::max(
                0,
                captureSlot.frame.height - 1);

            QVector<float> flatFieldSamples;
            flatFieldSamples.resize(
                static_cast<qsizetype>(lineLength) *
                static_cast<qsizetype>(lineCount));

            constexpr double blackVolts = 0.300;
            constexpr double whiteVolts = 1.000;
            constexpr double yBlack10 = 64.0;
            constexpr double yWhite10 = 940.0;
            constexpr double voltsPerCode =
                (whiteVolts - blackVolts) /
                (yWhite10 - yBlack10);

            for (int y = 0; y < lineCount; ++y)
            {
                const std::size_t sourceOffset =
                    static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(lineLength);

                const qsizetype destinationOffset =
                    static_cast<qsizetype>(y) *
                    static_cast<qsizetype>(lineLength);

                for (int x = 0; x < lineLength; ++x)
                {
                    const double y10 =
                        static_cast<double>(
                            captureSlot.frame.y[
                                sourceOffset +
                                static_cast<std::size_t>(x)]) /
                        64.0;

                    flatFieldSamples[
                        destinationOffset + x] =
                        static_cast<float>(
                            blackVolts +
                            (y10 - yBlack10) * voltsPerCode);
                }
            }

            emit waveformFlatFieldSpectrumDataChanged(
                flatFieldSamples,
                lineLength,
                lineCount,
                captureSlot.frame.inputSignalValid);
        }

        emit waveformChanged(
            waveformScreenRenderer_.image());

        emit waveformVideoChanged(
            waveformVideoRenderer_.image());
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

        VectorscopePresentationInfo presentation;

        {
            std::lock_guard<std::mutex> lock(
                vectorscopePresentationMutex_);

            presentation =
                vectorscopePresentationInfo_;
        }

        const int selectedLine =
            selectedLine_.load(
                std::memory_order_acquire);

        const int zoomFactor =
            waveformZoomFactor_.load(
                std::memory_order_acquire);

        const double scrollPosition =
            waveformScrollPosition_.load(
                std::memory_order_acquire);

        const int persistence =
            vectorscopePersistence_.load(
                std::memory_order_acquire);

        const int glow =
            vectorscopeGlow_.load(
                std::memory_order_acquire);

        const int screenWidth =
            vectorscopeOutputWidth_.load(
                std::memory_order_acquire);

        const int screenHeight =
            vectorscopeOutputHeight_.load(
                std::memory_order_acquire);

        vectorscopeScreenRenderer_.setOutputSize(
            screenWidth,
            screenHeight);

        vectorscopeScreenRenderer_.setPresentationInfo(
            presentation);

        vectorscopeScreenRenderer_.setSelectedLine(
            selectedLine);

        vectorscopeScreenRenderer_.setHorizontalWindow(
            zoomFactor,
            scrollPosition);

        vectorscopeScreenRenderer_.setPersistence(
            persistence);

        vectorscopeScreenRenderer_.setGlow(
            glow);

        QElapsedTimer timer;
        timer.start();

        vectorscopeScreenRenderer_.analyze(
            captureSlots_[slotIndex].frame);

        performanceStats_.vectorscope.update(
            static_cast<std::uint64_t>(
                timer.nsecsElapsed() / 1000));

        emit vectorscopeChanged(
            vectorscopeScreenRenderer_.image());

        if (vectorscopeVideoEnabled_.load(
                std::memory_order_acquire))
        {
            const int videoWidth =
                vectorscopeVideoOutputWidth_.load(
                    std::memory_order_acquire);

            const int videoHeight =
                vectorscopeVideoOutputHeight_.load(
                    std::memory_order_acquire);

            const double contentScaleX =
                vectorscopeVideoContentScaleX_.load(
                    std::memory_order_acquire);

            const double contentScaleY =
                vectorscopeVideoContentScaleY_.load(
                    std::memory_order_acquire);

            vectorscopeVideoRenderer_.setOutputSize(
                videoWidth,
                videoHeight);

            vectorscopeVideoRenderer_.setContentScale(
                contentScaleX,
                contentScaleY);

            vectorscopeVideoRenderer_.setPresentationInfo(
                presentation);

            vectorscopeVideoRenderer_.setSelectedLine(
                selectedLine);

            vectorscopeVideoRenderer_.setHorizontalWindow(
                zoomFactor,
                scrollPosition);

            vectorscopeVideoRenderer_.setPersistence(
                persistence);

            vectorscopeVideoRenderer_.setGlow(
                glow);

            vectorscopeVideoRenderer_.analyze(
                captureSlots_[slotIndex].frame);

            emit vectorscopeVideoChanged(
                vectorscopeVideoRenderer_.image());
        }

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
    waveformScreenRenderer_.setChromaFillIntensity(
        intensity);

    waveformVideoRenderer_.setChromaFillIntensity(
        intensity);
}

void VideoEngine::setWaveformColor(bool enabled)
{
    waveformScreenRenderer_.setColor(enabled);
    waveformVideoRenderer_.setColor(enabled);
}

void VideoEngine::setSpoutVideoEnabled(
    bool enabled)
{
    spoutVideoEnabled_.store(
        enabled,
        std::memory_order_release);
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


QImage VideoEngine::captureHighResolutionSnapshot()
{
    Yuv444Frame sourceFrame;

    {
        std::unique_lock<std::mutex> lock(
            exportSnapshotMutex_);

        exportSnapshotReady_ = false;
        exportSnapshotRequested_ = true;

        constexpr auto kSnapshotTimeout =
            std::chrono::milliseconds(250);

        const bool ready =
            exportSnapshotCondition_.wait_for(
                lock,
                kSnapshotTimeout,
                [this]()
                {
                    return exportSnapshotReady_;
                });

        if (!ready)
        {
            exportSnapshotRequested_ = false;
            return {};
        }

        sourceFrame =
            std::move(
                exportSnapshotFrame_);

        exportSnapshotFrame_ = {};
        exportSnapshotReady_ = false;
    }

    if (sourceFrame.width <= 0 ||
        sourceFrame.height <= 0 ||
        sourceFrame.y.empty() ||
        sourceFrame.u.empty() ||
        sourceFrame.v.empty())
    {
        return {};
    }

    constexpr int kExportScale = 4;

    const int exportWidth =
        sourceFrame.width *
        kExportScale;

    const int exportHeight =
        sourceFrame.height *
        kExportScale;

    Yuv444Frame exportFrame;

    exportFrame.resize(
        exportWidth,
        sourceFrame.height);

    LineResampler lumaResampler(
        kLumaReconstructionRadius,
        kLumaReconstructionCutoff);

    lumaResampler.setImplementation(
        ResamplerImplementation::Avx2);

    std::vector<float> sourceLuma(
        static_cast<std::size_t>(
            sourceFrame.width));

    std::vector<float> reconstructedLuma(
        static_cast<std::size_t>(
            exportWidth));

    const double horizontalScale =
        static_cast<double>(
            sourceFrame.width) /
        static_cast<double>(
            exportWidth);

    const auto interpolateChroma =
        [](std::uint16_t left,
            std::uint16_t right,
            double fraction)
        {
            const double value =
                static_cast<double>(left) +
                (
                    static_cast<double>(right) -
                    static_cast<double>(left)
                ) *
                fraction;

            return
                static_cast<std::uint16_t>(
                    std::lround(
                        std::clamp(
                            value,
                            0.0,
                            65535.0)));
        };

    for (int y = 0;
        y < sourceFrame.height;
        ++y)
    {
        const std::size_t sourceLineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(
                sourceFrame.width);

        const std::size_t exportLineOffset =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(
                exportWidth);

        for (int x = 0;
            x < sourceFrame.width;
            ++x)
        {
            sourceLuma[
                static_cast<std::size_t>(x)] =
                static_cast<float>(
                    sourceFrame.y[
                        sourceLineOffset +
                        static_cast<std::size_t>(
                            x)]);
        }

        lumaResampler.resample(
            sourceLuma,
            reconstructedLuma);

        for (int x = 0;
            x < exportWidth;
            ++x)
        {
            const std::size_t exportIndex =
                exportLineOffset +
                static_cast<std::size_t>(x);

            exportFrame.y[
                exportIndex] =
                static_cast<std::uint16_t>(
                    std::lround(
                        std::clamp(
                            reconstructedLuma[
                                static_cast<std::size_t>(
                                    x)],
                            0.0f,
                            65535.0f)));

            const double sourcePosition =
                (static_cast<double>(x) + 0.5) *
                horizontalScale -
                0.5;

            const int leftIndex =
                std::clamp(
                    static_cast<int>(
                        std::floor(
                            sourcePosition)),
                    0,
                    sourceFrame.width - 1);

            const int rightIndex =
                (std::min)(
                    leftIndex + 1,
                    sourceFrame.width - 1);

            const double fraction =
                std::clamp(
                    sourcePosition -
                    static_cast<double>(
                        leftIndex),
                    0.0,
                    1.0);

            const std::size_t leftOffset =
                sourceLineOffset +
                static_cast<std::size_t>(
                    leftIndex);

            const std::size_t rightOffset =
                sourceLineOffset +
                static_cast<std::size_t>(
                    rightIndex);

            exportFrame.u[
                exportIndex] =
                interpolateChroma(
                    sourceFrame.u[
                        leftOffset],
                    sourceFrame.u[
                        rightOffset],
                    fraction);

            exportFrame.v[
                exportIndex] =
                interpolateChroma(
                    sourceFrame.v[
                        leftOffset],
                    sourceFrame.v[
                        rightOffset],
                    fraction);
        }
    }

    DisplayConverter exportConverter;

    exportConverter.setImplementation(
        DisplayConversionImplementation::Avx2);

    // Export must represent the signal, not the user's display-look setting.
    exportConverter.setGamma(
        1.0);

    exportConverter.setHighlightedLine(
        -1);

    exportConverter.setHighlightedRange(
        0,
        -1);

    DisplayPerformance performance;

    return
        exportConverter.convert(
            exportFrame,
            exportFrame.y.data(),
            exportWidth,
            exportHeight,
            performance);
}


PerformanceSnapshot VideoEngine::performanceSnapshot() const
{
    return performanceStats_.snapshot();
}

void VideoEngine::setWaveformAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    waveformScreenRenderer_.setAspectRatio(
        aspectRatio);
}

void VideoEngine::setWaveformVideoAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    waveformVideoAspectRatio_.store(
        aspectRatio,
        std::memory_order_release);
}
