#include <QMetaObject>
#include "VideoEngine.h"
#include "BuildConfig.h"

#include <fstream>
#include <iomanip>
#include "standards/VideoStandard.h"
#include "diagnostics/TraceLog.h"
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
#ifdef _WIN32
    // OpenScope is a real-time-ish video instrument. Windows execution-speed
    // power throttling is undesirable: losing foreground focus must not
    // deliberately reduce renderer CPU throughput.
    PROCESS_POWER_THROTTLING_STATE powerThrottlingState{};
    powerThrottlingState.Version =
        PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    powerThrottlingState.ControlMask =
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    powerThrottlingState.StateMask = 0;

    SetProcessInformation(
        GetCurrentProcess(),
        ProcessPowerThrottling,
        &powerThrottlingState,
        sizeof(powerThrottlingState));
#endif

    waveformScreenRenderer_.setTraceRendererId(
        TraceRendererId::PcWaveform);
    waveformVideoRenderer_.setTraceRendererId(
        TraceRendererId::SpoutWaveform);

    for (DisplayConverter& converter :
        displayConverters_)
    {
        converter.setImplementation(
            DisplayConversionImplementation::Avx2);
    }

    for (DisplayConverter& converter :
        spoutVideoConverters_)
    {
        converter.setImplementation(
            DisplayConversionImplementation::Avx2);
    }

    // Conservative startup estimate.  Once real display frames complete,
    // this is replaced by a per-worker EMA of measured display work.
    for (auto& estimateUs :
        displayAssistWorkEstimateUs_)
    {
        estimateUs.store(
            5000u,
            std::memory_order_relaxed);
    }

    waveformScreenRenderer_.setTraceJobExecutor(
        [this](
            char phaseLabel,
            std::size_t jobCount,
            const WaveformRenderer::TraceJob& job)
        {
            runWaveformTraceJobs(
                phaseLabel,
                jobCount,
                job);
        });

    waveformScreenRenderer_.setTraceHelperAvailability(
        [this]()
        {
            for (std::size_t workerIndex = 0;
                workerIndex < displayPhaseWorkers_.size();
                ++workerIndex)
            {
                if (canDisplayWorkerAcceptAssist(workerIndex))
                {
                    return true;
                }
            }

            return false;
        });

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

    const std::int64_t captureTickNs =
        static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                captureTickTime.time_since_epoch()).count());

    slot.captureTickNs.store(
        captureTickNs,
        std::memory_order_release);
    slot.diagnosticOriginNs.store(
        captureTickNs,
        std::memory_order_release);

    const std::uint64_t frequencyTimelineGeneration =
        captureTickNs > 0
        ? static_cast<std::uint64_t>(captureTickNs)
        : 0u;
    for (auto& timeline : slot.frequencyAssistTimeline)
    {
        timeline.reset(frequencyTimelineGeneration);
    }

    const bool applyLumaCompensation =
        lumaCompensationSourceEnabled_.load(
            std::memory_order_acquire) &&
        lumaCompensationEnabled_.load(
            std::memory_order_acquire);

    std::uint32_t frequencyCompensationUs = 0u;

    if (applyLumaCompensation)
    {
        const auto frequencyCompensationStart =
            std::chrono::steady_clock::now();

        runFrequencyCompensationJobs(
            slot,
            lumaCompensationGainHundredthsDb_.load(
                std::memory_order_acquire));

        const auto frequencyCompensationEnd =
            std::chrono::steady_clock::now();

        frequencyCompensationUs =
            static_cast<std::uint32_t>(
                std::min<std::int64_t>(
                    80'000,
                    std::max<std::int64_t>(
                        0,
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            frequencyCompensationEnd -
                            frequencyCompensationStart).count())));
    }

    slot.frequencyCompensationUs.store(
        frequencyCompensationUs,
        std::memory_order_release);

    const std::uint64_t generation =
        captureGeneration_.fetch_add(
            1,
            std::memory_order_relaxed) + 1;

    traceLog(
        TraceEventType::Frame,
        generation,
        0u,
        static_cast<std::uint32_t>(slotIndex),
        slot.frame.inputSignalValid ? 1u : 0u,
        0u);

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

    latestCaptureTickNs_.store(
        captureTickNs,
        std::memory_order_release);

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


bool VideoEngine::startWaveformRawCapture(
    const std::string& rawFilePath)
{
    if constexpr (!OpenScopeBuild::kDebugBuild)
    {
        return false;
    }

    const int line =
        selectedLine_.load(
            std::memory_order_acquire);

    if (line < 0 || rawFilePath.empty())
    {
        return false;
    }

    std::scoped_lock lock(
        waveformRawCaptureMutex_);

    if (waveformRawCaptureActive_)
    {
        return false;
    }

    waveformRawCaptureLine_ = line;
    waveformRawCaptureFrameCount_ = 0;
    waveformRawCapturePath_ = rawFilePath;
    waveformRawCaptureSamples_.assign(
        kWaveformRawCaptureFrames *
            kWaveformRawCaptureSamples,
        0u);
    waveformRawCaptureGenerations_.assign(
        kWaveformRawCaptureFrames,
        0u);
    waveformRawCaptureActive_ = true;

    return true;
}

void VideoEngine::captureWaveformRawFrame(
    const std::vector<float>& reconstructedSamples,
    std::uint64_t generation,
    int renderedLine)
{
    std::string rawPath;
    int capturedLine = -1;
    std::vector<std::uint16_t> samples;
    std::vector<std::uint64_t> generations;

    {
        std::scoped_lock lock(
            waveformRawCaptureMutex_);

        if (!waveformRawCaptureActive_ ||
            renderedLine != waveformRawCaptureLine_ ||
            reconstructedSamples.size() !=
                kWaveformRawCaptureSamples)
        {
            return;
        }

        const std::size_t frameIndex =
            waveformRawCaptureFrameCount_;

        auto* destination =
            waveformRawCaptureSamples_.data() +
            frameIndex * kWaveformRawCaptureSamples;

        for (std::size_t x = 0;
            x < kWaveformRawCaptureSamples;
            ++x)
        {
            destination[x] =
                static_cast<std::uint16_t>(
                    std::lround(
                        std::clamp(
                            reconstructedSamples[x],
                            0.0f,
                            65535.0f)));
        }

        waveformRawCaptureGenerations_[frameIndex] =
            generation;

        ++waveformRawCaptureFrameCount_;

        if (waveformRawCaptureFrameCount_ <
            kWaveformRawCaptureFrames)
        {
            return;
        }

        waveformRawCaptureActive_ = false;
        rawPath = waveformRawCapturePath_;
        capturedLine = waveformRawCaptureLine_;
        samples = std::move(
            waveformRawCaptureSamples_);
        generations = std::move(
            waveformRawCaptureGenerations_);
    }

    waveformRawCaptureWriter_ =
        std::jthread(
            [rawPath = std::move(rawPath),
                capturedLine,
                samples = std::move(samples),
                generations = std::move(generations)]()
            {
                std::ofstream raw(
                    rawPath,
                    std::ios::binary |
                    std::ios::trunc);

                if (raw)
                {
                    raw.write(
                        reinterpret_cast<const char*>(
                            samples.data()),
                        static_cast<std::streamsize>(
                            samples.size() *
                            sizeof(std::uint16_t)));
                }

                std::string txtPath = rawPath;
                const auto dot = txtPath.find_last_of('.');
                if (dot != std::string::npos)
                {
                    txtPath.resize(dot);
                }
                txtPath += ".txt";

                std::ofstream meta(
                    txtPath,
                    std::ios::trunc);

                if (meta)
                {
                    meta
                        << "OpenScope waveform RAW capture\n"
                        << "format=u16le_y\n"
                        << "line=" << capturedLine << "\n"
                        << "samples_per_frame=2880\n"
                        << "frames=250\n"
                        << "sample_rate_hz=54000000\n"
                        << "bytes_per_frame=5760\n"
                        << "total_bytes=1440000\n"
                        << "generation_numbers=";

                    for (std::size_t i = 0;
                        i < generations.size();
                        ++i)
                    {
                        if (i != 0)
                        {
                            meta << ',';
                        }
                        meta << generations[i];
                    }

                    meta << '\\n';
                }
            });
}

void VideoEngine::setSelectedLine(int line)
{
    // Line selection is navigation state. The display workers read the
    // latest selected line atomically for the next frame, so there is no
    // reason to invalidate the currently presented video frame here.
    //
    // Clearing the presenter on every wheel tick, key repeat or mouse-drag
    // step caused the visible video stalls while moving through the image.
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
    const bool previousEnabled =
        videoHighlightEnabled_.exchange(
            enabled,
            std::memory_order_acq_rel);

    if (previousEnabled != enabled)
    {
        resetDisplayPresentation();
    }
}

void VideoEngine::setVideoLineHighlightEnabled(
    bool enabled)
{
    const bool previousEnabled =
        videoLineHighlightEnabled_.exchange(
            enabled,
            std::memory_order_acq_rel);

    if (previousEnabled != enabled)
    {
        resetDisplayPresentation();
    }
}


void VideoEngine::resetDisplayPresentation()
{
    std::lock_guard<std::mutex> lock(
        displayPresenterMutex_);

    for (DisplayFrameSlot& slot : displayFrameSlots_)
    {
        slot = DisplayFrameSlot{};
    }

    lastPresentedFirst_ = QImage{};
    lastPresentedSecond_ = QImage{};
    lastPresentedSpoutFirst_ = QImage{};
    lastPresentedSpoutSecond_ = QImage{};
    lastPresentedPairValid_ = false;

    displayPresenterCondition_.notify_one();
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


void VideoEngine::setLumaCompensationEnabled(
    bool enabled)
{
    lumaCompensationEnabled_.store(
        enabled,
        std::memory_order_release);
}


void VideoEngine::setLumaCompensationGainHundredthsDb(
    int gainHundredthsDb)
{
    lumaCompensationGainHundredthsDb_.store(
        std::clamp(
            gainHundredthsDb,
            0,
            100),
        std::memory_order_release);
}


void VideoEngine::setLumaCompensationSourceEnabled(
    bool enabled)
{
    lumaCompensationSourceEnabled_.store(
        enabled,
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


void VideoEngine::setWaveformCoreIntensity(
    int intensity)
{
    waveformCoreIntensity_.store(
        std::clamp(
            intensity,
            50,
            200),
        std::memory_order_release);
}


void VideoEngine::setWaveformCoreWidth(
    int widthTenths)
{
    waveformCoreWidthTenths_.store(
        std::clamp(
            widthTenths,
            5,
            30),
        std::memory_order_release);
}


void VideoEngine::setWaveformAntiAliasing(
    bool enabled)
{
    waveformScreenRenderer_.setAntiAliasing(enabled);
    waveformVideoRenderer_.setAntiAliasing(enabled);
}



void VideoEngine::setWaveformColorizeIllegalLuminance(
    bool enabled)
{
    waveformScreenRenderer_.setColorizeIllegalLuminance(enabled);
    waveformVideoRenderer_.setColorizeIllegalLuminance(enabled);
}


void VideoEngine::setVectorscopeColorizeGamutErrors(
    bool enabled)
{
    vectorscopeScreenRenderer_.setColorizeGamutErrors(enabled);
    vectorscopeVideoRenderer_.setColorizeGamutErrors(enabled);
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


void VideoEngine::setWaveformVideoEnabled(bool enabled)
{
    waveformVideoEnabled_.store(
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
    std::uint64_t lastFrequencyAssistGeneration = 0;
    std::uint64_t lastAssistGeneration = 0;

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
        bool runDisplayWork = false;

        {
            std::unique_lock<std::mutex> lock(
                displayPhaseMutex_);

            displayPhaseCondition_.wait(
                lock,
                [this,
                lastPhaseGeneration,
                lastFrequencyAssistGeneration,
                lastAssistGeneration]()
                {
                    return
                        displayPhaseStop_ ||
                        displayPhaseGeneration_ !=
                        lastPhaseGeneration ||
                        frequencyAssistGeneration_.load(
                            std::memory_order_acquire) !=
                        lastFrequencyAssistGeneration ||
                        frequencyAssistWorkAvailable_.load(
                            std::memory_order_acquire) ||
                        waveformAssistGeneration_.load(
                            std::memory_order_acquire) !=
                        lastAssistGeneration ||
                        waveformAssistWorkAvailable_.load(
                            std::memory_order_acquire);
                });

            if (displayPhaseStop_)
            {
                return;
            }

            // Video/deinterlace work always wins.  Waveform helper jobs are
            // considered only when no newer display phase is waiting.
            if (displayPhaseGeneration_ !=
                lastPhaseGeneration)
            {
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

                runDisplayWork = true;
            }
            else
            {
                lastFrequencyAssistGeneration =
                    frequencyAssistGeneration_.load(
                        std::memory_order_acquire);
                lastAssistGeneration =
                    waveformAssistGeneration_.load(
                        std::memory_order_acquire);
            }
        }

        if (runDisplayWork)
        {
            const auto displayWorkerPhaseStart =
                std::chrono::steady_clock::now();

            QElapsedTimer displayWorkerPhaseTimer;
            displayWorkerPhaseTimer.start();

            if (phase ==
                DisplayPhase::NoiseReduction)
            {
                const int height =
                    displayNoiseSource_ != nullptr
                    ? displayNoiseSource_->height
                    : 0;

                const int firstLine =
                    static_cast<int>(workerIndex) *
                    height /
                    static_cast<int>(
                        displayPhaseWorkers_.size());

                const int lastLine =
                    static_cast<int>(workerIndex + 1) *
                    height /
                    static_cast<int>(
                        displayPhaseWorkers_.size());

                if (displayNoiseSource_ != nullptr &&
                    displayNoiseDestination_ != nullptr)
                {
                    noiseReducer_.processRange(
                        *displayNoiseSource_,
                        *displayNoiseDestination_,
                        displayNoiseIntensity_,
                        firstLine,
                        lastLine);
                }
            }
            else if (phase ==
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
            else if (
                phase ==
                DisplayPhase::SpoutFirst ||
                phase ==
                DisplayPhase::SpoutSecond)
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

                spoutVideoConverters_[workerIndex].
                    convertNativeRange(
                        *frame,
                        luma,
                        outputPixels,
                        outputStridePixels,
                        firstOutputY,
                        lastOutputY,
                        displayPhasePerformance_[
                            workerIndex]);
            }

            const std::uint64_t workerPhaseUs =
                static_cast<std::uint64_t>(
                    displayWorkerPhaseTimer.nsecsElapsed() /
                    1000);

            displayCurrentFrameWorkerUs_[
                workerIndex].fetch_add(
                    workerPhaseUs,
                    std::memory_order_relaxed);

            PerformanceMetric* workerMetric = nullptr;

            if (workerIndex == 0)
            {
                switch (phase)
                {
                case DisplayPhase::NoiseReduction:
                    workerMetric = &performanceStats_.displayWorker0Noise;
                    break;
                case DisplayPhase::Deinterlace:
                    workerMetric = &performanceStats_.displayWorker0Deinterlace;
                    break;
                case DisplayPhase::ConvertFirst:
                    workerMetric = &performanceStats_.displayWorker0Convert1;
                    break;
                case DisplayPhase::SpoutFirst:
                    workerMetric = &performanceStats_.displayWorker0Spout1;
                    break;
                case DisplayPhase::ConvertSecond:
                    workerMetric = &performanceStats_.displayWorker0Convert2;
                    break;
                case DisplayPhase::SpoutSecond:
                    workerMetric = &performanceStats_.displayWorker0Spout2;
                    break;
                default:
                    break;
                }
            }
            else
            {
                switch (phase)
                {
                case DisplayPhase::NoiseReduction:
                    workerMetric = &performanceStats_.displayWorker1Noise;
                    break;
                case DisplayPhase::Deinterlace:
                    workerMetric = &performanceStats_.displayWorker1Deinterlace;
                    break;
                case DisplayPhase::ConvertFirst:
                    workerMetric = &performanceStats_.displayWorker1Convert1;
                    break;
                case DisplayPhase::SpoutFirst:
                    workerMetric = &performanceStats_.displayWorker1Spout1;
                    break;
                case DisplayPhase::ConvertSecond:
                    workerMetric = &performanceStats_.displayWorker1Convert2;
                    break;
                case DisplayPhase::SpoutSecond:
                    workerMetric = &performanceStats_.displayWorker1Spout2;
                    break;
                default:
                    break;
                }
            }

            if (workerMetric != nullptr)
            {
                workerMetric->update(workerPhaseUs);
            }

            const auto displayWorkerPhaseEnd =
                std::chrono::steady_clock::now();
            const std::int64_t displayOriginNs =
                displayTimelineOriginNs_.load(std::memory_order_acquire);
            const std::int64_t displayWorkerStartNs =
                static_cast<std::int64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        displayWorkerPhaseStart.time_since_epoch()).count());
            const std::uint64_t displayWorkerDurationUs =
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        displayWorkerPhaseEnd - displayWorkerPhaseStart).count());

            char displayPhaseLabel = '?';
            switch (phase)
            {
            case DisplayPhase::NoiseReduction: displayPhaseLabel = 'N'; break;
            case DisplayPhase::Deinterlace: displayPhaseLabel = 'D'; break;
            case DisplayPhase::ConvertFirst: displayPhaseLabel = '1'; break;
            case DisplayPhase::SpoutFirst: displayPhaseLabel = 'A'; break;
            case DisplayPhase::ConvertSecond: displayPhaseLabel = '2'; break;
            case DisplayPhase::SpoutSecond: displayPhaseLabel = 'B'; break;
            default: break;
            }

            if (displayPhaseLabel != '?' && workerIndex < 2u)
            {
                const std::uint64_t displayWorkerStartUs =
                    displayOriginNs > 0 && displayWorkerStartNs > displayOriginNs
                    ? static_cast<std::uint64_t>((displayWorkerStartNs - displayOriginNs) / 1000)
                    : 0u;

                performanceStats_.displayWorkerPhaseTimeline[workerIndex].append(
                    displayPhaseLabel,
                    static_cast<std::uint32_t>(std::min<std::uint64_t>(displayWorkerStartUs, 80'000u)),
                    static_cast<std::uint32_t>(std::min<std::uint64_t>(displayWorkerDurationUs, 80'000u)));
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

            continue;
        }

        // Capture-side frequency compensation has priority over waveform
        // assist.  It belongs to the NEW frame and must complete before that
        // frame is published to any consumer.  A previous frame's display
        // phase can still pre-empt us between F chunks.
        for (;;)
        {
            {
                std::lock_guard<std::mutex> lock(
                    displayPhaseMutex_);

                if (displayPhaseStop_)
                {
                    return;
                }

                if (displayPhaseGeneration_ !=
                    lastPhaseGeneration)
                {
                    break;
                }
            }

            if (!tryRunFrequencyAssistJob(
                static_cast<std::uint32_t>(workerIndex + 1u)))
            {
                break;
            }
        }

        // Help with small waveform stripes while the video pipeline is idle.
        // Check display generation between every stripe so a new field can
        // reclaim this worker promptly.
        //

        for (;;)
        {
            {
                std::lock_guard<std::mutex> lock(
                    displayPhaseMutex_);

                if (displayPhaseStop_)
                {
                    return;
                }

                if (displayPhaseGeneration_ !=
                    lastPhaseGeneration)
                {
                    break;
                }
            }

            if (!tryRunWaveformAssistJob(
                static_cast<std::uint32_t>(workerIndex + 1u),
                true))
            {
                break;
            }
        }

    }
}

void VideoEngine::runFrequencyCompensationJobs(
    CapturedFrameSlot& slot,
    int gainHundredthsDb)
{
    constexpr std::size_t kFrequencyJobCount = 6u;

    if (slot.frame.height <= 0 ||
        gainHundredthsDb <= 0)
    {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(
            frequencyAssistMutex_);

        frequencyAssistDoneCondition_.wait(
            lock,
            [this]()
            {
                return !frequencyAssistActive_;
            });

        frequencyAssistSlot_ = &slot;
        frequencyAssistGainHundredthsDb_ = gainHundredthsDb;
        frequencyAssistJobCount_ =
            std::min<std::size_t>(
                kFrequencyJobCount,
                static_cast<std::size_t>(slot.frame.height));
        frequencyAssistNextJob_ = 0u;
        frequencyAssistJobsRunning_ = 0u;
        frequencyAssistActive_ = true;
        frequencyAssistWorkAvailable_.store(
            frequencyAssistJobCount_ > 0u,
            std::memory_order_release);
        frequencyAssistGeneration_.fetch_add(
            1u,
            std::memory_order_acq_rel);
    }

    displayPhaseCondition_.notify_all();

    // W0 for F is the capture thread itself.  W1/W2 join whenever they are
    // not busy finishing display work from the previous frame.
    while (tryRunFrequencyAssistJob(0u))
    {
        std::this_thread::yield();
    }

    {
        std::unique_lock<std::mutex> lock(
            frequencyAssistMutex_);

        frequencyAssistDoneCondition_.wait(
            lock,
            [this]()
            {
                return
                    frequencyAssistNextJob_ >=
                    frequencyAssistJobCount_ &&
                    frequencyAssistJobsRunning_ == 0u;
            });

        frequencyAssistActive_ = false;
        frequencyAssistWorkAvailable_.store(
            false,
            std::memory_order_release);
        frequencyAssistSlot_ = nullptr;
    }

    frequencyAssistDoneCondition_.notify_all();
}

bool VideoEngine::tryRunFrequencyAssistJob(
    std::uint32_t workerId)
{
    // W1/W2 never interrupt the previous frame's display pipeline for F.
    // W0 is the capture thread and starts immediately.
    if (workerId > 0u &&
        displayPipelineActive_.load(std::memory_order_acquire))
    {
        return false;
    }

    CapturedFrameSlot* slot = nullptr;
    int gainHundredthsDb = 0;
    std::size_t jobIndex = 0u;
    std::size_t jobCount = 0u;

    {
        std::lock_guard<std::mutex> lock(
            frequencyAssistMutex_);

        if (!frequencyAssistActive_ ||
            frequencyAssistSlot_ == nullptr ||
            frequencyAssistNextJob_ >=
                frequencyAssistJobCount_)
        {
            return false;
        }

        slot = frequencyAssistSlot_;
        gainHundredthsDb = frequencyAssistGainHundredthsDb_;
        jobIndex = frequencyAssistNextJob_++;
        jobCount = frequencyAssistJobCount_;
        ++frequencyAssistJobsRunning_;

        frequencyAssistWorkAvailable_.store(
            frequencyAssistNextJob_ <
                frequencyAssistJobCount_,
            std::memory_order_release);
    }

    const int height = slot->frame.height;
    const int firstLine =
        static_cast<int>(
            (static_cast<std::int64_t>(jobIndex) *
                static_cast<std::int64_t>(height)) /
            static_cast<std::int64_t>(jobCount));
    const int lastLine =
        static_cast<int>(
            (static_cast<std::int64_t>(jobIndex + 1u) *
                static_cast<std::int64_t>(height)) /
            static_cast<std::int64_t>(jobCount));

    const auto start =
        std::chrono::steady_clock::now();

    lumaHighFrequencyCompensator_.processRange(
        slot->frame,
        gainHundredthsDb,
        firstLine,
        lastLine);

    const auto end =
        std::chrono::steady_clock::now();

    const std::int64_t originNs =
        slot->diagnosticOriginNs.load(
            std::memory_order_acquire);
    const std::int64_t startNs =
        static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                start.time_since_epoch()).count());
    const std::uint64_t startUs =
        originNs > 0 && startNs > originNs
        ? static_cast<std::uint64_t>(
            (startNs - originNs) / 1000)
        : 0u;
    const std::uint64_t durationUs =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                end - start).count());

    const std::size_t timelineIndex =
        std::min<std::size_t>(
            static_cast<std::size_t>(workerId),
            slot->frequencyAssistTimeline.size() - 1u);
    slot->frequencyAssistTimeline[timelineIndex].append(
        'F',
        static_cast<std::uint32_t>(
            std::min<std::uint64_t>(startUs, 80'000u)),
        static_cast<std::uint32_t>(
            std::min<std::uint64_t>(durationUs, 80'000u)));

    {
        std::lock_guard<std::mutex> lock(
            frequencyAssistMutex_);

        if (frequencyAssistJobsRunning_ > 0u)
        {
            --frequencyAssistJobsRunning_;
        }

        if (frequencyAssistNextJob_ >=
                frequencyAssistJobCount_ &&
            frequencyAssistJobsRunning_ == 0u)
        {
            frequencyAssistDoneCondition_.notify_all();
        }
    }

    return true;
}

bool VideoEngine::canDisplayWorkerAcceptAssist(
    std::size_t workerIndex) const
{
    if (workerIndex >= displayPhaseWorkers_.size())
    {
        return false;
    }

    /*
     * Hard ownership rule for the two display workers:
     *
     *   N / D / C1 / S1 / C2 / S2 first, waveform assist afterwards.
     *
     * W1/W2 may not claim R or X while the display pipeline for the current
     * captured frame is still active.  W0 remains independent and may keep
     * rasterising while the display workers finish the field pipeline.
     *
     * The normal display path clears displayPipelineActive_ after the full
     * display pipeline has completed and wakes the assist workers there, so
     * no predictive capture-time holdoff is needed here.
     */
    return !displayPipelineActive_.load(
        std::memory_order_acquire);
}

bool VideoEngine::tryRunWaveformAssistJob(
    std::uint32_t workerId,
    bool enforceDisplayHoldoff)
{
    if (enforceDisplayHoldoff)
    {
        if (workerId == 0u)
        {
            return false;
        }

        const std::size_t displayWorkerIndex =
            static_cast<std::size_t>(workerId - 1u);

        if (!canDisplayWorkerAcceptAssist(
            displayWorkerIndex))
        {
            return false;
        }
    }

    std::function<void(
        std::size_t,
        std::uint32_t)> job;
    std::size_t jobIndex = 0;
    char phaseLabel = '?';

    {
        std::lock_guard<std::mutex> lock(
            waveformAssistMutex_);

        if (!waveformAssistActive_ ||
            waveformAssistNextJob_ >=
            waveformAssistJobCount_ ||
            !waveformAssistJob_)
        {
            return false;
        }

        jobIndex =
            waveformAssistNextJob_++;

        waveformAssistWorkAvailable_.store(
            waveformAssistNextJob_ < waveformAssistJobCount_,
            std::memory_order_release);

        ++waveformAssistJobsRunning_;
        job = waveformAssistJob_;
        phaseLabel = waveformAssistPhaseLabel_;
    }

    // Wake the publisher as soon as a helper has actually claimed work.
    // This gives W1/W2 a deterministic head start instead of letting W0
    // drain the tiny chunk queue before the display workers leave their wait.
    waveformAssistDoneCondition_.notify_all();

    // The claimed job is never abandoned.  A display worker only checks its
    // higher-priority work before claiming the next chunk.  The renderer's
    // chunk dispatcher receives this one back as Done/Invalid itself.
    const auto assistStart =
        std::chrono::steady_clock::now();

    const std::uint64_t activeAssistGeneration =
        waveformAssistGeneration_.load(std::memory_order_acquire);

    traceLog(
        TraceEventType::AssistJobStart,
        activeAssistGeneration,
        workerId,
        static_cast<std::uint32_t>(jobIndex),
        0u,
        0u,
        TraceRendererId::PcWaveform);

    job(jobIndex, workerId);

    const auto assistEnd =
        std::chrono::steady_clock::now();

    const std::uint64_t assistDurationUs =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                assistEnd - assistStart).count());

    traceLog(
        TraceEventType::AssistJobEnd,
        activeAssistGeneration,
        workerId,
        static_cast<std::uint32_t>(jobIndex),
        assistDurationUs,
        0u,
        TraceRendererId::PcWaveform);

    if (workerId <= 2u)
    {
        const std::int64_t timelineOriginNs =
            waveformAssistTimelineOriginNs_.load(
                std::memory_order_acquire);

        const std::int64_t startNs =
            static_cast<std::int64_t>(
                std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                        assistStart.time_since_epoch())
                    .count());

        const std::uint64_t durationUs64 =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                        assistEnd - assistStart)
                    .count());

        const std::uint64_t startUs64 =
            timelineOriginNs > 0 && startNs > timelineOriginNs
            ? static_cast<std::uint64_t>(
                (startNs - timelineOriginNs) / 1000)
            : 0u;

        WaveformAssistTimelineStats* timeline =
            workerId == 0u
            ? &performanceStats_.waveformWorkerAssistTimeline
            : &performanceStats_.displayWorkerAssistTimeline[
                static_cast<std::size_t>(workerId - 1u)];

        timeline->append(
            phaseLabel,
            static_cast<std::uint32_t>(
                std::min<std::uint64_t>(startUs64, 80'000u)),
            static_cast<std::uint32_t>(
                std::min<std::uint64_t>(durationUs64, 80'000u)));
    }

    {
        std::lock_guard<std::mutex> lock(
            waveformAssistMutex_);

        if (waveformAssistJobsRunning_ > 0)
        {
            --waveformAssistJobsRunning_;
        }

        if (waveformAssistNextJob_ >=
            waveformAssistJobCount_ &&
            waveformAssistJobsRunning_ == 0)
        {
            waveformAssistDoneCondition_.
                notify_all();
        }
    }

    return true;
}

void VideoEngine::runWaveformTraceJobs(
    char phaseLabel,
    std::size_t jobCount,
    const std::function<void(
        std::size_t,
        std::uint32_t)>& job)
{
    QElapsedTimer assistTimer;
    assistTimer.start();

    const auto assistWallStart =
        std::chrono::steady_clock::now();
    const std::int64_t assistWallStartNs =
        static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                assistWallStart.time_since_epoch()).count());
    const std::int64_t assistCaptureNs =
        waveformAssistCaptureTickNs_.load(std::memory_order_acquire);
    const std::int64_t assistTimelineOriginNs =
        waveformAssistTimelineOriginNs_.load(std::memory_order_acquire);
    const std::uint64_t assistStartFromCaptureUs =
        assistTimelineOriginNs > 0 && assistWallStartNs > assistTimelineOriginNs
        ? static_cast<std::uint64_t>(
            (assistWallStartNs - assistTimelineOriginNs) / 1000)
        : 0u;

    performanceStats_.waveformScreenAssistTotalUs.store(0, std::memory_order_relaxed);
    performanceStats_.waveformScreenAssistFinalWaitStartUs.store(0, std::memory_order_relaxed);
    performanceStats_.waveformScreenAssistFinalWaitUs.store(0, std::memory_order_relaxed);

    std::uint64_t assistGeneration = 0;

    if (jobCount == 0 || !job)
    {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(
            waveformAssistMutex_);

        waveformAssistDoneCondition_.wait(
            lock,
            [this]()
            {
                return !waveformAssistActive_;
            });

        waveformAssistJob_ = job;
        waveformAssistPhaseLabel_ = phaseLabel;
        waveformAssistJobCount_ = jobCount;
        waveformAssistNextJob_ = 0;
        waveformAssistJobsRunning_ = 0;
        waveformAssistActive_ = true;
        waveformAssistWorkAvailable_.store(
            jobCount > 0u,
            std::memory_order_release);

        assistGeneration =
            waveformAssistGeneration_.fetch_add(
                1,
                std::memory_order_acq_rel) + 1u;

        traceLog(
            TraceEventType::AssistBegin,
            assistGeneration,
            0u,
            0u,
            static_cast<std::uint64_t>(jobCount),
            0u,
            TraceRendererId::PcWaveform);

        const std::uint64_t timelineGeneration =
            assistCaptureNs > 0
            ? static_cast<std::uint64_t>(assistCaptureNs)
            : assistGeneration;

        if (performanceStats_.waveformWorkerAssistTimeline.generation.load(
                std::memory_order_acquire) != timelineGeneration)
        {
            performanceStats_.waveformWorkerAssistTimeline.reset(timelineGeneration);
            for (auto& timeline : performanceStats_.displayWorkerAssistTimeline)
            {
                timeline.reset(timelineGeneration);
            }
        }
    }

    displayPhaseCondition_.notify_all();

    // W0 starts immediately.  Helpers join opportunistically when display
    // work/holdoff allows it; there is no artificial launch delay anymore.
    // The renderer now uses linear chunks and direct target strips, so early
    // W0 progress is useful work instead of queue stealing.

    // Worker id 0 is the waveform thread itself.  It consumes exactly the
    // same queue and is never subject to display holdoff.  Yield between
    // chunks so a helper that becomes runnable while W0 is rasterising keeps
    // getting a fair chance to claim the next small chunk.
    while (tryRunWaveformAssistJob(0u, false))
    {
        std::this_thread::yield();
    }

    {
        std::unique_lock<std::mutex> lock(
            waveformAssistMutex_);

        const std::uint64_t finalWaitStartUs =
            static_cast<std::uint64_t>(assistTimer.nsecsElapsed() / 1000);

        traceLog(
            TraceEventType::AssistWaitBegin,
            assistGeneration,
            0u,
            0u,
            0u,
            0u,
            TraceRendererId::PcWaveform);

        waveformAssistDoneCondition_.wait(
            lock,
            [this]()
            {
                return
                    waveformAssistNextJob_ >=
                    waveformAssistJobCount_ &&
                    waveformAssistJobsRunning_ == 0;
            });

        const std::uint64_t finalWaitEndUs =
            static_cast<std::uint64_t>(assistTimer.nsecsElapsed() / 1000);

        traceLog(
            TraceEventType::AssistWaitEnd,
            assistGeneration,
            0u,
            0u,
            finalWaitEndUs - finalWaitStartUs,
            0u,
            TraceRendererId::PcWaveform);

        performanceStats_.waveformScreenAssistFinalWaitStartUs.store(
            assistStartFromCaptureUs + finalWaitStartUs,
            std::memory_order_relaxed);
        performanceStats_.waveformScreenAssistFinalWaitUs.store(
            finalWaitEndUs - finalWaitStartUs,
            std::memory_order_relaxed);

        waveformAssistActive_ = false;
        waveformAssistWorkAvailable_.store(
            false,
            std::memory_order_release);
        waveformAssistJob_ = {};
    }

    performanceStats_.waveformScreenAssistTotalUs.store(
        static_cast<std::uint64_t>(assistTimer.nsecsElapsed() / 1000),
        std::memory_order_relaxed);

    waveformAssistCompletedCaptureTickNs_.store(
        assistCaptureNs,
        std::memory_order_release);
    waveformAssistCompletedGeneration_.store(
        assistGeneration,
        std::memory_order_release);

    waveformAssistDoneCondition_.notify_all();
}

void VideoEngine::runDisplayPhase(
    DisplayPhase phase)
{
    const auto phaseWallStart =
        std::chrono::steady_clock::now();

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

    lock.unlock();

    const auto phaseWallEnd =
        std::chrono::steady_clock::now();
    const std::int64_t displayOriginNs =
        displayTimelineOriginNs_.load(std::memory_order_acquire);
    const std::int64_t phaseStartNs =
        static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                phaseWallStart.time_since_epoch()).count());
    const std::uint64_t phaseStartUs =
        displayOriginNs > 0 && phaseStartNs > displayOriginNs
        ? static_cast<std::uint64_t>((phaseStartNs - displayOriginNs) / 1000)
        : 0u;
    const std::uint64_t phaseDurationUs =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                phaseWallEnd - phaseWallStart).count());

    char phaseLabel = '?';
    switch (phase)
    {
    case DisplayPhase::NoiseReduction: phaseLabel = 'N'; break;
    case DisplayPhase::Deinterlace: phaseLabel = 'D'; break;
    case DisplayPhase::ConvertFirst: phaseLabel = '1'; break;
    case DisplayPhase::SpoutFirst: phaseLabel = 'A'; break;
    case DisplayPhase::ConvertSecond: phaseLabel = '2'; break;
    case DisplayPhase::SpoutSecond: phaseLabel = 'B'; break;
    default: break;
    }

    if (phaseLabel != '?')
    {
        performanceStats_.displayFieldPhaseTimeline.append(
            phaseLabel,
            static_cast<std::uint32_t>(std::min<std::uint64_t>(phaseStartUs, 80'000u)),
            static_cast<std::uint32_t>(std::min<std::uint64_t>(phaseDurationUs, 80'000u)));
    }
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

        auto& captureSlot =
            captureSlots_[
                static_cast<std::size_t>(
                    captureSlotIndex)];

        const bool screenRenderEnabled =
            videoScreenRenderEnabled_.load(
                std::memory_order_acquire);

        const bool spoutRenderEnabled =
            spoutVideoEnabled_.load(
                std::memory_order_acquire);

        // Keep the worker timeline honest when a consumer is disabled.
        // ConvertFirst/ConvertSecond are screen-only work; SpoutFirst/
        // SpoutSecond are Spout-only work.  The actual phases are already
        // gated below, but without clearing their metrics the performance
        // window keeps showing the most recent non-zero C1/C2/S1/S2 sample
        // and makes idle workers look busy.
        if (!screenRenderEnabled)
        {
            performanceStats_.displayWorker0Convert1.update(0);
            performanceStats_.displayWorker0Convert2.update(0);
            performanceStats_.displayWorker1Convert1.update(0);
            performanceStats_.displayWorker1Convert2.update(0);
        }

        if (!spoutRenderEnabled)
        {
            performanceStats_.displayWorker0Spout1.update(0);
            performanceStats_.displayWorker0Spout2.update(0);
            performanceStats_.displayWorker1Spout1.update(0);
            performanceStats_.displayWorker1Spout2.update(0);
        }

        if (!screenRenderEnabled &&
            !spoutRenderEnabled)
        {
            /*
             * No video consumer exists for this frame.
             *
             * There is therefore no reason to run Noise Reduction,
             * deinterlacing or any C/S conversion phase.  More importantly,
             * the two display workers must be considered immediately FREE
             * for waveform R/X assist.
             *
             * Do not just `continue` here with a possibly stale
             * displayPipelineActive_ state: when PC video is hidden
             * (e.g. fullscreen instrument) and Spout video is also disabled,
             * that stale ownership bit can keep W1/W2 away from the waveform
             * queue indefinitely.
             */
            displayPipelineActive_.store(
                false,
                std::memory_order_release);

            performanceStats_.displayWorker0Noise.update(0);
            performanceStats_.displayWorker1Noise.update(0);
            performanceStats_.displayWorker0Deinterlace.update(0);
            performanceStats_.displayWorker1Deinterlace.update(0);
            performanceStats_.deinterlaceWorker0.update(0);
            performanceStats_.deinterlaceWorker1.update(0);
            performanceStats_.noiseReduction.update(0);
            performanceStats_.deinterlace.update(0);
            performanceStats_.videoScreen.update(0);
            performanceStats_.displayFirst.update(0);
            performanceStats_.displaySecond.update(0);
            performanceStats_.displayCompose.update(0);
            performanceStats_.spoutConvertFirst.update(0);
            performanceStats_.spoutConvertSecond.update(0);

            // Publish an empty display timeline for this capture so the
            // Performance floaty also shows that N/D/C/S did not run.
            const std::int64_t noVideoCaptureNs =
                captureSlot.captureTickNs.load(
                    std::memory_order_acquire);

            const std::uint64_t noVideoTimelineGeneration =
                noVideoCaptureNs > 0
                ? static_cast<std::uint64_t>(noVideoCaptureNs)
                : generation;

            performanceStats_.displayFieldPhaseTimeline.reset(
                noVideoTimelineGeneration);
            performanceStats_.displayWorkerPhaseTimeline[0].reset(
                noVideoTimelineGeneration);
            performanceStats_.displayWorkerPhaseTimeline[1].reset(
                noVideoTimelineGeneration);

            const std::uint32_t noVideoFrequencyCompensationUs =
                captureSlot.frequencyCompensationUs.load(
                    std::memory_order_acquire);
            if (noVideoFrequencyCompensationUs > 0u)
            {
                performanceStats_.displayFieldPhaseTimeline.append(
                    'F',
                    0u,
                    noVideoFrequencyCompensationUs);
            }

            /*
             * The floaty reads the PUBLISHED display timelines, not these
             * live accumulators.  Publish the empty generation immediately;
             * otherwise the last N/D/C/S frame remains frozen on screen and
             * the worker R/X overlay is rejected as a generation mismatch.
             */
            performanceStats_.publishDisplayDiagnosticTimelines();

            /*
             * A helper may already have observed the current assist
             * generation while display ownership was still active.  Bump the
             * wake generation and notify immediately so W1/W2 re-check the
             * SAME pending R/X queue.
             */
            waveformAssistGeneration_.fetch_add(
                1,
                std::memory_order_release);
            displayPhaseCondition_.notify_all();

            continue;
        }

        displayPipelineActive_.store(
            true,
            std::memory_order_release);

        const std::int64_t displayCaptureNs =
            captureSlot.captureTickNs.load(std::memory_order_acquire);
        const std::int64_t displayTimelineOriginNs =
            captureSlot.diagnosticOriginNs.load(
                std::memory_order_acquire);
        displayTimelineCaptureNs_.store(
            displayCaptureNs,
            std::memory_order_release);
        displayTimelineOriginNs_.store(
            displayTimelineOriginNs,
            std::memory_order_release);
        // Use the capture timestamp itself as the diagnostic generation.
        // Waveform R/X timelines use the same identity, so the Performance
        // floaty can only combine events that belong to the exact same frame.
        const std::uint64_t displayTimelineGeneration =
            displayCaptureNs > 0
            ? static_cast<std::uint64_t>(displayCaptureNs)
            : generation;
        performanceStats_.displayFieldPhaseTimeline.reset(displayTimelineGeneration);
        performanceStats_.displayWorkerPhaseTimeline[0].reset(displayTimelineGeneration);
        performanceStats_.displayWorkerPhaseTimeline[1].reset(displayTimelineGeneration);

        const std::uint32_t displayFrequencyCompensationUs =
            captureSlot.frequencyCompensationUs.load(
                std::memory_order_acquire);
        if (displayFrequencyCompensationUs > 0u)
        {
            performanceStats_.displayFieldPhaseTimeline.append(
                'F',
                0u,
                displayFrequencyCompensationUs);
        }

        for (auto& workerUs :
            displayCurrentFrameWorkerUs_)
        {
            workerUs.store(
                0u,
                std::memory_order_relaxed);
        }

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
            if (noiseReducedFrame_.width !=
                    captureSlot.frame.width ||
                noiseReducedFrame_.height !=
                    captureSlot.frame.height)
            {
                noiseReducedFrame_.resize(
                    captureSlot.frame.width,
                    captureSlot.frame.height);
            }

            noiseReducedFrame_.sampleClockHz =
                captureSlot.frame.sampleClockHz;

            displayNoiseSource_ =
                &captureSlot.frame;

            displayNoiseDestination_ =
                &noiseReducedFrame_;

            displayNoiseIntensity_ =
                noiseReductionIntensity;

            runDisplayPhase(
                DisplayPhase::NoiseReduction);

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

        const bool videoLineHighlightEnabled =
            videoLineHighlightEnabled_.load(
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
                    videoLineHighlightEnabled
                    ? selectedLine
                    : -1);
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
            displayPipelineActive_.store(
                false,
                std::memory_order_release);
            waveformAssistGeneration_.fetch_add(
                1,
                std::memory_order_release);
            displayPhaseCondition_.notify_all();
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

        QImage firstDisplayImage;
        std::array<DisplayPerformance, 2> firstPerformance{};
        std::uint64_t firstConvertUs = 0;

        if (screenRenderEnabled)
        {
            firstDisplayImage = QImage(
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

            firstConvertUs =
                static_cast<std::uint64_t>(
                    phaseTimer.nsecsElapsed() /
                    1000);

            firstPerformance =
                displayPhasePerformance_;
        }

        performanceStats_.displayFirst.update(
            firstConvertUs);

        QImage firstSpoutImage;
        QImage secondSpoutImage;
        std::uint64_t firstSpoutConvertUs = 0;
        std::uint64_t secondSpoutConvertUs = 0;

        // Field 1 has the earlier deadline. Finish every Field 1 buffer
        // before starting any Field 2 conversion:
        //     N -> D -> C1 -> S1 -> C2 -> S2
        if (spoutRenderEnabled)
        {
            firstSpoutImage = QImage(
                kCaptureWidth,
                kCaptureHeight,
                QImage::Format_RGB32);

            firstSpoutImage.detach();

            displayPhaseFrame_ =
                displayFrame;

            displayPhaseLuma_ =
                progressiveLuma_.first.y.data();

            displayPhaseOutputPixels_ =
                reinterpret_cast<QRgb*>(
                    firstSpoutImage.bits());

            displayPhaseOutputStridePixels_ =
                firstSpoutImage.bytesPerLine() /
                static_cast<int>(sizeof(QRgb));

            displayPhaseOutputWidth_ =
                kCaptureWidth;

            displayPhaseOutputHeight_ =
                kCaptureHeight;

            phaseTimer.restart();

            runDisplayPhase(
                DisplayPhase::SpoutFirst);

            firstSpoutConvertUs =
                static_cast<std::uint64_t>(
                    phaseTimer.nsecsElapsed() /
                    1000);
        }

        performanceStats_.spoutConvertFirst.update(
            firstSpoutConvertUs);

        const auto firstReadyWallTime =
            std::chrono::steady_clock::now();

        const std::uint64_t firstReadyUs =
            displayTimelineOriginNs > 0
            ? static_cast<std::uint64_t>(
                std::max<std::int64_t>(
                    0,
                    (static_cast<std::int64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            firstReadyWallTime.time_since_epoch()).count()) -
                     displayTimelineOriginNs) / 1000))
            : 0u;

        {
            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            DisplayFrameSlot& slot =
                displayFrameSlots_[
                    static_cast<std::size_t>(
                        generation %
                        kFrameSlotCount)];

            slot.first =
                std::move(firstDisplayImage);

            slot.second =
                QImage{};

            slot.spoutFirst =
                std::move(firstSpoutImage);

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

        QImage secondDisplayImage;
        std::array<DisplayPerformance, 2> secondPerformance{};
        std::uint64_t secondConvertUs = 0;

        if (screenRenderEnabled)
        {
            secondDisplayImage = QImage(
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

            displayPhaseOutputWidth_ =
                outputWidth;

            displayPhaseOutputHeight_ =
                outputHeight;

            phaseTimer.restart();

            runDisplayPhase(
                DisplayPhase::ConvertSecond);

            secondConvertUs =
                static_cast<std::uint64_t>(
                    phaseTimer.nsecsElapsed() /
                    1000);

            secondPerformance =
                displayPhasePerformance_;
        }

        performanceStats_.displaySecond.update(
            secondConvertUs);

        if (spoutRenderEnabled)
        {
            secondSpoutImage = QImage(
                kCaptureWidth,
                kCaptureHeight,
                QImage::Format_RGB32);

            secondSpoutImage.detach();

            displayPhaseFrame_ =
                displayFrame;

            displayPhaseLuma_ =
                progressiveLuma_.second.y.data();

            displayPhaseOutputPixels_ =
                reinterpret_cast<QRgb*>(
                    secondSpoutImage.bits());

            displayPhaseOutputStridePixels_ =
                secondSpoutImage.bytesPerLine() /
                static_cast<int>(sizeof(QRgb));

            displayPhaseOutputWidth_ =
                kCaptureWidth;

            displayPhaseOutputHeight_ =
                kCaptureHeight;

            phaseTimer.restart();

            runDisplayPhase(
                DisplayPhase::SpoutSecond);

            secondSpoutConvertUs =
                static_cast<std::uint64_t>(
                    phaseTimer.nsecsElapsed() /
                    1000);
        }

        performanceStats_.spoutConvertSecond.update(
            secondSpoutConvertUs);

        /*
         * All physical display-worker phases for this captured frame are now
         * complete.  Release W1/W2 to waveform assist HERE, before presenter
         * publication, stats folding, deadline bookkeeping and other serial
         * display-thread administration.
         *
         * This is the deterministic ownership boundary the scheduler needs:
         *     N/D/C1/S1/C2/S2 -> assist allowed
         *
         * Releasing only at the end of displayWorkerLoop() made helper
         * availability timing-dependent: sometimes W0 had already consumed
         * every remaining R chunk, while on slower frames W1/W2 happened to
         * catch the tail of the same R queue.
         */
        displayPipelineActive_.store(
            false,
            std::memory_order_release);

        // Helpers may have observed the current assist generation while they
        // were still owned by display work.  Bump the wake generation and
        // notify them immediately so they can rejoin that SAME R/X queue.
        waveformAssistGeneration_.fetch_add(
            1,
            std::memory_order_release);
        displayPhaseCondition_.notify_all();

        const auto secondReadyWallTime =
            std::chrono::steady_clock::now();

        const std::uint64_t secondReadyUs =
            displayTimelineOriginNs > 0
            ? static_cast<std::uint64_t>(
                std::max<std::int64_t>(
                    0,
                    (static_cast<std::int64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            secondReadyWallTime.time_since_epoch()).count()) -
                     displayTimelineOriginNs) / 1000))
            : 0u;

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
                    std::move(secondDisplayImage);

                slot.spoutSecond =
                    std::move(secondSpoutImage);

                slot.secondReady =
                    true;
            }
        }

        displayPresenterCondition_.
            notify_one();

        performanceStats_.videoScreen.update(
            firstConvertUs + secondConvertUs);

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

        // Publish one coherent completed display timeline only after both
        // field ready timestamps and all N/D/C/S phases belong to this frame.
        performanceStats_.publishDisplayDiagnosticTimelines();

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

        // One full display pipeline has completed.  Fold each worker's
        // measured N/D/C/S time into a light EMA and derive future holdoff
        // from that value (+50% in canDisplayWorkerAcceptAssist()).
        for (std::size_t workerIndex = 0;
            workerIndex < displayPhaseWorkers_.size();
            ++workerIndex)
        {
            const std::uint64_t measuredUs =
                displayCurrentFrameWorkerUs_[
                    workerIndex].load(
                        std::memory_order_relaxed);

            if (measuredUs > 0u)
            {
                const std::uint64_t oldEstimateUs =
                    displayAssistWorkEstimateUs_[
                        workerIndex].load(
                            std::memory_order_relaxed);

                // EMA alpha = 0.20: responsive to sustained load, but not
                // jerked around by a single scheduler hiccup.
                const std::uint64_t newEstimateUs =
                    (oldEstimateUs * 4u + measuredUs) / 5u;

                displayAssistWorkEstimateUs_[
                    workerIndex].store(
                        newEstimateUs,
                        std::memory_order_relaxed);
            }
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

    constexpr auto framePeriod =
        fieldPeriod * 2;

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

    // Capture supplies data at 25 fps, but it is no longer the output
    // clock. Once started, this presenter owns one continuous 20 ms
    // field cadence. Both the PC video present and the 50 fps video
    // Spout dispatch are emitted from these exact same F1/F2 ticks.
    Clock::time_point nextFirstFieldTime{};
    std::uint64_t nextGeneration = 0;

    {
        std::unique_lock<std::mutex> lock(
            displayPresenterMutex_);

        displayPresenterCondition_.wait(
            lock,
            [this]()
            {
                return
                    displayPresenterStop_ ||
                    latestCaptureTickGeneration_ > 1;
            });

        if (!displayPresenterStop_)
        {
            nextGeneration =
                latestCaptureTickGeneration_ - 1;

            nextFirstFieldTime =
                latestCaptureTickTime_;

            presenterLastTickGeneration_ =
                latestCaptureTickGeneration_;
        }
    }

    while (nextFirstFieldTime !=
        Clock::time_point{})
    {
        waitUntil(
            nextFirstFieldTime);

        const auto firstTickLateUs =
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                    Clock::now() -
                    nextFirstFieldTime)
                .count();

        traceLog(
            TraceEventType::PresenterField1Tick,
            nextGeneration,
            0u,
            0u,
            static_cast<std::uint64_t>(
                std::max<std::int64_t>(
                    firstTickLateUs,
                    0)),
            0u);

        QImage firstImage;
        QImage secondImage;
        QImage firstSpoutImage;
        QImage secondSpoutImage;

        bool presentingNewGeneration = false;
        bool haveFirst = false;

        {
            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            if (displayPresenterStop_)
            {
                break;
            }

            // Do not get permanently stuck on a generation that was
            // skipped/overwritten while switching source or while a worker
            // was late.  Move forward to the newest ready generation in
            // the small presenter ring, but never backwards.
            std::uint64_t readyGeneration =
                nextGeneration;

            bool exactGenerationReady = false;

            for (const DisplayFrameSlot& candidate :
                displayFrameSlots_)
            {
                if (!candidate.firstReady ||
                    candidate.generation < nextGeneration)
                {
                    continue;
                }

                if (candidate.generation == nextGeneration)
                {
                    exactGenerationReady = true;
                    readyGeneration = nextGeneration;
                    break;
                }

                readyGeneration =
                    std::max(
                        readyGeneration,
                        candidate.generation);
            }

            if (!exactGenerationReady &&
                readyGeneration > nextGeneration)
            {
                nextGeneration = readyGeneration;
            }

            DisplayFrameSlot& slot =
                displayFrameSlots_[
                    static_cast<std::size_t>(
                        nextGeneration %
                        kFrameSlotCount)];

            if (slot.generation ==
                    nextGeneration &&
                slot.firstReady)
            {
                firstImage =
                    slot.first;

                firstSpoutImage =
                    slot.spoutFirst;

                lastPresentedFirst_ =
                    firstImage;

                lastPresentedSpoutFirst_ =
                    firstSpoutImage;

                presentingNewGeneration =
                    true;

                haveFirst =
                    true;
            }
            else if (lastPresentedPairValid_)
            {
                firstImage =
                    lastPresentedFirst_;

                secondImage =
                    lastPresentedSecond_;

                firstSpoutImage =
                    lastPresentedSpoutFirst_;

                secondSpoutImage =
                    lastPresentedSpoutSecond_;

                haveFirst =
                    true;
            }
        }

        if (!haveFirst)
        {
            // Keep the master clock running during startup. Retry the
            // same generation on the next 40 ms pair instead of letting
            // a capture callback redefine the field phase.
            nextFirstFieldTime +=
                framePeriod;

            continue;
        }

        recordPresentInterval();

        {
            const auto lateUs =
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                        Clock::now() -
                        nextFirstFieldTime)
                    .count();

            performanceStats_.field1Present.update(
                40000u +
                static_cast<std::uint64_t>(
                    std::max<std::int64_t>(
                        lateUs,
                        0)));
        }

        if (videoScreenRenderEnabled_.load(
                std::memory_order_acquire) &&
            !firstImage.isNull())
        {
            emit frameChanged(
                firstImage);
        }

        if (spoutVideoEnabled_.load(
                std::memory_order_acquire) &&
            !firstSpoutImage.isNull())
        {
            const qint64 dispatchTimestampUs =
                static_cast<qint64>(
                    std::chrono::duration_cast<
                        std::chrono::microseconds>(
                            Clock::now().time_since_epoch())
                        .count());

            emit videoSpoutChanged(
                firstSpoutImage,
                dispatchTimestampUs);
        }

        const Clock::time_point secondFieldTime =
            nextFirstFieldTime +
            fieldPeriod;

        waitUntil(
            secondFieldTime);

        const auto secondTickLateUs =
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                    Clock::now() -
                    secondFieldTime)
                .count();

        traceLog(
            TraceEventType::PresenterField2Tick,
            nextGeneration,
            0u,
            0u,
            static_cast<std::uint64_t>(
                std::max<std::int64_t>(
                    secondTickLateUs,
                    0)),
            0u);

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
                            nextGeneration %
                            kFrameSlotCount)];

                if (slot.generation ==
                        nextGeneration &&
                    slot.secondReady)
                {
                    secondImage =
                        slot.second;

                    secondSpoutImage =
                        slot.spoutSecond;

                    lastPresentedSecond_ =
                        secondImage;

                    lastPresentedSpoutSecond_ =
                        secondSpoutImage;

                    lastPresentedPairValid_ =
                        true;
                }
                else if (lastPresentedPairValid_)
                {
                    secondImage =
                        lastPresentedSecond_;

                    secondSpoutImage =
                        lastPresentedSpoutSecond_;
                }
                else
                {
                    secondImage =
                        firstImage;

                    secondSpoutImage =
                        firstSpoutImage;
                }
            }
        }

        if (!secondImage.isNull() ||
            !secondSpoutImage.isNull())
        {
            recordPresentInterval();

            {
                const auto lateUs =
                    std::chrono::duration_cast<
                        std::chrono::microseconds>(
                            Clock::now() -
                            secondFieldTime)
                        .count();

                performanceStats_.field2Present.update(
                    60000u +
                    static_cast<std::uint64_t>(
                        std::max<std::int64_t>(
                            lateUs,
                            0)));
            }

            if (videoScreenRenderEnabled_.load(
                    std::memory_order_acquire) &&
                !secondImage.isNull())
            {
                emit frameChanged(
                    secondImage);
            }

            if (spoutVideoEnabled_.load(
                    std::memory_order_acquire) &&
                !secondSpoutImage.isNull())
            {
                const qint64 dispatchTimestampUs =
                    static_cast<qint64>(
                        std::chrono::duration_cast<
                            std::chrono::microseconds>(
                                Clock::now().time_since_epoch())
                            .count());

                emit videoSpoutChanged(
                    secondSpoutImage,
                    dispatchTimestampUs);
            }
        }

        if (presentingNewGeneration)
        {
            ++nextGeneration;
        }

        nextFirstFieldTime +=
            framePeriod;

        // If the machine was suspended/stalled for a long time, do not
        // run a burst of expired timer ticks. Re-anchor once, then return
        // immediately to the normal fixed 20 ms cadence.
        const Clock::time_point now =
            Clock::now();

        if (now >
            nextFirstFieldTime +
                framePeriod)
        {
            std::lock_guard<std::mutex> lock(
                displayPresenterMutex_);

            if (latestCaptureTickGeneration_ > 1)
            {
                nextGeneration =
                    latestCaptureTickGeneration_ - 1;

                nextFirstFieldTime =
                    now +
                    fieldPeriod;
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

        const auto waveformWorkerStart =
            std::chrono::steady_clock::now();

        const std::int64_t waveformDiagnosticOriginNs =
            captureSlot.diagnosticOriginNs.load(
                std::memory_order_acquire);

        const auto waveformPhaseAppend =
            [&](char label,
                const std::chrono::steady_clock::time_point& phaseStart,
                const std::chrono::steady_clock::time_point& phaseEnd)
            {
                if (!waveformScreenRenderEnabled_.load(
                        std::memory_order_acquire) ||
                    waveformDiagnosticOriginNs <= 0 ||
                    phaseEnd <= phaseStart)
                {
                    return;
                }

                const auto startNs =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        phaseStart.time_since_epoch()).count();
                const auto durationUs =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        phaseEnd - phaseStart).count();

                const std::uint64_t startUs =
                    startNs > waveformDiagnosticOriginNs
                    ? static_cast<std::uint64_t>(
                        (startNs - waveformDiagnosticOriginNs) / 1000)
                    : 0u;

                performanceStats_.waveformScreenPhaseTimeline.append(
                    label,
                    static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(startUs, 80'000u)),
                    static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(
                            static_cast<std::uint64_t>(durationUs),
                            80'000u)));
            };

        traceLog(
            TraceEventType::WaveformWorkerBegin,
            generation,
            0u,
            static_cast<std::uint32_t>(
                captureSlotIndex),
            0u,
            0u,
            TraceRendererId::PcWaveform);

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

        const int waveformCoreIntensity =
            waveformCoreIntensity_.load(
                std::memory_order_acquire);

        const int waveformCoreWidthTenths =
            waveformCoreWidthTenths_.load(
                std::memory_order_acquire);

        const int glow =
            vectorscopeGlow_.load(
                std::memory_order_acquire);

        const bool screenRenderEnabled =
            waveformScreenRenderEnabled_.load(
                std::memory_order_acquire);

        if (screenRenderEnabled)
        {
            waveformScreenRenderer_.setOutputSize(
                screenOutputWidth,
                screenOutputHeight);

            waveformScreenRenderer_.setFitAspectRatio(true);

            waveformScreenRenderer_.setLineInfoOverlayEnabled(true, false);

            waveformScreenRenderer_.setContentScale(
                1.0,
                1.0);

            waveformScreenRenderer_.setSelectedLine(
                selectedLine);

            waveformScreenRenderer_.setPersistence(
                waveformPersistence);

            waveformScreenRenderer_.setCoreIntensity(
                waveformCoreIntensity);

            waveformScreenRenderer_.setCoreWidth(
                waveformCoreWidthTenths);


            waveformScreenRenderer_.setGlow(
                glow);

            QElapsedTimer screenTimer;
            screenTimer.start();

            const auto waveformWallStart =
                std::chrono::steady_clock::now();
            const std::int64_t waveformWallStartNs =
                static_cast<std::int64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        waveformWallStart.time_since_epoch()).count());

            // Freeze the exact capture identity/time origin of the slot that
            // this waveform render is actually analysing.  Never use the
            // global latest-capture clock here: a newer frame may arrive while
            // this worker is still rendering the previous slot, which would
            // shift R/X onto another frame's display timeline and can make one
            // physical worker appear to execute two jobs at once.
            const std::int64_t waveformTimelineCaptureNs =
                captureSlot.captureTickNs.load(std::memory_order_acquire);
            const std::int64_t waveformTimelineOriginNs =
                captureSlot.diagnosticOriginNs.load(
                    std::memory_order_acquire);
            waveformAssistCaptureTickNs_.store(
                waveformTimelineCaptureNs,
                std::memory_order_release);
            waveformAssistTimelineOriginNs_.store(
                waveformTimelineOriginNs,
                std::memory_order_release);

            // Seed the R/X assist timelines with the capture-side F chunks
            // from this exact frame.  The waveform worker is sequential, so
            // the previous frame has already published before these live
            // timelines are reset.  R and X will append to the same generation.
            const std::uint64_t assistTimelineGeneration =
                waveformTimelineCaptureNs > 0
                ? static_cast<std::uint64_t>(waveformTimelineCaptureNs)
                : generation;

            performanceStats_.waveformWorkerAssistTimeline.reset(
                assistTimelineGeneration);
            performanceStats_.displayWorkerAssistTimeline[0].reset(
                assistTimelineGeneration);
            performanceStats_.displayWorkerAssistTimeline[1].reset(
                assistTimelineGeneration);

            const std::array<WaveformAssistTimelineStats*, 3> targetAssist =
            {
                &performanceStats_.waveformWorkerAssistTimeline,
                &performanceStats_.displayWorkerAssistTimeline[0],
                &performanceStats_.displayWorkerAssistTimeline[1]
            };

            for (std::size_t assistWorker = 0u;
                assistWorker < targetAssist.size();
                ++assistWorker)
            {
                const auto frequencySnapshot =
                    captureSlot.frequencyAssistTimeline[assistWorker].snapshot();
                for (std::uint32_t eventIndex = 0u;
                    eventIndex < frequencySnapshot.count;
                    ++eventIndex)
                {
                    const auto& event = frequencySnapshot.events[eventIndex];
                    targetAssist[assistWorker]->append(
                        static_cast<char>(event.phase),
                        event.startUs,
                        event.durationUs);
                }
            }

            waveformScreenRenderer_.analyze(
                captureSlot.frame);

            if constexpr (OpenScopeBuild::kDebugBuild)
            {
                captureWaveformRawFrame(
                    waveformScreenRenderer_.reconstructedLumaSamples(),
                    generation,
                    selectedLine);
            }

            performanceStats_.waveformScreen.update(
                static_cast<std::uint64_t>(
                    screenTimer.nsecsElapsed() /
                    1000));

            const auto& timings =
                waveformScreenRenderer_.renderTimings();

            const std::uint64_t waveformTimelineGeneration =
                waveformTimelineCaptureNs > 0
                ? static_cast<std::uint64_t>(waveformTimelineCaptureNs)
                : waveformAssistCompletedGeneration_.load(
                    std::memory_order_acquire);
            const std::uint64_t waveformStartFromCaptureUs =
                waveformTimelineOriginNs > 0 &&
                waveformWallStartNs > waveformTimelineOriginNs
                ? static_cast<std::uint64_t>(
                    (waveformWallStartNs - waveformTimelineOriginNs) / 1000)
                : 0u;
            performanceStats_.waveformScreenPhaseTimeline.reset(
                waveformTimelineGeneration);

            const std::uint32_t waveformFrequencyCompensationUs =
                captureSlot.frequencyCompensationUs.load(
                    std::memory_order_acquire);
            if (waveformFrequencyCompensationUs > 0u)
            {
                performanceStats_.waveformScreenPhaseTimeline.append(
                    'F',
                    0u,
                    waveformFrequencyCompensationUs);
            }

            const std::uint64_t waveformWallUs =
                static_cast<std::uint64_t>(
                    screenTimer.nsecsElapsed() / 1000);
            performanceStats_.waveformScreenPhaseTimeline.append(
                'X',
                static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(waveformStartFromCaptureUs, 80'000u)),
                static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(waveformWallUs, 80'000u)));
            for (std::uint32_t phaseIndex = 0;
                phaseIndex < timings.phaseCount &&
                phaseIndex < WaveformRenderTimings::kPhaseCapacity;
                ++phaseIndex)
            {
                const auto& phase = timings.phases[phaseIndex];
                performanceStats_.waveformScreenPhaseTimeline.append(
                    phase.label,
                    static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(
                            waveformStartFromCaptureUs + phase.startUs,
                            80'000u)),
                    static_cast<std::uint32_t>(
                        std::min<std::uint64_t>(phase.durationUs, 80'000u)));
            }

            performanceStats_.waveformScreenPersistence.update(
                timings.persistenceUs);
            performanceStats_.waveformScreenTrace.update(
                timings.traceUs);
            performanceStats_.waveformScreenTracePrep.update(
                timings.tracePrepUs);
            performanceStats_.waveformScreenTraceRaster.update(
                timings.traceRasterUs);
            performanceStats_.waveformScreenCompose.update(
                timings.composeUs);
            performanceStats_.waveformScreenGlow.update(
                timings.glowUs);
            performanceStats_.waveformScreenOverlay.update(
                timings.overlayUs);
            performanceStats_.waveformScreenTraceParallel.store(
                timings.traceParallel,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenOutputSizeChanged.store(
                timings.outputSizeChanged,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenOutputBufferCapacityGrew.store(
                timings.outputBufferCapacityGrew,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenResamplerCacheRebuilt.store(
                timings.resamplerCacheRebuilt,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenTraceJobCount.store(
                timings.traceJobCount,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenCatWuzleChunkCount.store(
                timings.catWuzleChunkCount,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenCatWuzleInvalidChunkCount.store(
                timings.catWuzleInvalidChunkCount,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenCatWuzleZipperUs.store(
                timings.catWuzleZipperUs,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenCatWuzleChunkRenderMinUs.store(
                timings.catWuzleChunkRenderMinUs,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenCatWuzleChunkRenderAvgUs.store(
                timings.catWuzleChunkRenderAvgUs,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenCatWuzleChunkRenderMaxUs.store(
                timings.catWuzleChunkRenderMaxUs,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenCatWuzleChunkQueueWaitMaxUs.store(
                timings.catWuzleChunkQueueWaitMaxUs,
                std::memory_order_relaxed);
            for (std::size_t workerId = 0;
                workerId < timings.catWuzleWorkerChunkCount.size();
                ++workerId)
            {
                performanceStats_.waveformScreenCatWuzleWorkerChunkCount[workerId].store(
                    timings.catWuzleWorkerChunkCount[workerId],
                    std::memory_order_relaxed);
                performanceStats_.waveformScreenCatWuzleWorkerRenderUs[workerId].store(
                    timings.catWuzleWorkerRenderUs[workerId],
                    std::memory_order_relaxed);
            }
            performanceStats_.waveformScreenBeamCoreRadiusPx.store(
                timings.beamCoreRadiusPx,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenBeamCoreMarginPx.store(
                timings.beamCoreMarginPx,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenWidth.store(
                static_cast<std::uint32_t>(screenOutputWidth),
                std::memory_order_relaxed);
            performanceStats_.waveformScreenHeight.store(
                static_cast<std::uint32_t>(screenOutputHeight),
                std::memory_order_relaxed);
            performanceStats_.waveformScreenGlowWorkload.update(
                timings.glowDirtyTiles,
                timings.glowTotalTiles,
                timings.glowHorizontalPass1Tiles,
                timings.glowVerticalPass1Tiles,
                timings.glowHorizontalPass2Tiles,
                timings.glowVerticalPass2Tiles,
                timings.glowActiveX,
                timings.glowActiveY,
                timings.glowActiveWidth,
                timings.glowActiveHeight);

            // Publication is deferred until the complete waveform worker
            // iteration has finished.  V/M/E below belong to the same frame
            // and must be part of the same coherent diagnostic snapshot.
        }
        else
        {
            performanceStats_.waveformScreen.update(0);
            performanceStats_.waveformScreenPhaseTimeline.reset(0);
            performanceStats_.clearPublishedWaveformDiagnosticTimelines();
            performanceStats_.waveformScreenPersistence.update(0);
            performanceStats_.waveformScreenTrace.update(0);
            performanceStats_.waveformScreenTracePrep.update(0);
            performanceStats_.waveformScreenTraceRaster.update(0);
            performanceStats_.waveformScreenCompose.update(0);
            performanceStats_.waveformScreenGlow.update(0);
            performanceStats_.waveformScreenOverlay.update(0);
            performanceStats_.waveformScreenTraceParallel.store(
                false,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenOutputSizeChanged.store(
                false,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenOutputBufferCapacityGrew.store(
                false,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenResamplerCacheRebuilt.store(
                false,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenTraceJobCount.store(
                0,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenBeamCoreRadiusPx.store(
                0.0,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenBeamCoreMarginPx.store(
                0,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenWidth.store(
                0,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenHeight.store(
                0,
                std::memory_order_relaxed);
            performanceStats_.waveformScreenGlowWorkload.update(
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }

        const bool videoRenderEnabled =
            waveformVideoEnabled_.load(
                std::memory_order_acquire);

        if (videoRenderEnabled)
        {
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

            waveformVideoRenderer_.setLineInfoOverlayEnabled(true, true);

            waveformVideoRenderer_.setContentScale(
                videoStandard.safeWidthScale,
                videoStandard.safeHeightScale);

            waveformVideoRenderer_.setSelectedLine(
                selectedLine);

            // The 720x576 video/Spout target needs less temporal build-up
            // than the high-resolution PC target.  Keep one shared UI, but
            // tune the video renderer to 60% of the same user setting.
            const int waveformVideoPersistence =
                (waveformPersistence * 3 + 2) / 5;

            waveformVideoRenderer_.setPersistence(
                waveformVideoPersistence);

            waveformVideoRenderer_.setCoreIntensity(
                waveformCoreIntensity);

            waveformVideoRenderer_.setCoreWidth(
                waveformCoreWidthTenths);


            // Same renderer, different target: the SD video output needs
            // less halo energy.  UI 50 therefore behaves like UI 30 on the
            // video/Spout instance while the PC instance remains unchanged.
            const int waveformVideoGlow =
                (glow * 3 + 2) / 5;

            waveformVideoRenderer_.setGlow(
                waveformVideoGlow);

            QElapsedTimer videoTimer;
            videoTimer.start();

            const auto waveformVideoPhaseStart =
                std::chrono::steady_clock::now();

            waveformVideoRenderer_.analyze(
                captureSlot.frame);

            const auto waveformVideoPhaseEnd =
                std::chrono::steady_clock::now();

            waveformPhaseAppend(
                'V',
                waveformVideoPhaseStart,
                waveformVideoPhaseEnd);

            performanceStats_.waveformVideo.update(
                static_cast<std::uint64_t>(
                    videoTimer.nsecsElapsed() /
                    1000));

            const auto& videoTimings =
                waveformVideoRenderer_.renderTimings();

            performanceStats_.waveformVideoPersistence.update(
                videoTimings.persistenceUs);
            performanceStats_.waveformVideoTrace.update(
                videoTimings.traceUs);
            performanceStats_.waveformVideoCompose.update(
                videoTimings.composeUs);
            performanceStats_.waveformVideoGlow.update(
                videoTimings.glowUs);
            performanceStats_.waveformVideoOverlay.update(
                videoTimings.overlayUs);
            performanceStats_.waveformVideoGlowWorkload.update(
                videoTimings.glowDirtyTiles,
                videoTimings.glowTotalTiles,
                videoTimings.glowHorizontalPass1Tiles,
                videoTimings.glowVerticalPass1Tiles,
                videoTimings.glowHorizontalPass2Tiles,
                videoTimings.glowVerticalPass2Tiles,
                videoTimings.glowActiveX,
                videoTimings.glowActiveY,
                videoTimings.glowActiveWidth,
                videoTimings.glowActiveHeight);
        }
        else
        {
            performanceStats_.waveformVideo.update(0);
            performanceStats_.waveformVideoPersistence.update(0);
            performanceStats_.waveformVideoTrace.update(0);
            performanceStats_.waveformVideoCompose.update(0);
            performanceStats_.waveformVideoGlow.update(0);
            performanceStats_.waveformVideoOverlay.update(0);
            performanceStats_.waveformVideoGlowWorkload.update(
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }

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

        const auto waveformMeasurementPhaseStart =
            std::chrono::steady_clock::now();

        const WaveformRenderer* measurementRenderer = nullptr;

        if (screenRenderEnabled)
        {
            measurementRenderer =
                &waveformScreenRenderer_;
        }
        else if (videoRenderEnabled)
        {
            measurementRenderer =
                &waveformVideoRenderer_;
        }

        QVector<float> measurementSamples;
        QVector<float> fullSpectrumSamples;

        if (measurementRenderer != nullptr)
        {
            const auto& measurementSource =
                measurementRenderer->visibleLumaVolts();

            measurementSamples.reserve(
                static_cast<qsizetype>(
                    measurementSource.size()));

            for (float sample : measurementSource)
            {
                measurementSamples.append(sample);
            }

            const auto& fullSpectrumSource =
                measurementRenderer->fullLumaVolts();

            fullSpectrumSamples.reserve(
                static_cast<qsizetype>(
                    fullSpectrumSource.size()));

            for (float sample : fullSpectrumSource)
            {
                fullSpectrumSamples.append(sample);
            }
        }

        emit waveformMeasurementDataChanged(
            measurementSamples);

        if (selectedLine >= 0 &&
            measurementRenderer != nullptr)
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

        const auto waveformMeasurementPhaseEnd =
            std::chrono::steady_clock::now();

        waveformPhaseAppend(
            'M',
            waveformMeasurementPhaseStart,
            waveformMeasurementPhaseEnd);

        const auto waveformEmitPhaseStart =
            std::chrono::steady_clock::now();

        if (screenRenderEnabled)
        {
            emit waveformChanged(
                waveformScreenRenderer_.image());
        }

        if (videoRenderEnabled)
        {
            emit waveformVideoChanged(
                waveformVideoRenderer_.image());
        }

        const auto waveformEmitPhaseEnd =
            std::chrono::steady_clock::now();

        waveformPhaseAppend(
            'E',
            waveformEmitPhaseStart,
            waveformEmitPhaseEnd);

        const auto waveformWorkerEnd =
            waveformEmitPhaseEnd;

        traceLog(
            TraceEventType::WaveformWorkerEnd,
            generation,
            0u,
            static_cast<std::uint32_t>(
                captureSlotIndex),
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                        waveformWorkerEnd -
                        waveformWorkerStart)
                    .count()),
            captureValid ? 1u : 0u,
            TraceRendererId::PcWaveform);

        if (screenRenderEnabled)
        {
            // Publish only after V/M/E have been appended.  The floaty then
            // shows one complete waveform-worker iteration, not just the PC
            // renderer portion.
            performanceStats_.publishWaveformDiagnosticTimelines();
        }
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

        const bool screenRenderEnabled =
            vectorscopeScreenRenderEnabled_.load(
                std::memory_order_acquire);

        if (screenRenderEnabled)
        {
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

            performanceStats_.vectorscopeScreen.update(
                static_cast<std::uint64_t>(
                    timer.nsecsElapsed() / 1000));

            const auto& timings =
                vectorscopeScreenRenderer_.renderTimings();

            performanceStats_.vectorscopeScreenAnalyzer.update(
                timings.analyzerUs);
            performanceStats_.vectorscopeScreenGlowPersistence.update(
                timings.glowPersistenceUs);
            performanceStats_.vectorscopeScreenCompose.update(
                timings.composeUs);
            performanceStats_.vectorscopeScreenOverlay.update(
                timings.overlayUs);
            performanceStats_.vectorscopeScreenGlowWorkload.update(
                timings.glowDirtyTiles,
                timings.glowTotalTiles,
                timings.glowHorizontalPass1Tiles,
                timings.glowVerticalPass1Tiles,
                timings.glowHorizontalPass2Tiles,
                timings.glowVerticalPass2Tiles,
                timings.glowActiveX,
                timings.glowActiveY,
                timings.glowActiveWidth,
                timings.glowActiveHeight);

            emit vectorscopeChanged(
                vectorscopeScreenRenderer_.image());
        }
        else
        {
            performanceStats_.vectorscopeScreen.update(0);
            performanceStats_.vectorscopeScreenAnalyzer.update(0);
            performanceStats_.vectorscopeScreenGlowPersistence.update(0);
            performanceStats_.vectorscopeScreenCompose.update(0);
            performanceStats_.vectorscopeScreenOverlay.update(0);
            performanceStats_.vectorscopeScreenGlowWorkload.update(
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        }

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

            QElapsedTimer videoTimer;
            videoTimer.start();

            vectorscopeVideoRenderer_.analyze(
                captureSlots_[slotIndex].frame);

            performanceStats_.vectorscopeVideo.update(
                static_cast<std::uint64_t>(
                    videoTimer.nsecsElapsed() / 1000));

            const auto& timings =
                vectorscopeVideoRenderer_.renderTimings();

            performanceStats_.vectorscopeVideoAnalyzer.update(
                timings.analyzerUs);
            performanceStats_.vectorscopeVideoGlowPersistence.update(
                timings.glowPersistenceUs);
            performanceStats_.vectorscopeVideoCompose.update(
                timings.composeUs);
            performanceStats_.vectorscopeVideoOverlay.update(
                timings.overlayUs);
            performanceStats_.vectorscopeVideoGlowWorkload.update(
                timings.glowDirtyTiles,
                timings.glowTotalTiles,
                timings.glowHorizontalPass1Tiles,
                timings.glowVerticalPass1Tiles,
                timings.glowHorizontalPass2Tiles,
                timings.glowVerticalPass2Tiles,
                timings.glowActiveX,
                timings.glowActiveY,
                timings.glowActiveWidth,
                timings.glowActiveHeight);

            emit vectorscopeVideoChanged(
                vectorscopeVideoRenderer_.image());
         }
        else
        {
            performanceStats_.vectorscopeVideo.update(0);
            performanceStats_.vectorscopeVideoAnalyzer.update(0);
            performanceStats_.vectorscopeVideoGlowPersistence.update(0);
            performanceStats_.vectorscopeVideoCompose.update(0);
            performanceStats_.vectorscopeVideoOverlay.update(0);
            performanceStats_.vectorscopeVideoGlowWorkload.update(
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
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

void VideoEngine::setWaveformMeasurementProbePresentation(
    bool enabled,
    double normalizedX,
    double volts)
{
    // Measurement highlighting is a PC presentation aid only. Keep the
    // independent PAL/Spout renderer completely unchanged.
    waveformScreenRenderer_.setMeasurementProbePresentation(
        enabled,
        normalizedX,
        volts);
}

void VideoEngine::recordVideoSpoutTiming(
    std::uint64_t queueDelayUs,
    std::uint64_t sendUs,
    std::uint64_t intervalUs)
{
    performanceStats_.spoutQueueDelay.update(
        queueDelayUs);

    performanceStats_.spoutSend.update(
        sendUs);

    if (intervalUs > 0)
    {
        performanceStats_.spoutInterval.update(
            intervalUs);
    }
}

void VideoEngine::setSpoutVideoEnabled(
    bool enabled)
{
    spoutVideoEnabled_.store(
        enabled,
        std::memory_order_release);
}

void VideoEngine::setVideoScreenRenderEnabled(
    bool enabled)
{
    videoScreenRenderEnabled_.store(
        enabled,
        std::memory_order_release);
}

void VideoEngine::setWaveformScreenRenderEnabled(
    bool enabled)
{
    waveformScreenRenderEnabled_.store(
        enabled,
        std::memory_order_release);
}

void VideoEngine::setVectorscopeScreenRenderEnabled(
    bool enabled)
{
    vectorscopeScreenRenderEnabled_.store(
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
