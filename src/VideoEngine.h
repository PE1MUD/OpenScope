#pragma once

#include <QObject>
#include <QImage>
#include <QThread>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "video/DisplayConverter.h"
#include "video/Yuv444Frame.h"
#include "video/ReconstructedLumaFrame.h"

#include "rendering/WaveformRenderer.h"
#include "processing/SignalReconstructor.h"
#include "analysis/VectorscopeAnalyzer.h"

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
    void setWaveformZoomed(bool zoomed);
    void setWaveformScrollPosition(
        double position);
    void setWaveformOutputSize(
        int width,
        int height);
    void setVectorscopeOutputSize(
        int width,
        int height);
    void setWaveformPersistence(int persistence);

    void setVideoOutputSize(
        int width,
        int height);

    void setWaveformChromaFillIntensity(
        int intensity);

    void setWaveformColor(bool enabled);

signals:
    void frameChanged(const QImage& image);
    void waveformChanged(const QImage& image);
    void vectorscopeChanged(const QImage& image);

private:
    static constexpr unsigned int kLumaWorkerDivisor = 8;
    static constexpr unsigned int kMinimumWorkerCount = 2;

    static constexpr int kCaptureWidth = 720;
    static constexpr int kCaptureHeight = 576;

    static constexpr int kDefaultVideoWidth = 720;
    static constexpr int kDefaultVideoHeight = 576;

    static constexpr int kMinimumOutputSize = 1;
    static constexpr int kDefaultSelectedLine = 320;

    static constexpr std::size_t kCaptureSlotCount = 3;
    static constexpr std::size_t kReconstructedSlotCount = 3;

    static constexpr int kLumaReconstructionRadius = 24;
    static constexpr float kLumaReconstructionCutoff = 1.00f;

    static constexpr std::size_t kFrameSlotCount = 3;
    static constexpr int kInvalidSlotIndex = -1;

    struct CapturedFrameSlot
    {
        std::atomic<std::uint64_t> generation{ 0 };
        std::atomic_bool writing{ false };

        Yuv444Frame frame;
    };

    struct ReconstructedLumaFrameSlot
    {
        std::atomic<std::uint64_t> generation{ 0 };
        std::atomic_bool writing{ false };

        ReconstructedLumaFrame frame;
    };

    struct LumaWorker
    {
        LineResampler reconstructor{
            kLumaReconstructionRadius,
            kLumaReconstructionCutoff
        };

        std::vector<float> sourceLine;
        std::vector<float> reconstructedLine;
    };

    std::atomic<int> selectedLine_{ kDefaultSelectedLine };

    std::atomic<int> videoOutputWidth_{ kDefaultVideoWidth };
    std::atomic<int> videoOutputHeight_{ kDefaultVideoHeight };

    std::atomic<int> waveformOutputWidth_{ kMinimumOutputSize };
    std::atomic<int> waveformOutputHeight_{ kMinimumOutputSize };

    std::atomic<int> vectorscopeOutputWidth_{ kMinimumOutputSize };
    std::atomic<int> vectorscopeOutputHeight_{ kMinimumOutputSize };

    std::array<
        CapturedFrameSlot,
        kFrameSlotCount> captureSlots_;

    std::atomic<int> latestCaptureSlot_{ kInvalidSlotIndex };

    std::size_t nextCaptureWriteSlot_ = 0;
    std::size_t activeCaptureWriteSlot_ = 0;

    std::atomic<std::uint64_t> captureGeneration_{ 0 };

    std::array<
        ReconstructedLumaFrameSlot,
        kFrameSlotCount> reconstructedSlots_;

    std::atomic<int> latestReconstructedSlot_{ kInvalidSlotIndex };

    std::size_t nextReconstructedWriteSlot_ = 0;

    QThread displayThread_;
    std::mutex displayMutex_;
    std::condition_variable displayCondition_;

    bool displayStop_ = false;
    std::uint64_t displayLastGeneration_ = 0;

    QThread waveformThread_;
    std::mutex waveformMutex_;
    std::condition_variable waveformCondition_;

    bool waveformStop_ = false;
    std::uint64_t waveformLastGeneration_ = 0;

    QThread vectorscopeThread_;
    std::mutex vectorscopeMutex_;
    std::condition_variable vectorscopeCondition_;

    bool vectorscopeStop_ = false;
    std::uint64_t vectorscopeLastGeneration_ = 0;

    std::vector<std::jthread> lumaThreads_;
    std::vector<LumaWorker> lumaWorkers_;

    std::mutex lumaMutex_;
    std::condition_variable lumaCondition_;
    std::condition_variable lumaDoneCondition_;

    bool lumaStop_ = false;

    std::jthread lumaCoordinatorThread_;
    std::mutex lumaInputMutex_;
    std::condition_variable lumaInputCondition_;

    bool lumaCoordinatorStop_ = false;
    std::uint64_t lumaLastCaptureGeneration_ = 0;

    const Yuv444Frame* lumaFrame_ = nullptr;
    ReconstructedLumaFrame* lumaOutputFrame_ = nullptr;

    int lumaWorkersRemaining_ = 0;
    std::uint64_t lumaGeneration_ = 0;

    DisplayConverter displayConverter_;
    WaveformRenderer waveformRenderer_;
    VectorscopeAnalyzer vectorscopeAnalyzer_;

    void displayWorkerLoop();
    void waveformWorkerLoop();
    void vectorscopeWorkerLoop();
    void lumaCoordinatorLoop();

    void lumaWorkerLoop(
        std::size_t workerIndex);

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

    void reconstructLuma(
        const Yuv444Frame& frame,
        std::uint64_t generation);
};