#pragma once

#include <QObject>
#include <QImage>
#include <QThread>
#include <QVector>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "rendering/VectorscopeRenderer.h"
#include "processing/SignalReconstructor.h"
#include "processing/VideoDeinterlacer.h"
#include "processing/NoiseReducer.h"
#include "processing/LumaHighFrequencyCompensator.h"
#include "rendering/WaveformRenderer.h"
#include "util/PerformanceStats.h"
#include "video/DisplayConverter.h"
#include "video/ProgressiveLumaPair.h"
#include "video/ReconstructedLumaFrame.h"
#include "video/Yuv444Frame.h"
#include "settings/OpenScopeSettings.h"

class VideoEngine : public QObject
{
    Q_OBJECT

public:
    explicit VideoEngine(QObject* parent = nullptr);
    ~VideoEngine() override;

    Yuv444Frame* tryAcquireWriteFrame();
    void submitWriteFrame();
    void cancelWriteFrame();

    void setSelectedLine(int line);

    void requestWaveformFlatFieldSpectrum();

    void setVideoOutputSize(
        int width,
        int height);

    void setDisplayGamma(
        double gamma);

    void setSpoutVideoEnabled(
        bool enabled);

    void setVideoScreenRenderEnabled(
        bool enabled);

    void setWaveformScreenRenderEnabled(
        bool enabled);

    void setVectorscopeScreenRenderEnabled(
        bool enabled);

    void setVideoHighlightEnabled(
        bool enabled);

    void resetDisplayPresentation();

    void setNoiseReductionEnabled(
        bool enabled);

    void setNoiseReductionIntensity(
        int intensity);

    void setLumaCompensationEnabled(
        bool enabled);

    void setLumaCompensationGainHundredthsDb(
        int gainHundredthsDb);

    void setLumaCompensationSourceEnabled(
        bool enabled);

    void setWaveformOutputSize(
        int width,
        int height);

    void setWaveformVideoContentScale(
        double scale);

    void setWaveformZoomed(
        bool zoomed);

    void setWaveformZoomFactor(
        int factor);

    void setWaveformScrollPosition(
        double position);

    void setWaveformPersistence(
        int persistence);

    void setWaveformCoreIntensity(
        int intensity);

    void setVectorscopeGlow(
        int glow);

    void setWaveformChromaFillIntensity(
        int intensity);

    void setWaveformColor(
        bool enabled);

    void setWaveformMeasurementProbePresentation(
        bool enabled,
        double normalizedX,
        double volts);

    void setVectorscopeOutputSize(
        int width,
        int height);

    void setVectorscopeVideoOutputSize(
        int width,
        int height);

    void setVectorscopeVideoContentScale(
        double horizontalScale,
        double verticalScale);

    void setVectorscopeVideoEnabled(bool enabled);

    void setWaveformVideoEnabled(bool enabled);

    void setVectorscopePresentationInfo(
        const VectorscopePresentationInfo& info);

    void setWaveformAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

    void setWaveformVideoAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

    PerformanceSnapshot performanceSnapshot() const;

    void recordVideoSpoutTiming(
        std::uint64_t queueDelayUs,
        std::uint64_t sendUs,
        std::uint64_t intervalUs);

    QImage captureHighResolutionSnapshot();

signals:
    void inputSignalStateChanged(bool valid);

    void frameChanged(
        const QImage& image);

    void videoSpoutChanged(
        const QImage& image,
        qint64 dispatchTimestampUs);

    void waveformChanged(
        const QImage& image);

    void waveformVideoChanged(
        const QImage& image);

    void waveformMeasurementDataChanged(
        const QVector<float>& samples);

    void waveformSpectrumDataChanged(
        const QVector<float>& fullLine,
        const QVector<float>& visiblePart,
        bool inputSignalValid);

    void waveformFlatFieldSpectrumDataChanged(
        const QVector<float>& samples,
        int lineLength,
        int lineCount,
        bool inputSignalValid);

    void vectorscopeChanged(
        const QImage& image);

    void vectorscopeVideoChanged(
        const QImage& image);

private:
    static constexpr unsigned int kLumaWorkerDivisor = 8;
    static constexpr unsigned int kMinimumWorkerCount = 2;

    static constexpr int kCaptureWidth = 720;
    static constexpr int kCaptureHeight = 576;

    static constexpr int kDefaultVideoWidth = 720;
    static constexpr int kDefaultVideoHeight = 576;

    static constexpr int kMinimumOutputSize = 1;
    static constexpr int kDefaultSelectedLine = 320;

    static constexpr std::size_t kFrameSlotCount = 3;
    static constexpr int kInvalidSlotIndex = -1;

    static constexpr int kLumaReconstructionRadius = 24;
    static constexpr float kLumaReconstructionCutoff = 1.00f;

    struct CapturedFrameSlot
    {
        std::atomic<std::uint64_t> generation{ 0 };
        std::atomic_bool writing{ false };

        Yuv444Frame frame;
    };

    struct DisplayFrameSlot
    {
        QImage first;
        QImage second;
        QImage spoutFirst;
        QImage spoutSecond;
        std::uint64_t generation = 0;
        bool firstReady = false;
        bool secondReady = false;
    };

    enum class DisplayPhase
    {
        Idle,
        NoiseReduction,
        Deinterlace,
        ConvertFirst,
        SpoutFirst,
        ConvertSecond,
        SpoutSecond
    };

    //struct ReconstructedLumaFrameSlot
    //{
    //    std::atomic<std::uint64_t> generation{ 0 };
    //    std::atomic_bool writing{ false };

    //    ReconstructedLumaFrame frame;
    //};

    //struct LumaWorker
    //{
    //    LineResampler reconstructor{
    //        kLumaReconstructionRadius,
    //        kLumaReconstructionCutoff
    //    };

    //    std::vector<float> sourceLine;
    //    std::vector<float> reconstructedLine;
    //};

    // General state

    std::atomic<int> selectedLine_{
        kDefaultSelectedLine
    };

    std::atomic<bool> videoHighlightEnabled_{
        true
    };

    std::atomic<bool> flatFieldSpectrumRequested_{
        false
    };

    std::atomic<bool> noiseReductionEnabled_{
        false
    };

    std::atomic<int> noiseReductionIntensity_{
        50
    };

    std::atomic<bool> lumaCompensationEnabled_{
        false
    };

    std::atomic<int> lumaCompensationGainHundredthsDb_{
        60
    };

    std::atomic<bool> lumaCompensationSourceEnabled_{
        true
    };

    std::atomic<bool> spoutVideoEnabled_{
        false
    };

    std::atomic<bool> videoScreenRenderEnabled_{
        true
    };

    std::atomic<bool> waveformScreenRenderEnabled_{
        true
    };

    std::atomic<bool> vectorscopeScreenRenderEnabled_{
        true
    };

    // Output sizes

    std::atomic<int> videoOutputWidth_{
        kDefaultVideoWidth
    };

    std::atomic<int> videoOutputHeight_{
        kDefaultVideoHeight
    };

    std::atomic<int> waveformOutputWidth_{
        kMinimumOutputSize
    };

    std::atomic<int> waveformOutputHeight_{
        kMinimumOutputSize
    };

    std::atomic<double> waveformVideoContentScale_{
        0.80
    };

    std::atomic<OpenScopeSettings::AspectRatio>
        waveformVideoAspectRatio_{
            OpenScopeSettings::AspectRatio::Ratio16x9
        };

    std::atomic<int> vectorscopeOutputWidth_{
        kMinimumOutputSize
    };

    std::atomic<int> vectorscopeOutputHeight_{
        kMinimumOutputSize
    };

    std::atomic<int> vectorscopeVideoOutputWidth_{
        kDefaultVideoWidth
    };

    std::atomic<int> vectorscopeVideoOutputHeight_{
        kDefaultVideoHeight
    };

    std::atomic<double> vectorscopeVideoContentScaleX_{ 0.80 };
    std::atomic<double> vectorscopeVideoContentScaleY_{ 0.90 };
    std::atomic_bool vectorscopeVideoEnabled_{ false };
    std::atomic_bool waveformVideoEnabled_{ false };

    std::mutex vectorscopePresentationMutex_;
    VectorscopePresentationInfo vectorscopePresentationInfo_;

    // Capture buffers

    std::array<
        CapturedFrameSlot,
        kFrameSlotCount> captureSlots_;

    std::atomic<int> latestCaptureSlot_{
        kInvalidSlotIndex
    };

    std::size_t nextCaptureWriteSlot_ = 0;
    std::size_t activeCaptureWriteSlot_ = 0;

    std::atomic<std::uint64_t> captureGeneration_{
        0
    };

    // One-shot source-frame snapshot for high-resolution PNG export.
    // submitWriteFrame() owns the source slot while copying, so the copy
    // cannot race with the DeckLink writer.
    std::mutex exportSnapshotMutex_;
    std::condition_variable exportSnapshotCondition_;
    bool exportSnapshotRequested_ = false;
    bool exportSnapshotReady_ = false;
    Yuv444Frame exportSnapshotFrame_;

    // Reconstructed luma buffers

    //std::array<
    //    ReconstructedLumaFrameSlot,
    //    kFrameSlotCount> reconstructedSlots_;

    //std::atomic<int> latestReconstructedSlot_{
    //    kInvalidSlotIndex
    //};

    std::size_t nextReconstructedWriteSlot_ = 0;

    // Display worker

    QThread displayThread_;
    std::mutex displayMutex_;
    std::condition_variable displayCondition_;

    bool displayStop_ = false;
    std::uint64_t displayLastGeneration_ = 0;

    // Waveform worker

    QThread waveformThread_;
    std::mutex waveformMutex_;
    std::condition_variable waveformCondition_;

    bool waveformStop_ = false;
    std::uint64_t waveformLastGeneration_ = 0;

    // Vectorscope worker

    QThread vectorscopeThread_;
    std::mutex vectorscopeMutex_;
    std::condition_variable vectorscopeCondition_;

    bool vectorscopeStop_ = false;
    std::uint64_t vectorscopeLastGeneration_ = 0;

    // Luma workers

    //std::vector<std::jthread> lumaThreads_;
    //std::vector<LumaWorker> lumaWorkers_;

    //std::mutex lumaMutex_;
    //std::condition_variable lumaCondition_;
    //std::condition_variable lumaDoneCondition_;

    //bool lumaStop_ = false;

    //const Yuv444Frame* lumaFrame_ = nullptr;
    //ReconstructedLumaFrame* lumaOutputFrame_ = nullptr;

    //int lumaWorkersRemaining_ = 0;
    //std::uint64_t lumaGeneration_ = 0;

    // Luma coordinator

    //std::jthread lumaCoordinatorThread_;
    //std::mutex lumaInputMutex_;
    //std::condition_variable lumaInputCondition_;

    //bool lumaCoordinatorStop_ = false;
    //std::uint64_t lumaLastCaptureGeneration_ = 0;

    // Processing components

    std::array<
        DisplayConverter,
        2> displayConverters_;
    std::array<
        DisplayConverter,
        2> spoutVideoConverters_;
    WaveformRenderer waveformScreenRenderer_;
    WaveformRenderer waveformVideoRenderer_;
    VectorscopeRenderer vectorscopeScreenRenderer_{
        VectorscopeRenderer::Profile::Screen
    };

    VectorscopeRenderer vectorscopeVideoRenderer_{
        VectorscopeRenderer::Profile::Video
    };

    NoiseReducer noiseReducer_;
    LumaHighFrequencyCompensator lumaHighFrequencyCompensator_;
    Yuv444Frame noiseReducedFrame_;

    VideoDeinterlacer videoDeinterlacer_;
    ProgressiveLumaPair progressiveLuma_;

    PerformanceStats performanceStats_;

    LineResampler singleLineReconstructor_{
    kLumaReconstructionRadius,
    kLumaReconstructionCutoff
    };

    // Worker loops

    void displayWorkerLoop();
    void displayPhaseWorkerLoop(
        std::size_t workerIndex);
    void runDisplayPhase(
        DisplayPhase phase);
    void runWaveformTraceJobs(
        std::size_t jobCount,
        const std::function<void(std::size_t)>& job);
    bool tryRunWaveformAssistJob();
    void displayPresenterLoop();
    void waveformWorkerLoop();
    void vectorscopeWorkerLoop();
    void lumaCoordinatorLoop();

    void lumaWorkerLoop(
        std::size_t workerIndex);

    // Slot helpers

    int findCaptureSlotByGeneration(
        std::uint64_t generation) const;

    bool isCaptureSlotValid(
        std::size_t slotIndex,
        std::uint64_t generation) const;

    bool isReconstructedSlotValid(
        std::size_t slotIndex,
        std::uint64_t generation) const;

    std::size_t acquireNextCaptureWriteSlot();
    std::size_t acquireNextReconstructedWriteSlot();

    //std::vector<float> singleLineSource_;
    //std::vector<float> singleLineReconstructed_;
    // Processing

    void reconstructLuma(
        const Yuv444Frame& frame,
        std::uint64_t generation);

    std::array<
        std::jthread,
        2> displayPhaseWorkers_;

    std::mutex displayPhaseMutex_;
    std::condition_variable displayPhaseCondition_;
    std::condition_variable displayPhaseDoneCondition_;

    bool displayPhaseStop_ = false;
    DisplayPhase displayPhase_ =
        DisplayPhase::Idle;

    std::uint64_t displayPhaseGeneration_ = 0;
    std::size_t displayPhaseWorkersRemaining_ = 0;

    const Yuv444Frame* displayPhaseFrame_ = nullptr;
    const std::uint16_t* displayPhaseLuma_ = nullptr;

    const Yuv444Frame* displayNoiseSource_ = nullptr;
    Yuv444Frame* displayNoiseDestination_ = nullptr;
    int displayNoiseIntensity_ = 0;

    QRgb* displayPhaseOutputPixels_ = nullptr;
    int displayPhaseOutputStridePixels_ = 0;
    int displayPhaseOutputWidth_ = 0;
    int displayPhaseOutputHeight_ = 0;

    std::array<
        DisplayPerformance,
        2> displayPhasePerformance_;

    static constexpr int kDeinterlaceChunkLines = 8;
    std::atomic<int> displayDeinterlaceNextLine_{ 0 };

    std::array<
        std::atomic<std::uint64_t>,
        2> displayDeinterlaceWorkerUs_{};

    // Opportunistic low-priority work for the existing display phase worker.
    // The waveform thread also consumes these jobs itself.  Display phases
    // always take priority; helper work is checked only between small stripes.
    std::mutex waveformAssistMutex_;
    std::condition_variable waveformAssistDoneCondition_;
    std::function<void(std::size_t)> waveformAssistJob_;
    std::size_t waveformAssistJobCount_ = 0;
    std::size_t waveformAssistNextJob_ = 0;
    std::size_t waveformAssistJobsRunning_ = 0;
    bool waveformAssistActive_ = false;
    std::atomic<std::uint64_t> waveformAssistGeneration_{ 0 };

    std::thread displayPresenterThread_;
    std::mutex displayPresenterMutex_;
    std::condition_variable displayPresenterCondition_;

    bool displayPresenterStop_ = false;

    std::uint64_t latestCaptureTickGeneration_ = 0;
    std::uint64_t presenterLastTickGeneration_ = 0;
    std::chrono::steady_clock::time_point latestCaptureTickTime_{};

    std::array<
        DisplayFrameSlot,
        kFrameSlotCount> displayFrameSlots_;

    QImage lastPresentedFirst_;
    QImage lastPresentedSecond_;
    QImage lastPresentedSpoutFirst_;
    QImage lastPresentedSpoutSecond_;
    bool lastPresentedPairValid_ = false;

    std::atomic<int> waveformZoomFactor_{ 1 };

    std::atomic<double> waveformScrollPosition_{ 0.0 };

    std::atomic<int> waveformPersistence_{ 0 };
    std::atomic<int> waveformCoreIntensity_{ 100 };
    std::atomic<int> vectorscopePersistence_{ 0 };
    std::atomic<int> vectorscopeGlow_{ 50 };
};