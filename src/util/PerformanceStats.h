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

struct PerformanceSnapshot
{
    PerformanceMetricSnapshot reconstruct;
    PerformanceMetricSnapshot noiseReduction;

    PerformanceMetricSnapshot deinterlace;
    PerformanceMetricSnapshot deinterlaceWorker0;
    PerformanceMetricSnapshot deinterlaceWorker1;

    PerformanceMetricSnapshot waveform;
    PerformanceMetricSnapshot vectorscope;

    PerformanceMetricSnapshot displayFirst;
    PerformanceMetricSnapshot field1Ready;
    PerformanceMetricSnapshot field1Margin;

    PerformanceMetricSnapshot displaySecond;
    PerformanceMetricSnapshot field2Ready;
    PerformanceMetricSnapshot field2Margin;

    PerformanceMetricSnapshot presentInterval;

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

    PerformanceMetric waveform;
    PerformanceMetric vectorscope;

    PerformanceMetric displayFirst;
    PerformanceMetric field1Ready;
    PerformanceMetric field1Margin;

    PerformanceMetric displaySecond;
    PerformanceMetric field2Ready;
    PerformanceMetric field2Margin;

    PerformanceMetric presentInterval;

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

            waveform.snapshot(),
            vectorscope.snapshot(),

            displayFirst.snapshot(),
            field1Ready.snapshot(),
            field1Margin.snapshot(),

            displaySecond.snapshot(),
            field2Ready.snapshot(),
            field2Margin.snapshot(),

            presentInterval.snapshot(),

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