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
    PerformanceMetricSnapshot waveform;
    PerformanceMetricSnapshot vectorscope;
    PerformanceMetricSnapshot display;
};

struct PerformanceStats
{
    PerformanceMetric reconstruct;
    PerformanceMetric waveform;
    PerformanceMetric vectorscope;
    PerformanceMetric display;

    PerformanceSnapshot snapshot() const
    {
        return
        {
            reconstruct.snapshot(),
            waveform.snapshot(),
            vectorscope.snapshot(),
            display.snapshot()
        };
    }
};