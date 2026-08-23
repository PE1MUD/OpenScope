#pragma once

#include <atomic>
#include <cstdint>

struct PerformanceMetricSnapshot
{
    std::uint64_t latestUs = 0;
    std::uint64_t averageUs = 0;
    std::uint64_t minUs = 0;
    std::uint64_t maxUs = 0;

    double latestMs() const
    {
        return latestUs / 1000.0;
    }

    double averageMs() const
    {
        return averageUs / 1000.0;
    }

    double minMs() const
    {
        return minUs / 1000.0;
    }

    double maxMs() const
    {
        return maxUs / 1000.0;
    }
};

struct PerformanceMetric
{
    std::atomic<std::uint64_t> latestUs{ 0 };
    std::atomic<std::uint64_t> averageUs{ 0 };
    std::atomic<std::uint64_t> minUs{ 0 };
    std::atomic<std::uint64_t> maxUs{ 0 };

    void update(std::uint64_t elapsedUs)
    {
        latestUs.store(
            elapsedUs,
            std::memory_order_relaxed);

        const std::uint64_t previousAverage =
            averageUs.load(
                std::memory_order_relaxed);

        if (previousAverage == 0)
        {
            averageUs.store(
                elapsedUs,
                std::memory_order_relaxed);
        }
        else
        {
            averageUs.store(
                (previousAverage * 15 + elapsedUs) / 16,
                std::memory_order_relaxed);
        }

        const std::uint64_t previousMin =
            minUs.load(
                std::memory_order_relaxed);

        if (previousMin == 0 ||
            elapsedUs < previousMin)
        {
            minUs.store(
                elapsedUs,
                std::memory_order_relaxed);
        }

        const std::uint64_t previousMax =
            maxUs.load(
                std::memory_order_relaxed);

        if (elapsedUs > previousMax)
        {
            maxUs.store(
                elapsedUs,
                std::memory_order_relaxed);
        }
    }

    PerformanceMetricSnapshot snapshot() const
    {
        return
        {
            latestUs.load(std::memory_order_relaxed),
            averageUs.load(std::memory_order_relaxed),
            minUs.load(std::memory_order_relaxed),
            maxUs.load(std::memory_order_relaxed)
        };
    }
};


struct GlowWorkloadSnapshot
{
    std::uint32_t dirtyTiles = 0;
    std::uint32_t totalTiles = 0;
    std::uint32_t horizontalPass1Tiles = 0;
    std::uint32_t verticalPass1Tiles = 0;
    std::uint32_t horizontalPass2Tiles = 0;
    std::uint32_t verticalPass2Tiles = 0;
    std::int32_t activeX = 0;
    std::int32_t activeY = 0;
    std::int32_t activeWidth = 0;
    std::int32_t activeHeight = 0;
};

struct GlowWorkloadStats
{
    std::atomic<std::uint32_t> dirtyTiles{ 0 };
    std::atomic<std::uint32_t> totalTiles{ 0 };
    std::atomic<std::uint32_t> horizontalPass1Tiles{ 0 };
    std::atomic<std::uint32_t> verticalPass1Tiles{ 0 };
    std::atomic<std::uint32_t> horizontalPass2Tiles{ 0 };
    std::atomic<std::uint32_t> verticalPass2Tiles{ 0 };
    std::atomic<std::int32_t> activeX{ 0 };
    std::atomic<std::int32_t> activeY{ 0 };
    std::atomic<std::int32_t> activeWidth{ 0 };
    std::atomic<std::int32_t> activeHeight{ 0 };

    void update(
        std::uint32_t dirty,
        std::uint32_t total,
        std::uint32_t horizontal1,
        std::uint32_t vertical1,
        std::uint32_t horizontal2,
        std::uint32_t vertical2,
        std::int32_t x,
        std::int32_t y,
        std::int32_t width,
        std::int32_t height)
    {
        dirtyTiles.store(dirty, std::memory_order_relaxed);
        totalTiles.store(total, std::memory_order_relaxed);
        horizontalPass1Tiles.store(horizontal1, std::memory_order_relaxed);
        verticalPass1Tiles.store(vertical1, std::memory_order_relaxed);
        horizontalPass2Tiles.store(horizontal2, std::memory_order_relaxed);
        verticalPass2Tiles.store(vertical2, std::memory_order_relaxed);
        activeX.store(x, std::memory_order_relaxed);
        activeY.store(y, std::memory_order_relaxed);
        activeWidth.store(width, std::memory_order_relaxed);
        activeHeight.store(height, std::memory_order_relaxed);
    }

    GlowWorkloadSnapshot snapshot() const
    {
        return
        {
            dirtyTiles.load(std::memory_order_relaxed),
            totalTiles.load(std::memory_order_relaxed),
            horizontalPass1Tiles.load(std::memory_order_relaxed),
            verticalPass1Tiles.load(std::memory_order_relaxed),
            horizontalPass2Tiles.load(std::memory_order_relaxed),
            verticalPass2Tiles.load(std::memory_order_relaxed),
            activeX.load(std::memory_order_relaxed),
            activeY.load(std::memory_order_relaxed),
            activeWidth.load(std::memory_order_relaxed),
            activeHeight.load(std::memory_order_relaxed)
        };
    }
};

struct PerformanceSnapshot
{
    PerformanceMetricSnapshot reconstruct;
    PerformanceMetricSnapshot noiseReduction;

    PerformanceMetricSnapshot deinterlace;
    PerformanceMetricSnapshot deinterlaceWorker0;
    PerformanceMetricSnapshot deinterlaceWorker1;

    // Per-display-worker phase timings.  These are wall-clock durations spent
    // by each of the two display workers in the strict N -> D -> C1 -> S1 -> C2 -> S2 pipeline.
    PerformanceMetricSnapshot displayWorker0Noise;
    PerformanceMetricSnapshot displayWorker0Deinterlace;
    PerformanceMetricSnapshot displayWorker0Convert1;
    PerformanceMetricSnapshot displayWorker0Spout1;
    PerformanceMetricSnapshot displayWorker0Convert2;
    PerformanceMetricSnapshot displayWorker0Spout2;

    PerformanceMetricSnapshot displayWorker1Noise;
    PerformanceMetricSnapshot displayWorker1Deinterlace;
    PerformanceMetricSnapshot displayWorker1Convert1;
    PerformanceMetricSnapshot displayWorker1Spout1;
    PerformanceMetricSnapshot displayWorker1Convert2;
    PerformanceMetricSnapshot displayWorker1Spout2;

    PerformanceMetricSnapshot videoScreen;
    PerformanceMetricSnapshot waveformScreen;
    PerformanceMetricSnapshot waveformVideo;
    PerformanceMetricSnapshot vectorscopeScreen;
    PerformanceMetricSnapshot vectorscopeVideo;

    PerformanceMetricSnapshot waveformScreenPersistence;
    PerformanceMetricSnapshot waveformScreenTrace;
    PerformanceMetricSnapshot waveformScreenTracePrep;
    PerformanceMetricSnapshot waveformScreenTraceRaster;
    PerformanceMetricSnapshot waveformScreenCompose;
    PerformanceMetricSnapshot waveformScreenGlow;
    PerformanceMetricSnapshot waveformScreenOverlay;
    bool waveformScreenTraceParallel = false;
    bool waveformScreenOutputSizeChanged = false;
    bool waveformScreenOutputBufferCapacityGrew = false;
    bool waveformScreenResamplerCacheRebuilt = false;
    std::uint32_t waveformScreenTraceJobCount = 0;
    double waveformScreenBeamCoreRadiusPx = 0.0;
    std::int32_t waveformScreenBeamCoreMarginPx = 0;
    std::uint32_t waveformScreenWidth = 0;
    std::uint32_t waveformScreenHeight = 0;

    PerformanceMetricSnapshot waveformVideoPersistence;
    PerformanceMetricSnapshot waveformVideoTrace;
    PerformanceMetricSnapshot waveformVideoCompose;
    PerformanceMetricSnapshot waveformVideoGlow;
    PerformanceMetricSnapshot waveformVideoOverlay;

    PerformanceMetricSnapshot vectorscopeScreenAnalyzer;
    PerformanceMetricSnapshot vectorscopeScreenGlowPersistence;
    PerformanceMetricSnapshot vectorscopeScreenCompose;
    PerformanceMetricSnapshot vectorscopeScreenOverlay;

    PerformanceMetricSnapshot vectorscopeVideoAnalyzer;
    PerformanceMetricSnapshot vectorscopeVideoGlowPersistence;
    PerformanceMetricSnapshot vectorscopeVideoCompose;
    PerformanceMetricSnapshot vectorscopeVideoOverlay;

    GlowWorkloadSnapshot waveformScreenGlowWorkload;
    GlowWorkloadSnapshot waveformVideoGlowWorkload;
    GlowWorkloadSnapshot vectorscopeScreenGlowWorkload;
    GlowWorkloadSnapshot vectorscopeVideoGlowWorkload;

    PerformanceMetricSnapshot displayFirst;
    PerformanceMetricSnapshot spoutConvertFirst;
    PerformanceMetricSnapshot field1Ready;
    PerformanceMetricSnapshot field1Margin;

    PerformanceMetricSnapshot displaySecond;
    PerformanceMetricSnapshot spoutConvertSecond;
    PerformanceMetricSnapshot field2Ready;
    PerformanceMetricSnapshot field2Margin;

    PerformanceMetricSnapshot presentInterval;
    PerformanceMetricSnapshot field1Present;
    PerformanceMetricSnapshot field2Present;

    PerformanceMetricSnapshot spoutQueueDelay;
    PerformanceMetricSnapshot spoutSend;
    PerformanceMetricSnapshot spoutInterval;

    std::uint64_t field1DeadlineMisses = 0;
    std::uint64_t field2DeadlineMisses = 0;

    PerformanceMetricSnapshot displayAllocation;
    PerformanceMetricSnapshot displaySetup;
    PerformanceMetricSnapshot displayCompose;
    PerformanceMetricSnapshot displayInterpolation;
    PerformanceMetricSnapshot displayColorConversion;
    PerformanceMetricSnapshot displayOutput;
};

struct PerformanceStats
{
    PerformanceMetric reconstruct;
    PerformanceMetric noiseReduction;

    PerformanceMetric deinterlace;
    PerformanceMetric deinterlaceWorker0;
    PerformanceMetric deinterlaceWorker1;

    PerformanceMetric displayWorker0Noise;
    PerformanceMetric displayWorker0Deinterlace;
    PerformanceMetric displayWorker0Convert1;
    PerformanceMetric displayWorker0Spout1;
    PerformanceMetric displayWorker0Convert2;
    PerformanceMetric displayWorker0Spout2;

    PerformanceMetric displayWorker1Noise;
    PerformanceMetric displayWorker1Deinterlace;
    PerformanceMetric displayWorker1Convert1;
    PerformanceMetric displayWorker1Spout1;
    PerformanceMetric displayWorker1Convert2;
    PerformanceMetric displayWorker1Spout2;

    PerformanceMetric videoScreen;
    PerformanceMetric waveformScreen;
    PerformanceMetric waveformVideo;
    PerformanceMetric vectorscopeScreen;
    PerformanceMetric vectorscopeVideo;

    PerformanceMetric waveformScreenPersistence;
    PerformanceMetric waveformScreenTrace;
    PerformanceMetric waveformScreenTracePrep;
    PerformanceMetric waveformScreenTraceRaster;
    PerformanceMetric waveformScreenCompose;
    PerformanceMetric waveformScreenGlow;
    PerformanceMetric waveformScreenOverlay;
    std::atomic<bool> waveformScreenTraceParallel{ false };
    std::atomic<bool> waveformScreenOutputSizeChanged{ false };
    std::atomic<bool> waveformScreenOutputBufferCapacityGrew{ false };
    std::atomic<bool> waveformScreenResamplerCacheRebuilt{ false };
    std::atomic<std::uint32_t> waveformScreenTraceJobCount{ 0 };
    std::atomic<double> waveformScreenBeamCoreRadiusPx{ 0.0 };
    std::atomic<std::int32_t> waveformScreenBeamCoreMarginPx{ 0 };
    std::atomic<std::uint32_t> waveformScreenWidth{ 0 };
    std::atomic<std::uint32_t> waveformScreenHeight{ 0 };

    PerformanceMetric waveformVideoPersistence;
    PerformanceMetric waveformVideoTrace;
    PerformanceMetric waveformVideoCompose;
    PerformanceMetric waveformVideoGlow;
    PerformanceMetric waveformVideoOverlay;

    PerformanceMetric vectorscopeScreenAnalyzer;
    PerformanceMetric vectorscopeScreenGlowPersistence;
    PerformanceMetric vectorscopeScreenCompose;
    PerformanceMetric vectorscopeScreenOverlay;

    PerformanceMetric vectorscopeVideoAnalyzer;
    PerformanceMetric vectorscopeVideoGlowPersistence;
    PerformanceMetric vectorscopeVideoCompose;
    PerformanceMetric vectorscopeVideoOverlay;

    GlowWorkloadStats waveformScreenGlowWorkload;
    GlowWorkloadStats waveformVideoGlowWorkload;
    GlowWorkloadStats vectorscopeScreenGlowWorkload;
    GlowWorkloadStats vectorscopeVideoGlowWorkload;

    PerformanceMetric displayFirst;
    PerformanceMetric spoutConvertFirst;
    PerformanceMetric field1Ready;
    PerformanceMetric field1Margin;

    PerformanceMetric displaySecond;
    PerformanceMetric spoutConvertSecond;
    PerformanceMetric field2Ready;
    PerformanceMetric field2Margin;

    PerformanceMetric presentInterval;
    PerformanceMetric field1Present;
    PerformanceMetric field2Present;

    PerformanceMetric spoutQueueDelay;
    PerformanceMetric spoutSend;
    PerformanceMetric spoutInterval;

    std::atomic<std::uint64_t> field1DeadlineMisses{ 0 };
    std::atomic<std::uint64_t> field2DeadlineMisses{ 0 };

    PerformanceMetric displayAllocation;
    PerformanceMetric displaySetup;
    PerformanceMetric displayCompose;
    PerformanceMetric displayInterpolation;
    PerformanceMetric displayColorConversion;
    PerformanceMetric displayOutput;

    PerformanceSnapshot snapshot() const
    {
        return
        {
            reconstruct.snapshot(),
            noiseReduction.snapshot(),

            deinterlace.snapshot(),
            deinterlaceWorker0.snapshot(),
            deinterlaceWorker1.snapshot(),

            displayWorker0Noise.snapshot(),
            displayWorker0Deinterlace.snapshot(),
            displayWorker0Convert1.snapshot(),
            displayWorker0Spout1.snapshot(),
            displayWorker0Convert2.snapshot(),
            displayWorker0Spout2.snapshot(),

            displayWorker1Noise.snapshot(),
            displayWorker1Deinterlace.snapshot(),
            displayWorker1Convert1.snapshot(),
            displayWorker1Spout1.snapshot(),
            displayWorker1Convert2.snapshot(),
            displayWorker1Spout2.snapshot(),

            videoScreen.snapshot(),
            waveformScreen.snapshot(),
            waveformVideo.snapshot(),
            vectorscopeScreen.snapshot(),
            vectorscopeVideo.snapshot(),

            waveformScreenPersistence.snapshot(),
            waveformScreenTrace.snapshot(),
            waveformScreenTracePrep.snapshot(),
            waveformScreenTraceRaster.snapshot(),
            waveformScreenCompose.snapshot(),
            waveformScreenGlow.snapshot(),
            waveformScreenOverlay.snapshot(),
            waveformScreenTraceParallel.load(
                std::memory_order_relaxed),
            waveformScreenOutputSizeChanged.load(
                std::memory_order_relaxed),
            waveformScreenOutputBufferCapacityGrew.load(
                std::memory_order_relaxed),
            waveformScreenResamplerCacheRebuilt.load(
                std::memory_order_relaxed),
            waveformScreenTraceJobCount.load(
                std::memory_order_relaxed),
            waveformScreenBeamCoreRadiusPx.load(
                std::memory_order_relaxed),
            waveformScreenBeamCoreMarginPx.load(
                std::memory_order_relaxed),
            waveformScreenWidth.load(
                std::memory_order_relaxed),
            waveformScreenHeight.load(
                std::memory_order_relaxed),

            waveformVideoPersistence.snapshot(),
            waveformVideoTrace.snapshot(),
            waveformVideoCompose.snapshot(),
            waveformVideoGlow.snapshot(),
            waveformVideoOverlay.snapshot(),

            vectorscopeScreenAnalyzer.snapshot(),
            vectorscopeScreenGlowPersistence.snapshot(),
            vectorscopeScreenCompose.snapshot(),
            vectorscopeScreenOverlay.snapshot(),

            vectorscopeVideoAnalyzer.snapshot(),
            vectorscopeVideoGlowPersistence.snapshot(),
            vectorscopeVideoCompose.snapshot(),
            vectorscopeVideoOverlay.snapshot(),

            waveformScreenGlowWorkload.snapshot(),
            waveformVideoGlowWorkload.snapshot(),
            vectorscopeScreenGlowWorkload.snapshot(),
            vectorscopeVideoGlowWorkload.snapshot(),

            displayFirst.snapshot(),
            spoutConvertFirst.snapshot(),
            field1Ready.snapshot(),
            field1Margin.snapshot(),

            displaySecond.snapshot(),
            spoutConvertSecond.snapshot(),
            field2Ready.snapshot(),
            field2Margin.snapshot(),

            presentInterval.snapshot(),
            field1Present.snapshot(),
            field2Present.snapshot(),

            spoutQueueDelay.snapshot(),
            spoutSend.snapshot(),
            spoutInterval.snapshot(),

            field1DeadlineMisses.load(
                std::memory_order_relaxed),
            field2DeadlineMisses.load(
                std::memory_order_relaxed),

            displayAllocation.snapshot(),
            displaySetup.snapshot(),
            displayCompose.snapshot(),
            displayInterpolation.snapshot(),
            displayColorConversion.snapshot(),
            displayOutput.snapshot()
        };
    }
};