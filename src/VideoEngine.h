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
#include <mutex>
#include <thread>
#include <vector>

#include "analysis/VectorscopeAnalyzer.h"
#include "processing/SignalReconstructor.h"
#include "processing/VideoDeinterlacer.h"
#include "processing/NoiseReducer.h"
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

    void setVideoOutputSize(
        int width,
        int height);

    void setDisplayGamma(
        double gamma);

    void setSpoutVideoEnabled(
        bool enabled);

    void setVideoHighlightEnabled(
        bool enabled);

    void setNoiseReductionEnabled(
        bool enabled);

    void setNoiseReductionIntensity(
        int intensity);

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

    void setVectorscopeGlow(
        int glow);

    void setWaveformChromaFillIntensity(
        int intensity);

    void setWaveformColor(
        bool enabled);

    void setVectorscopeOutputSize(
        int width,
        int height);

    void setWaveformAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

    void setWaveformVideoAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

    PerformanceSnapshot performanceSnapshot() const;

    QImage captureHighResolutionSnapshot();

signals:
    void frameChanged(
        const QImage& image);

    void videoSpoutChanged(
        const QImage& image);

    void waveformChanged(
        const QImage& image);

    void waveformVideoChanged(
        const QImage& image);

    void waveformMeasurementDataChanged(
        const QVector<float>& samples);

    void vectorscopeChanged(
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
        Deinterlace,
        ConvertFirst,
        ConvertSecond
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

    std::atomic<bool> noiseReductionEnabled_{
        false
    };

    std::atomic<int> noiseReductionIntensity_{
        50
    };

    std::atomic<bool> spoutVideoEnabled_{
        false
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
    DisplayConverter spoutVideoConverter_;
    WaveformRenderer waveformScreenRenderer_;
    WaveformRenderer waveformVideoRenderer_;
    VectorscopeAnalyzer vectorscopeAnalyzer_;

    NoiseReducer noiseReducer_;
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
        1> displayPhaseWorkers_;

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
    bool lastPresentedPairValid_ = false;

    std::atomic<int> waveformZoomFactor_{ 1 };

    std::atomic<double> waveformScrollPosition_{ 0.0 };

    std::atomic<int> waveformPersistence_{ 0 };
    std::atomic<int> vectorscopePersistence_{ 0 };
    std::atomic<int> vectorscopeGlow_{ 50 };
};