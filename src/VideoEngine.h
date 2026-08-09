#pragma once

#include <QObject>
#include <QImage>
#include <QThread>
#include <condition_variable>
#include <mutex>

#include <atomic>
#include <cstdint>
#include <array>

#include "video/DisplayConverter.h"
#include "video/Yuv444Frame.h"

#include "rendering/WaveformRenderer.h"
#include "processing/SignalReconstructor.h"
#include "analysis/VectorscopeAnalyzer.h"
#include "video/ReconstructedLumaFrame.h"

class VideoEngine : public QObject
{
    Q_OBJECT

public:
    explicit VideoEngine(QObject* parent = nullptr);
    ~VideoEngine() override;
    void setFrame(const QImage& frame);

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
    double displayBandwidthMHz() const;
    double traceBandwidthMHz() const;
    const QImage& currentFrame() const;
    void setWaveformPersistence(int persistence);

    void setVideoOutputSize(
        int width,
        int height);

signals:
    void frameChanged(const QImage& image);
    void waveformChanged(const QImage& image);
    void vectorscopeChanged(const QImage& image);

private:
    std::atomic<int> selectedLine_{ 320 };
    std::atomic<int> videoOutputWidth_{ 720 };
    std::atomic<int> videoOutputHeight_{ 576 };
    std::atomic<int> waveformOutputWidth_{ 1 };
    std::atomic<int> waveformOutputHeight_{ 1 };
    std::atomic<int> vectorscopeOutputWidth_{ 1 };
    std::atomic<int> vectorscopeOutputHeight_{ 1 };
    QThread displayThread_;
    std::mutex displayMutex_;
    std::condition_variable displayCondition_;

    bool displayStop_ = false;
    std::uint64_t displayLastGeneration_ = 0;

    void displayWorkerLoop();
    QThread waveformThread_;
    std::mutex waveformMutex_;
    std::condition_variable waveformCondition_;

    bool waveformStop_ = false;
    std::uint64_t waveformLastGeneration_ = 0;

    void waveformWorkerLoop();
    int findCaptureSlotByGeneration(
        std::uint64_t generation) const;
    bool isReconstructedSlotValid(
        std::size_t slotIndex,
        std::uint64_t generation) const;
    std::size_t acquireNextReconstructedWriteSlot();
    struct ReconstructedFrameSlot
    {
        std::atomic<std::uint64_t> generation{ 0 };
        std::atomic_bool writing{ false };

        ReconstructedLumaFrame frame;
    };

    static constexpr std::size_t kReconstructedSlotCount = 3;

    std::array<
        ReconstructedFrameSlot,
        kReconstructedSlotCount> reconstructedSlots_;

    std::atomic<int> latestReconstructedSlot_{ -1 };
    std::size_t nextReconstructedWriteSlot_ = 0;
    
    bool isCaptureSlotValid(
        std::size_t slotIndex,
        std::uint64_t generation) const;
    std::size_t acquireNextCaptureWriteSlot();
    struct CapturedFrame
    {
        std::atomic<std::uint64_t> generation{ 0 };
        std::atomic_bool writing{ false };

        Yuv444Frame frame;
    };

    static constexpr std::size_t kCaptureSlotCount = 3;

    std::array<CapturedFrame, kCaptureSlotCount> captureSlots_;
    std::atomic<int> latestCaptureSlot_{ -1 };
    std::size_t nextCaptureWriteSlot_ = 0;
    std::size_t activeCaptureWriteSlot_ = 0;
    QThread vectorscopeThread_;
    std::mutex vectorscopeMutex_;
    std::condition_variable vectorscopeCondition_;
    bool vectorscopeStop_ = false;
    std::uint64_t vectorscopeLastGeneration_ = 0;

    std::vector<std::jthread> lumaThreads_;
    std::mutex lumaMutex_;
    std::condition_variable lumaCondition_;
    std::condition_variable lumaDoneCondition_;

    bool lumaStop_ = false;
    
    std::jthread lumaCoordinatorThread_;
    std::mutex lumaInputMutex_;
    std::condition_variable lumaInputCondition_;

    bool lumaCoordinatorStop_ = false;
    std::uint64_t lumaLastCaptureGeneration_ = 0;

    void lumaCoordinatorLoop();
    const Yuv444Frame* lumaFrame_ = nullptr;
    ReconstructedLumaFrame* lumaOutputFrame_ = nullptr;

    int lumaWorkersRemaining_ = 0;
    void lumaWorkerLoop(
        std::size_t workerIndex);
    std::uint64_t lumaGeneration_ = 0;
    struct LumaWorker
    {
        LineResampler reconstructor{ 24, 1.00f };
        std::vector<float> sourceLine;
        std::vector<float> reconstructedLine;
    };
    std::vector<LumaWorker> lumaWorkers_;
    void vectorscopeWorkerLoop();
    QImage currentFrame_;
    DisplayConverter displayConverter_;

    std::atomic_bool framePending_{ false };
    std::atomic<std::uint64_t> captureGeneration_{ 0 };
    WaveformRenderer waveformRenderer_;
    VectorscopeAnalyzer vectorscopeAnalyzer_;

    
    void reconstructLuma(
        const Yuv444Frame& frame,
        std::uint64_t generation);
};