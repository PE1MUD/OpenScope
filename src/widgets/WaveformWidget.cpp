#include "widgets/WaveformWidget.h"

#include "rendering/WaveformGraticule.h"
#include "standards/VideoStandard.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QResizeEvent>
#include <QTimer>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
constexpr double kBlackLevelVolts = 0.3;
constexpr double kWhiteLevelVolts = 1.0;
constexpr double kMinimumPeakVolts = 0.1;
constexpr double kMaximumVariation = 0.10;
constexpr int kMinimumStableCycles = 2;
constexpr int kTemporalMeasurementFrames = 4;
constexpr int kMinimumTemporalValidFrames = 2;
constexpr int kExpectedMultiburstBursts = 6;
constexpr int kMinimumMultiburstBursts = 4;
constexpr int kMultiburstSearchFrames = 8;

struct WaveformGeometry
{
    QRect displayRect;
    QRectF scopeRect;
    double zeroVoltY = 0.0;
    double oneVoltY = 0.0;
    double voltsPerDisplayPixel = 0.0;
    VideoStandard standard = VideoStandard::pal625();
};

struct SamplePoint
{
    double displayX = 0.0;
    double displayY = 0.0;
    double volts = 0.0;
    double sourceSamples = 0.0;
    bool valid = false;
};

struct CandidateCycle
{
    int startIndex = 0;
    int endIndex = 0;
    int troughIndex = 0;
    double periodSamples = 0.0;
    double vppVolts = 0.0;
};

QPointF clampPointToRect(
    const QPointF& point,
    const QRectF& rect)
{
    return
    {
        std::clamp(
            point.x(),
            rect.left(),
            rect.right()),
        std::clamp(
            point.y(),
            rect.top(),
            rect.bottom())
    };
}

QRectF normalizedRect(
    const QPointF& a,
    const QPointF& b)
{
    return QRectF(a, b).normalized();
}

double sampleClockHz(
    const VideoStandard& standard)
{
    switch (standard.colorStandard)
    {
    case VideoColorStandard::Rec601_625:
    case VideoColorStandard::Rec601_525:
        return 13.5e6;

    default:
        return 13.5e6;
    }
}

QString formatPercent(
    double percent)
{
    if (std::abs(percent) < 0.05)
    {
        percent = 0.0;
    }

    return QStringLiteral("%1%").arg(percent, 0, 'f', 1);
}

void drawArrowHead(
    QPainter& painter,
    const QPointF& tip,
    const QPointF& direction,
    double length,
    double width)
{
    const QPointF baseCenter =
        tip - direction * length;

    const QPointF perpendicular(
        -direction.y(),
        direction.x());

    const QPointF sideA =
        baseCenter + perpendicular * width;

    const QPointF sideB =
        baseCenter - perpendicular * width;

    painter.drawLine(tip, sideA);
    painter.drawLine(tip, sideB);
}

WaveformGeometry makeGeometry(
    const QImage& image,
    const QRect& displayRect,
    const QWidget* widget)
{
    WaveformGeometry geometry;
    geometry.displayRect = displayRect;
    geometry.standard = VideoStandard::pal625();

    const WaveformGraticule graticule;
    const double leftInset =
        graticule.leftInset(
            QApplication::font(),
            widget,
            static_cast<double>(displayRect.height()));

    const QRectF scopeRect(
        static_cast<double>(displayRect.left()) + leftInset,
        static_cast<double>(displayRect.top()),
        static_cast<double>(displayRect.width()) - leftInset,
        static_cast<double>(displayRect.height()));

    const QFont font =
        graticule.labelFont(
            QApplication::font(),
            scopeRect.height());

    const QFontMetricsF metrics(font, widget);
    const double labelHeight = metrics.height();

    geometry.scopeRect = QRectF(
        scopeRect.left(),
        scopeRect.top() + labelHeight * 0.5,
        scopeRect.width(),
        scopeRect.height() - labelHeight);

    const AnalogVideoLevels analog =
        analogLevels(geometry.standard.colorStandard);

    const auto voltsToDisplayY =
        [&](double volts)
        {
            return
                geometry.scopeRect.bottom() -
                volts * geometry.scopeRect.height() /
                analog.graticuleMaxVolts;
        };

    geometry.zeroVoltY = voltsToDisplayY(0.0);
    geometry.oneVoltY = voltsToDisplayY(1.0);
    geometry.voltsPerDisplayPixel =
        1.0 / (geometry.zeroVoltY - geometry.oneVoltY);

    Q_UNUSED(image);
    return geometry;
}

int whitenessScore(QRgb pixel)
{
    const int red = qRed(pixel);
    const int green = qGreen(pixel);
    const int blue = qBlue(pixel);
    const int maximum = (std::max)({ red, green, blue });
    const int minimum = (std::min)({ red, green, blue });
    const int delta = maximum - minimum;
    return minimum - delta / 2;
}

double relativeVariation(
    double value,
    double reference)
{
    if (std::abs(reference) < 1.0e-12)
    {
        return std::numeric_limits<double>::infinity();
    }

    return std::abs(value - reference) / std::abs(reference);
}


struct SineFitResult
{
    bool valid = false;
    double dc = 0.0;
    double sine = 0.0;
    double cosine = 0.0;
    double peak = 0.0;
    double vpp = 0.0;
};

bool solve3x3(
    double matrix[3][4])
{
    for (int pivot = 0; pivot < 3; ++pivot)
    {
        int bestRow = pivot;
        double bestMagnitude = std::abs(matrix[pivot][pivot]);

        for (int row = pivot + 1; row < 3; ++row)
        {
            const double magnitude = std::abs(matrix[row][pivot]);
            if (magnitude > bestMagnitude)
            {
                bestMagnitude = magnitude;
                bestRow = row;
            }
        }

        if (bestMagnitude < 1.0e-12)
        {
            return false;
        }

        if (bestRow != pivot)
        {
            for (int column = pivot; column < 4; ++column)
            {
                std::swap(matrix[pivot][column], matrix[bestRow][column]);
            }
        }

        const double divisor = matrix[pivot][pivot];
        for (int column = pivot; column < 4; ++column)
        {
            matrix[pivot][column] /= divisor;
        }

        for (int row = 0; row < 3; ++row)
        {
            if (row == pivot)
            {
                continue;
            }

            const double factor = matrix[row][pivot];
            for (int column = pivot; column < 4; ++column)
            {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
        }
    }

    return true;
}

SineFitResult fitSineAtMeasuredPeriod(
    const QVector<double>& samples,
    int firstIndex,
    int lastIndex,
    double periodSamples)
{
    SineFitResult result;

    if (periodSamples <= 0.0 ||
        firstIndex < 0 ||
        lastIndex < firstIndex ||
        lastIndex >= static_cast<int>(samples.size()))
    {
        return result;
    }

    const double omega =
        2.0 * std::acos(-1.0) /
        periodSamples;

    double sumOne = 0.0;
    double sumSin = 0.0;
    double sumCos = 0.0;
    double sumSinSin = 0.0;
    double sumCosCos = 0.0;
    double sumSinCos = 0.0;
    double sumY = 0.0;
    double sumYSin = 0.0;
    double sumYCos = 0.0;

    for (int index = firstIndex; index <= lastIndex; ++index)
    {
        const double phase =
            omega *
            static_cast<double>(index - firstIndex);
        const double sine = std::sin(phase);
        const double cosine = std::cos(phase);
        const double value = samples[index];

        sumOne += 1.0;
        sumSin += sine;
        sumCos += cosine;
        sumSinSin += sine * sine;
        sumCosCos += cosine * cosine;
        sumSinCos += sine * cosine;
        sumY += value;
        sumYSin += value * sine;
        sumYCos += value * cosine;
    }

    double matrix[3][4]
    {
        { sumOne, sumSin,    sumCos,    sumY    },
        { sumSin, sumSinSin, sumSinCos, sumYSin },
        { sumCos, sumSinCos, sumCosCos, sumYCos }
    };

    if (!solve3x3(matrix))
    {
        return result;
    }

    result.dc = matrix[0][3];
    result.sine = matrix[1][3];
    result.cosine = matrix[2][3];
    result.peak = std::hypot(result.sine, result.cosine);
    result.vpp = 2.0 * result.peak;
    result.valid = std::isfinite(result.vpp) && result.vpp > 0.0;
    return result;
}


double rms(const QVector<double>& values)
{
    if (values.isEmpty())
    {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (double value : values)
    {
        sumSquares += value * value;
    }

    return std::sqrt(
        sumSquares /
        static_cast<double>(values.size()));
}

double standardDeviation(
    const QVector<double>& values,
    double mean)
{
    if (values.isEmpty())
    {
        return 0.0;
    }

    double sumSquares = 0.0;
    for (double value : values)
    {
        const double difference = value - mean;
        sumSquares += difference * difference;
    }

    return std::sqrt(
        sumSquares /
        static_cast<double>(values.size()));
}
}

WaveformWidget::WaveformWidget(QWidget* parent)
    : VideoWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    multiburstDebugTimer_ = new QTimer(this);
    multiburstDebugTimer_->setInterval(1000);
    multiburstDebugTimer_->setSingleShot(false);

    connect(
        multiburstDebugTimer_,
        &QTimer::timeout,
        this,
        &WaveformWidget::advanceMultiburstDebugStep);
}

bool WaveformWidget::isZoomed() const
{
    return zoomFactor_ > 1;
}

int WaveformWidget::zoomFactor() const
{
    return zoomFactor_;
}

void WaveformWidget::setScrollPosition(double position)
{
    const double newPosition = std::clamp(position, 0.0, 1.0);

    if (newPosition == scrollPosition_)
    {
        return;
    }

    scrollPosition_ = newPosition;
    clearAreaAnalysis();
    temporalArea_ = {};
    temporalReference_ = {};
    referenceAnalysis_.selectionRect = {};
    emit scrollPositionChanged(scrollPosition_);
    update();
}

void WaveformWidget::setZoomEnabled(bool enabled)
{
    if (zoomEnabled_ == enabled)
    {
        return;
    }

    zoomEnabled_ = enabled;
    if (!zoomEnabled_ && zoomFactor_ != 1)
    {
        setZoomFactor(1);
    }
}

void WaveformWidget::setZoomed(bool zoomed)
{
    setZoomFactor(zoomed ? 10 : 1);
}

void WaveformWidget::setZoomFactor(int factor)
{
    if (factor != 1 && factor != 5 && factor != 10)
    {
        factor = 1;
    }

    if (factor > 1 && !zoomEnabled_)
    {
        factor = 1;
    }

    if (zoomFactor_ == factor)
    {
        return;
    }

    zoomFactor_ = factor;
    if (zoomFactor_ <= 1)
    {
        panActive_ = false;
    }

    updateInteractionCursor();
    clearAreaAnalysis();
    temporalArea_ = {};
    temporalReference_ = {};
    referenceAnalysis_.selectionRect = {};

    emit zoomFactorChanged(zoomFactor_);
    emit zoomChanged(zoomFactor_ > 1);

    emitOutputSize();
    update();
}

QRect WaveformWidget::imageRect() const
{
    const QSize outputSize = fitAspectSize(width(), height());
    return QRect(
        (width() - outputSize.width()) / 2,
        (height() - outputSize.height()) / 2,
        outputSize.width(),
        outputSize.height());
}

QRectF WaveformWidget::scopeRect(const QRect& displayRect) const
{
    return makeGeometry(image(), displayRect, this).scopeRect;
}

void WaveformWidget::updateHover(const QPointF& position)
{
    if (areaMode_ || referenceMode_)
    {
        hoverPosition_ = position;
        hoverActive_ = false;
        update();
        return;
    }

    const QRect displayRect = imageRect();
    if (!displayRect.contains(position.toPoint()))
    {
        if (hoverActive_)
        {
            hoverActive_ = false;
            update();
        }
        return;
    }

    hoverPosition_ = position;
    hoverActive_ = true;
    update();
}

void WaveformWidget::updateInteractionCursor()
{
    if (measurementTableDragging_)
    {
        setCursor(Qt::SizeAllCursor);
        return;
    }

    if (referenceLevelDragging_ ||
        areaLevelDragging_ ||
        multiburstLevelDragging_)
    {
        setCursor(Qt::SizeVerCursor);
        return;
    }

    if (panActive_)
    {
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (areaMode_ || referenceMode_)
    {
        setCursor(Qt::CrossCursor);
        return;
    }

    unsetCursor();
}

int WaveformWidget::referenceLevelHit(const QPointF& position) const
{
    if (!referenceAnalysis_.valid ||
        referenceAnalysis_.selectionRect.isEmpty())
    {
        return -1;
    }

    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);

    if (position.x() < referenceAnalysis_.selectionRect.left() ||
        position.x() > referenceAnalysis_.selectionRect.right())
    {
        return -1;
    }

    const AnalogVideoLevels analog =
        analogLevels(geometry.standard.colorStandard);

    const auto voltsToDisplayY =
        [&](double volts)
        {
            return
                geometry.scopeRect.bottom() -
                volts * geometry.scopeRect.height() /
                analog.graticuleMaxVolts;
        };

    const double lowY = voltsToDisplayY(referenceAnalysis_.lowVolts);
    const double highY = voltsToDisplayY(referenceAnalysis_.highVolts);
    const double hitDistance =
        (std::max)(6.0, geometry.scopeRect.height() * 0.006);

    const double lowDistance = std::abs(position.y() - lowY);
    const double highDistance = std::abs(position.y() - highY);

    if (lowDistance <= hitDistance && lowDistance <= highDistance)
    {
        return 0;
    }

    if (highDistance <= hitDistance)
    {
        return 1;
    }

    return -1;
}

int WaveformWidget::areaLevelHit(const QPointF& position) const
{
    if (!areaAnalysis_.valid ||
        areaAnalysis_.selectionRect.isEmpty())
    {
        return -1;
    }

    if (position.x() < areaAnalysis_.selectionRect.left() ||
        position.x() > areaAnalysis_.selectionRect.right())
    {
        return -1;
    }

    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);

    const double lowY = voltsToDisplayY(areaAnalysis_.lowVolts);
    const double highY = voltsToDisplayY(areaAnalysis_.highVolts);
    const double hitDistance =
        (std::max)(6.0, geometry.scopeRect.height() * 0.006);

    const double lowDistance = std::abs(position.y() - lowY);
    const double highDistance = std::abs(position.y() - highY);

    if (lowDistance <= hitDistance && lowDistance <= highDistance)
    {
        return 0;
    }

    if (highDistance <= hitDistance)
    {
        return 1;
    }

    return -1;
}

int WaveformWidget::multiburstLevelHit(
    const QPointF& position,
    int* measurementIndex) const
{
    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);

    const double hitDistance =
        (std::max)(6.0, geometry.scopeRect.height() * 0.006);

    for (int i = 0;
        i < multiburstAnalyses_.size();
        ++i)
    {
        const AreaAnalysisResult& analysis =
            multiburstAnalyses_[i];

        if (!analysis.valid ||
            analysis.selectionRect.isEmpty())
        {
            continue;
        }

        if (position.x() < analysis.selectionRect.left() ||
            position.x() > analysis.selectionRect.right())
        {
            continue;
        }

        const double lowY =
            voltsToDisplayY(analysis.lowVolts);
        const double highY =
            voltsToDisplayY(analysis.highVolts);

        const double lowDistance =
            std::abs(position.y() - lowY);
        const double highDistance =
            std::abs(position.y() - highY);

        if (lowDistance <= hitDistance &&
            lowDistance <= highDistance)
        {
            if (measurementIndex != nullptr)
            {
                *measurementIndex = i;
            }
            return 0;
        }

        if (highDistance <= hitDistance)
        {
            if (measurementIndex != nullptr)
            {
                *measurementIndex = i;
            }
            return 1;
        }
    }

    return -1;
}

double WaveformWidget::displayYToVolts(double displayY) const
{
    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);
    const AnalogVideoLevels analog =
        analogLevels(geometry.standard.colorStandard);

    const double clampedY =
        std::clamp(
            displayY,
            geometry.scopeRect.top(),
            geometry.scopeRect.bottom());

    return
        (geometry.scopeRect.bottom() - clampedY) *
        analog.graticuleMaxVolts /
        geometry.scopeRect.height();
}

double WaveformWidget::voltsToDisplayY(double volts) const
{
    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);
    const AnalogVideoLevels analog =
        analogLevels(geometry.standard.colorStandard);

    const double clampedVolts =
        std::clamp(volts, 0.0, analog.graticuleMaxVolts);

    return
        geometry.scopeRect.bottom() -
        clampedVolts * geometry.scopeRect.height() /
        analog.graticuleMaxVolts;
}

void WaveformWidget::clearAreaAnalysis()
{
    areaSelecting_ = false;
    areaAnalysis_ = {};
}

void WaveformWidget::clearMeasurements()
{
    areaSelecting_ = false;
    referenceSelecting_ = false;
    referenceLevelDragging_ = false;
    referenceLevelDragIndex_ = -1;
    areaLevelDragging_ = false;
    areaLevelDragIndex_ = -1;
    multiburstLevelDragging_ = false;
    multiburstDragMeasurementIndex_ = -1;
    multiburstLevelDragIndex_ = -1;
    measurementTableDragging_ = false;
    measurementTableUserPositioned_ = false;
    measurementTableRect_ = {};
    measurementTablePosition_ = {};
    measureActive_ = false;
    areaAnalysis_ = {};
    referenceAnalysis_ = {};
    temporalArea_ = {};
    temporalReference_ = {};
    multiburstMeasurementActive_ = false;
    multiburstPending_ = false;
    multiburstSearchFrames_ = 0;
    multiburstMeasureStep_ = 0;
    multiburstStatus_.clear();
    multiburstDebugActivity_.clear();
    multiburstDebugBaseline_.clear();
    multiburstDebugThreshold_ = 0.0;
    multiburstDebugMaximum_ = 0.0;
    multiburstDebugCandidateRects_.clear();
    multiburstDebugCandidateCount_ = 0;
    if (multiburstDebugTimer_ != nullptr)
    {
        multiburstDebugTimer_->stop();
    }
    multiburstAnalyses_.clear();
    temporalMultiburst_.clear();
    update();
}

void WaveformWidget::advanceMultiburstDebugStep()
{
    if (multiburstPending_)
    {
        processTemporalMeasurements();

        if (multiburstPending_)
        {
            if (multiburstSearchFrames_ >= kMultiburstSearchFrames)
            {
                multiburstStatus_ =
                    QStringLiteral("MULTIBURST  NOT FOUND   CANDIDATES %1")
                        .arg(multiburstDebugCandidateCount_);
                multiburstDebugTimer_->stop();
            }
            else
            {
                multiburstStatus_ =
                    QStringLiteral("MULTIBURST  SEARCH  %1/%2   CANDIDATES %3")
                        .arg(multiburstSearchFrames_ + 1)
                        .arg(kMultiburstSearchFrames)
                        .arg(multiburstDebugCandidateCount_);
            }
        }
        else if (!temporalMultiburst_.isEmpty())
        {
            multiburstMeasureStep_ = 0;
            multiburstStatus_ =
                QStringLiteral("MULTIBURST  MEASURE  1/%1")
                    .arg(kTemporalMeasurementFrames);
        }
        else
        {
            multiburstStatus_ =
                QStringLiteral("MULTIBURST  NOT FOUND   CANDIDATES %1")
                    .arg(multiburstDebugCandidateCount_);
            multiburstDebugTimer_->stop();
        }

        update();
        return;
    }

    if (!multiburstMeasurementActive_ ||
        (temporalMultiburst_.isEmpty() &&
            !temporalReference_.active))
    {
        multiburstDebugTimer_->stop();
        return;
    }

    ++multiburstMeasureStep_;
    processTemporalMeasurements();

    if (multiburstMeasureStep_ >= kTemporalMeasurementFrames)
    {
        int validBursts = 0;
        for (const AreaAnalysisResult& analysis : multiburstAnalyses_)
        {
            if (analysis.valid)
            {
                ++validBursts;
            }
        }

        multiburstStatus_ =
            QStringLiteral("MULTIBURST  DONE  %1/%2")
                .arg(validBursts)
                .arg(kExpectedMultiburstBursts);
        multiburstDebugTimer_->stop();
    }
    else
    {
        multiburstStatus_ =
            QStringLiteral("MULTIBURST  MEASURE  %1/%2")
                .arg(multiburstMeasureStep_ + 1)
                .arg(kTemporalMeasurementFrames);
    }

    update();
}

void WaveformWidget::processTemporalMeasurements()
{
    if (multiburstPending_)
    {
        ++multiburstSearchFrames_;

        const MultiburstLayout layout =
            detectMultiburstLayout();

        if (layout.valid &&
            layout.burstRects.size() >= kMinimumMultiburstBursts)
        {
            multiburstPending_ = false;
            multiburstSearchFrames_ = 0;

            temporalReference_ = {};
            temporalReference_.active = true;
            temporalReference_.selectionRect = layout.referenceRect;
            temporalReference_.representative.frequencyMHz =
                layout.referenceFrequencyMHz;

            const int burstCount =
                static_cast<int>(layout.burstRects.size());

            temporalMultiburst_.clear();
            temporalMultiburst_.resize(burstCount);
            multiburstAnalyses_.clear();
            multiburstAnalyses_.resize(burstCount);

            for (int i = 0; i < burstCount; ++i)
            {
                temporalMultiburst_[i].active = true;
                temporalMultiburst_[i].selectionRect = layout.burstRects[i];
            }

            return;
        }
        else if (multiburstSearchFrames_ >= kMultiburstSearchFrames)
        {
            multiburstPending_ = false;
            multiburstSearchFrames_ = 0;
        }
    }

    if (temporalArea_.active)
    {
        ++temporalArea_.framesSeen;

        const AreaAnalysisResult result =
            analyzeSelection(temporalArea_.selectionRect);

        if (result.valid)
        {
            ++temporalArea_.validFrames;
            temporalArea_.sumFrequencyMHz += result.frequencyMHz;
            temporalArea_.sumVppVolts +=
                static_cast<double>(result.vppMillivolts) / 1000.0;
            temporalArea_.sumLowVolts += result.lowVolts;
            temporalArea_.sumHighVolts += result.highVolts;
            temporalArea_.sumVppTopY += result.vppTop.y();
            temporalArea_.sumVppBottomY += result.vppBottom.y();
            temporalArea_.representative = result;
        }

        if (temporalArea_.framesSeen >= kTemporalMeasurementFrames)
        {
            if (temporalArea_.validFrames >= kMinimumTemporalValidFrames)
            {
                const double divisor =
                    static_cast<double>(temporalArea_.validFrames);

                AreaAnalysisResult averaged =
                    temporalArea_.representative;

                averaged.attempted = true;
                averaged.valid = true;
                averaged.selectionRect = temporalArea_.selectionRect;
                averaged.frequencyMHz =
                    temporalArea_.sumFrequencyMHz / divisor;

                const double averagedVppVolts =
                    temporalArea_.sumVppVolts / divisor;

                averaged.vppMillivolts =
                    static_cast<int>(
                        std::lround(
                            averagedVppVolts * 1000.0));

                averaged.lowVolts =
                    temporalArea_.sumLowVolts / divisor;
                averaged.highVolts =
                    temporalArea_.sumHighVolts / divisor;
                averaged.vppTop.setY(
                    temporalArea_.sumVppTopY / divisor);
                averaged.vppBottom.setY(
                    temporalArea_.sumVppBottomY / divisor);
                averaged.message =
                    QStringLiteral("Temporal average %1/%2")
                        .arg(temporalArea_.validFrames)
                        .arg(kTemporalMeasurementFrames);

                areaAnalysis_ = averaged;
            }
            else
            {
                AreaAnalysisResult failed;
                failed.attempted = true;
                failed.valid = false;
                failed.selectionRect = temporalArea_.selectionRect;
                failed.message =
                    QStringLiteral("Unstable measurement (%1/%2 valid)")
                        .arg(temporalArea_.validFrames)
                        .arg(kTemporalMeasurementFrames);
                areaAnalysis_ = failed;
            }

            temporalArea_ = {};
            update();
        }
    }

    for (int burstIndex = 0;
        burstIndex < temporalMultiburst_.size();
        ++burstIndex)
    {
        TemporalAreaMeasurement& temporal =
            temporalMultiburst_[burstIndex];

        if (!temporal.active)
        {
            continue;
        }

        ++temporal.framesSeen;

        const AreaAnalysisResult result =
            analyzeSelection(temporal.selectionRect);

        if (result.valid)
        {
            ++temporal.validFrames;
            temporal.sumFrequencyMHz += result.frequencyMHz;
            temporal.sumVppVolts +=
                static_cast<double>(result.vppMillivolts) / 1000.0;
            temporal.sumLowVolts += result.lowVolts;
            temporal.sumHighVolts += result.highVolts;
            temporal.sumVppTopY += result.vppTop.y();
            temporal.sumVppBottomY += result.vppBottom.y();
            temporal.representative = result;
        }

        if (temporal.framesSeen >= kTemporalMeasurementFrames)
        {
            AreaAnalysisResult averaged;
            averaged.attempted = true;
            averaged.selectionRect = temporal.selectionRect;

            if (temporal.validFrames >= kMinimumTemporalValidFrames)
            {
                const double divisor =
                    static_cast<double>(temporal.validFrames);

                averaged = temporal.representative;
                averaged.attempted = true;
                averaged.valid = true;
                averaged.selectionRect = temporal.selectionRect;
                averaged.frequencyMHz =
                    temporal.sumFrequencyMHz / divisor;

                const double averagedVppVolts =
                    temporal.sumVppVolts / divisor;

                averaged.vppMillivolts =
                    static_cast<int>(
                        std::lround(
                            averagedVppVolts * 1000.0));
                averaged.lowVolts =
                    temporal.sumLowVolts / divisor;
                averaged.highVolts =
                    temporal.sumHighVolts / divisor;
                averaged.vppTop.setY(
                    temporal.sumVppTopY / divisor);
                averaged.vppBottom.setY(
                    temporal.sumVppBottomY / divisor);
                averaged.message =
                    QStringLiteral("Multiburst temporal average %1/%2")
                        .arg(temporal.validFrames)
                        .arg(kTemporalMeasurementFrames);
            }
            else
            {
                averaged.valid = false;
                averaged.message =
                    QStringLiteral("Multiburst unstable (%1/%2 valid)")
                        .arg(temporal.validFrames)
                        .arg(kTemporalMeasurementFrames);
            }

            if (burstIndex >= multiburstAnalyses_.size())
            {
                multiburstAnalyses_.resize(burstIndex + 1);
            }

            multiburstAnalyses_[burstIndex] = averaged;
            temporal.active = false;
        }
    }

    if (temporalReference_.active)
    {
        ++temporalReference_.framesSeen;

        ReferenceAnalysisResult result =
            analyzeReferenceSelection(temporalReference_.selectionRect);

        result.frequencyMHz =
            temporalReference_.representative.frequencyMHz;

        if (!result.valid)
        {
            const AreaAnalysisResult sinusResult =
                analyzeSelection(temporalReference_.selectionRect);

            if (sinusResult.valid &&
                sinusResult.vppMillivolts > 0)
            {
                result.valid = true;
                result.vppMillivolts = sinusResult.vppMillivolts;
                result.vppVolts =
                    static_cast<double>(sinusResult.vppMillivolts) /
                    1000.0;
                result.selectionRect = sinusResult.selectionRect;
                result.lowVolts = sinusResult.lowVolts;
                result.highVolts = sinusResult.highVolts;

                if (result.frequencyMHz <= 0.0)
                {
                    result.frequencyMHz =
                        sinusResult.frequencyMHz;
                }

                result.message =
                    QStringLiteral("Reference set from sinus");
            }
        }

        if (result.valid)
        {
            ++temporalReference_.validFrames;
            temporalReference_.sumVppVolts += result.vppVolts;
            temporalReference_.sumLowVolts += result.lowVolts;
            temporalReference_.sumHighVolts += result.highVolts;
            temporalReference_.representative = result;
        }

        if (temporalReference_.framesSeen >= kTemporalMeasurementFrames)
        {
            if (temporalReference_.validFrames >= kMinimumTemporalValidFrames)
            {
                const double divisor =
                    static_cast<double>(temporalReference_.validFrames);

                ReferenceAnalysisResult averaged =
                    temporalReference_.representative;

                averaged.valid = true;
                averaged.selectionRect = temporalReference_.selectionRect;
                averaged.vppVolts =
                    temporalReference_.sumVppVolts / divisor;
                averaged.vppMillivolts =
                    static_cast<int>(
                        std::lround(
                            averaged.vppVolts * 1000.0));
                averaged.lowVolts =
                    temporalReference_.sumLowVolts / divisor;
                averaged.highVolts =
                    temporalReference_.sumHighVolts / divisor;
                averaged.frequencyMHz =
                    temporalReference_.representative.frequencyMHz;
                averaged.message =
                    QStringLiteral("Reference temporal average %1/%2")
                        .arg(temporalReference_.validFrames)
                        .arg(kTemporalMeasurementFrames);

                referenceAnalysis_ = averaged;
            }

            temporalReference_ = {};
            update();
        }
    }

    if (multiburstMeasurementActive_)
    {
        bool multiburstMeasuring =
            temporalReference_.active;
        int multiburstFramesSeen = 0;

        for (const TemporalAreaMeasurement& temporal : temporalMultiburst_)
        {
            if (temporal.active)
            {
                multiburstMeasuring = true;
            }

            multiburstFramesSeen =
                (std::max)(
                    multiburstFramesSeen,
                    temporal.framesSeen);
        }

        multiburstMeasureStep_ =
            (std::min)(
                multiburstFramesSeen,
                kTemporalMeasurementFrames);

        if (multiburstMeasuring)
        {
            multiburstStatus_ =
                QStringLiteral("MULTIBURST  MEASURE  %1/%2")
                    .arg(multiburstMeasureStep_)
                    .arg(kTemporalMeasurementFrames);
        }
        else
        {
            // The M-owned temporal measurements have finished.
            multiburstMeasurementActive_ = false;
            multiburstStatus_.clear();
        }
    }
}

void WaveformWidget::setMeasurementLuma(
    const QVector<float>& samples)
{
    measurementLuma_ = samples;
    processTemporalMeasurements();
}

void WaveformWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_A && !event->isAutoRepeat())
    {
        areaMode_ = true;
        areaModeLabelMuted_ = false;
        clearAreaAnalysis();
        temporalArea_ = {};
        hoverActive_ = false;
        measureActive_ = false;
        updateInteractionCursor();
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_R && !event->isAutoRepeat())
    {
        multiburstMeasurementActive_ = false;
        multiburstStatus_.clear();
        referenceAnalysis_.frequencyMHz = 0.0;

        referenceMode_ = true;
        referenceModeLabelMuted_ = false;
        referenceSelecting_ = false;
        temporalReference_ = {};
        hoverActive_ = false;
        measureActive_ = false;
        areaSelecting_ = false;
        updateInteractionCursor();
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_D && !event->isAutoRepeat())
    {
        probeDetailsMode_ = true;
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_M && !event->isAutoRepeat())
    {
        clearMeasurements();
        areaMode_ = false;
        referenceMode_ = false;
        areaModeLabelMuted_ = false;
        referenceModeLabelMuted_ = false;
        hoverActive_ = false;
        measureActive_ = false;
        multiburstSearchFrames_ = 0;
        multiburstMeasureStep_ = 0;
        multiburstPending_ = false;
        multiburstDebugActivity_.clear();
        multiburstDebugBaseline_.clear();
        multiburstDebugThreshold_ = 0.0;
        multiburstDebugMaximum_ = 0.0;
        multiburstDebugCandidateRects_.clear();
        multiburstDebugCandidateCount_ = 0;

        const MultiburstLayout layout =
            detectMultiburstLayout();

        if (layout.valid &&
            layout.burstRects.size() >= kMinimumMultiburstBursts)
        {
            multiburstMeasurementActive_ = true;

            temporalReference_ = {};
            temporalReference_.active = true;
            temporalReference_.selectionRect = layout.referenceRect;
            temporalReference_.representative.frequencyMHz =
                layout.referenceFrequencyMHz;

            const int burstCount =
                static_cast<int>(layout.burstRects.size());

            temporalMultiburst_.clear();
            temporalMultiburst_.resize(burstCount);
            multiburstAnalyses_.clear();
            multiburstAnalyses_.resize(burstCount);

            for (int i = 0; i < burstCount; ++i)
            {
                temporalMultiburst_[i].active = true;
                temporalMultiburst_[i].selectionRect = layout.burstRects[i];
            }

            multiburstStatus_ =
                QStringLiteral("MULTIBURST  MEASURE  0/%1")
                    .arg(kTemporalMeasurementFrames);
        }
        else
        {
            multiburstMeasurementActive_ = false;
            multiburstStatus_ =
                QStringLiteral("MULTIBURST  NOT FOUND");
        }

        updateInteractionCursor();
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_C && !event->isAutoRepeat())
    {
        clearMeasurements();
        areaMode_ = false;
        referenceMode_ = false;
        updateInteractionCursor();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape)
    {
        clearAreaAnalysis();
        areaMode_ = false;
        referenceMode_ = false;
        referenceSelecting_ = false;
        referenceAnalysis_ = {};
        updateInteractionCursor();
        update();
        event->accept();
        return;
    }

    VideoWidget::keyPressEvent(event);
}

void WaveformWidget::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_D && !event->isAutoRepeat())
    {
        probeDetailsMode_ = false;
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_A && !event->isAutoRepeat())
    {
        areaMode_ = false;
        areaSelecting_ = false;
        updateInteractionCursor();
        update();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_R && !event->isAutoRepeat())
    {
        referenceMode_ = false;
        referenceSelecting_ = false;
        updateInteractionCursor();
        update();
        event->accept();
        return;
    }

    VideoWidget::keyReleaseEvent(event);
}

void WaveformWidget::leaveEvent(QEvent* event)
{
    hoverActive_ = false;
    measureActive_ = false;
    areaSelecting_ = false;
    referenceSelecting_ = false;
    referenceLevelDragging_ = false;
    referenceLevelDragIndex_ = -1;
    areaLevelDragging_ = false;
    areaLevelDragIndex_ = -1;
    multiburstLevelDragging_ = false;
    multiburstDragMeasurementIndex_ = -1;
    multiburstLevelDragIndex_ = -1;
    measurementTableDragging_ = false;
    updateInteractionCursor();
    update();
    QWidget::leaveEvent(event);
}

void WaveformWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);

    const QRect displayRect = imageRect();
    const QRectF scope = scopeRect(displayRect);

    if (!areaMode_ && !referenceMode_ &&
        event->button() == Qt::LeftButton)
    {
        if (!measurementTableRect_.isEmpty() &&
            measurementTableRect_.contains(
                event->position()))
        {
            measurementTableDragging_ = true;
            measurementTableUserPositioned_ = true;
            measurementTableDragOffset_ =
                event->position() -
                measurementTableRect_.topLeft();
            measurementTablePosition_ =
                measurementTableRect_.topLeft();
            hoverActive_ = false;
            updateInteractionCursor();
            event->accept();
            return;
        }

        const int referenceLevel =
            referenceLevelHit(event->position());

        if (referenceLevel >= 0)
        {
            referenceLevelDragging_ = true;
            referenceLevelDragIndex_ = referenceLevel;
            hoverActive_ = false;
            updateInteractionCursor();
            event->accept();
            return;
        }

        const int areaLevel =
            areaLevelHit(event->position());

        if (areaLevel >= 0)
        {
            areaLevelDragging_ = true;
            areaLevelDragIndex_ = areaLevel;
            hoverActive_ = false;
            updateInteractionCursor();
            event->accept();
            return;
        }


        int multiburstMeasurementIndex = -1;
        const int multiburstLevel =
            multiburstLevelHit(
                event->position(),
                &multiburstMeasurementIndex);

        if (multiburstLevel >= 0 &&
            multiburstMeasurementIndex >= 0)
        {
            multiburstLevelDragging_ = true;
            multiburstDragMeasurementIndex_ =
                multiburstMeasurementIndex;
            multiburstLevelDragIndex_ = multiburstLevel;
            hoverActive_ = false;
            updateInteractionCursor();
            event->accept();
            return;
        }
    }

    if (referenceMode_ &&
        event->button() == Qt::LeftButton &&
        scope.contains(event->position()))
    {
        referenceSelecting_ = true;
        referenceStartPosition_ = clampPointToRect(event->position(), scope);
        referenceCurrentPosition_ = referenceStartPosition_;
        hoverActive_ = false;
        event->accept();
        update();
        return;
    }

    if (areaMode_ &&
        event->button() == Qt::LeftButton &&
        scope.contains(event->position()))
    {
        areaSelecting_ = true;
        areaStartPosition_ = clampPointToRect(event->position(), scope);
        areaCurrentPosition_ = areaStartPosition_;
        areaAnalysis_ = {};
        hoverActive_ = false;
        event->accept();
        update();
        return;
    }

    if (!areaMode_ && !referenceMode_ &&
        event->button() == Qt::LeftButton &&
        displayRect.contains(event->position().toPoint()))
    {
        measureActive_ = true;
        measureStartPosition_ = clampPointToRect(event->position(), scope);
        measureCurrentPosition_ = measureStartPosition_;
        hoverActive_ = false;
        event->accept();
        update();
        return;
    }

    if (zoomFactor_ > 1 &&
        event->button() == Qt::RightButton &&
        scope.contains(event->position()))
    {
        panActive_ = true;
        panStartX_ = event->position().x();
        panStartScrollPosition_ = scrollPosition_;
        hoverActive_ = false;
        updateInteractionCursor();
        event->accept();
        return;
    }

    VideoWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QRect displayRect = imageRect();
    const QRectF scope = scopeRect(displayRect);

    if (measurementTableDragging_ &&
        (event->buttons() & Qt::LeftButton) != 0)
    {
        QPointF position =
            event->position() -
            measurementTableDragOffset_;

        const double maximumX =
            (std::max)(
                scope.left(),
                scope.right() -
                    measurementTableRect_.width());

        const double maximumY =
            (std::max)(
                scope.top(),
                scope.bottom() -
                    measurementTableRect_.height());

        position.setX(
            std::clamp(
                position.x(),
                scope.left(),
                maximumX));

        position.setY(
            std::clamp(
                position.y(),
                scope.top(),
                maximumY));

        measurementTablePosition_ = position;
        event->accept();
        update();
        return;
    }

    if (referenceLevelDragging_ &&
        (event->buttons() & Qt::LeftButton) != 0)
    {
        const double volts = displayYToVolts(event->position().y());

        if (referenceLevelDragIndex_ == 0)
        {
            referenceAnalysis_.lowVolts =
                (std::min)(volts, referenceAnalysis_.highVolts);
        }
        else if (referenceLevelDragIndex_ == 1)
        {
            referenceAnalysis_.highVolts =
                (std::max)(volts, referenceAnalysis_.lowVolts);
        }

        referenceAnalysis_.vppVolts =
            referenceAnalysis_.highVolts -
            referenceAnalysis_.lowVolts;
        referenceAnalysis_.vppMillivolts =
            static_cast<int>(
                std::lround(referenceAnalysis_.vppVolts * 1000.0));

        event->accept();
        update();
        return;
    }

    if (areaLevelDragging_ &&
        (event->buttons() & Qt::LeftButton) != 0)
    {
        const double volts = displayYToVolts(event->position().y());

        if (areaLevelDragIndex_ == 0)
        {
            areaAnalysis_.lowVolts =
                (std::min)(volts, areaAnalysis_.highVolts);
        }
        else if (areaLevelDragIndex_ == 1)
        {
            areaAnalysis_.highVolts =
                (std::max)(volts, areaAnalysis_.lowVolts);
        }

        const double vppVolts =
            areaAnalysis_.highVolts -
            areaAnalysis_.lowVolts;

        areaAnalysis_.vppMillivolts =
            static_cast<int>(
                std::lround(vppVolts * 1000.0));

        areaAnalysis_.vppTop.setY(
            voltsToDisplayY(areaAnalysis_.highVolts));
        areaAnalysis_.vppBottom.setY(
            voltsToDisplayY(areaAnalysis_.lowVolts));

        event->accept();
        update();
        return;
    }

    if (multiburstLevelDragging_ &&
        (event->buttons() & Qt::LeftButton) != 0 &&
        multiburstDragMeasurementIndex_ >= 0 &&
        multiburstDragMeasurementIndex_ < multiburstAnalyses_.size())
    {
        AreaAnalysisResult& analysis =
            multiburstAnalyses_[multiburstDragMeasurementIndex_];

        const double volts =
            displayYToVolts(event->position().y());

        if (multiburstLevelDragIndex_ == 0)
        {
            analysis.lowVolts =
                (std::min)(volts, analysis.highVolts);
        }
        else if (multiburstLevelDragIndex_ == 1)
        {
            analysis.highVolts =
                (std::max)(volts, analysis.lowVolts);
        }

        const double vppVolts =
            analysis.highVolts - analysis.lowVolts;

        analysis.vppMillivolts =
            static_cast<int>(
                std::lround(vppVolts * 1000.0));

        analysis.vppTop.setY(
            voltsToDisplayY(analysis.highVolts));
        analysis.vppBottom.setY(
            voltsToDisplayY(analysis.lowVolts));

        event->accept();
        update();
        return;
    }

    if (referenceSelecting_ && (event->buttons() & Qt::LeftButton) != 0)
    {
        referenceCurrentPosition_ = clampPointToRect(event->position(), scope);
        event->accept();
        update();
        return;
    }

    if (areaSelecting_ && (event->buttons() & Qt::LeftButton) != 0)
    {
        areaCurrentPosition_ = clampPointToRect(event->position(), scope);
        event->accept();
        update();
        return;
    }

    if (measureActive_ && (event->buttons() & Qt::LeftButton) != 0)
    {
        measureCurrentPosition_ = clampPointToRect(event->position(), scope);
        event->accept();
        update();
        return;
    }

    if (panActive_ && (event->buttons() & Qt::RightButton) != 0 && zoomFactor_ > 1)
    {
        const double displayWidth = static_cast<double>((std::max)(displayRect.width(), 1));
        const double dragPixels = event->position().x() - panStartX_;
        const double normalizedDelta =
            dragPixels /
            (displayWidth * static_cast<double>(zoomFactor_ - 1));

        setScrollPosition(panStartScrollPosition_ - normalizedDelta);
        event->accept();
        return;
    }

    if (event->buttons() == Qt::NoButton)
    {
        if (!areaMode_ && !referenceMode_ &&
            !measurementTableRect_.isEmpty() &&
            measurementTableRect_.contains(
                event->position()))
        {
            setCursor(Qt::SizeAllCursor);
        }
        else if (!areaMode_ && !referenceMode_ &&
            (referenceLevelHit(event->position()) >= 0 ||
                areaLevelHit(event->position()) >= 0 ||
                multiburstLevelHit(event->position()) >= 0))
        {
            setCursor(Qt::SizeVerCursor);
        }
        else
        {
            updateInteractionCursor();
        }

        updateHover(event->position());
    }

    VideoWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (measurementTableDragging_ &&
        event->button() == Qt::LeftButton)
    {
        measurementTableDragging_ = false;
        updateInteractionCursor();
        event->accept();
        update();
        return;
    }

    if (multiburstLevelDragging_ && event->button() == Qt::LeftButton)
    {
        multiburstLevelDragging_ = false;
        multiburstDragMeasurementIndex_ = -1;
        multiburstLevelDragIndex_ = -1;
        updateInteractionCursor();
        event->accept();
        update();
        return;
    }

    if (referenceLevelDragging_ && event->button() == Qt::LeftButton)
    {
        referenceLevelDragging_ = false;
        referenceLevelDragIndex_ = -1;
        updateInteractionCursor();
        event->accept();
        update();
        return;
    }

    if (areaLevelDragging_ && event->button() == Qt::LeftButton)
    {
        areaLevelDragging_ = false;
        areaLevelDragIndex_ = -1;
        updateInteractionCursor();
        event->accept();
        update();
        return;
    }

    if (referenceSelecting_ && event->button() == Qt::LeftButton)
    {
        referenceSelecting_ = false;
        referenceModeLabelMuted_ = true;
        const QRectF selectionRect =
            normalizedRect(referenceStartPosition_, referenceCurrentPosition_);

        temporalReference_ = {};
        temporalReference_.active = true;
        temporalReference_.selectionRect = selectionRect;

        event->accept();
        update();
        return;
    }

    if (areaSelecting_ && event->button() == Qt::LeftButton)
    {
        areaSelecting_ = false;
        areaModeLabelMuted_ = true;
        const QRectF selectionRect =
            normalizedRect(areaStartPosition_, areaCurrentPosition_);

        temporalArea_ = {};
        temporalArea_.active = true;
        temporalArea_.selectionRect = selectionRect;
        areaAnalysis_ = {};

        event->accept();
        update();
        return;
    }

    if (measureActive_ && event->button() == Qt::LeftButton)
    {
        measureActive_ = false;
        updateHover(event->position());
        event->accept();
        update();
        return;
    }

    if (panActive_ && event->button() == Qt::RightButton)
    {
        panActive_ = false;
        updateInteractionCursor();
        updateHover(event->position());
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        clearMeasurements();
        areaMode_ = false;
        referenceMode_ = false;
        areaModeLabelMuted_ = false;
        referenceModeLabelMuted_ = false;
        hoverActive_ = false;
        updateInteractionCursor();
    }

    // Preserve the normal waveform-view double-click behavior.
    VideoWidget::mouseDoubleClickEvent(event);
}

WaveformWidget::MultiburstLayout WaveformWidget::detectMultiburstLayout() const
{
    MultiburstLayout layout;

    // The old FIR / residual-energy detector deliberately disappears here.
    // Multiburst is centred around 50 % video level, so discover periodic
    // regions directly from alternating excursions around that level.
    multiburstDebugActivity_.clear();
    multiburstDebugBaseline_.clear();
    multiburstDebugThreshold_ = 0.0;
    multiburstDebugMaximum_ = 0.0;
    multiburstDebugCandidateRects_.clear();
    multiburstDebugCandidateCount_ = 0;

    const int sampleCount =
        static_cast<int>(measurementLuma_.size());

    if (sampleCount < 512)
    {
        return layout;
    }

    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);

    constexpr double kMidLevelVolts =
        kBlackLevelVolts +
        0.5 * (kWhiteLevelVolts - kBlackLevelVolts);

    // A little dead-band prevents noise near 50 % from making tiny false
    // lobes.  It does NOT determine the measured amplitude or frequency;
    // it is used only to discover where periodic signals live.
    constexpr double kDiscoveryDeadBandVolts = 0.045;
    constexpr int kMinimumLobeSamples = 3;
    constexpr int kMaximumSignalCount =
        kExpectedMultiburstBursts + 1; // Ref + up to six bursts.
    constexpr int kMinimumSignalCount =
        kMinimumMultiburstBursts + 1; // Ref + at least four bursts.

    struct Lobe
    {
        int first = 0;
        int last = 0;
        int sign = 0;

        int length() const
        {
            return last - first + 1;
        }

        double center() const
        {
            return
                0.5 *
                (static_cast<double>(first) +
                    static_cast<double>(last));
        }
    };

    struct SignalGroup
    {
        int firstLobe = 0;
        int lastLobe = 0;
        int firstSample = 0;
        int lastSample = 0;
        double halfPeriodSamples = 0.0;
        double frequencyHz = 0.0;
        int lobeCount = 0;

        int length() const
        {
            return lastSample - firstSample + 1;
        }
    };

    QVector<Lobe> lobes;

    bool inLobe = false;
    int lobeStart = 0;
    int lobeSign = 0;

    for (int i = 0; i < sampleCount; ++i)
    {
        const double value =
            static_cast<double>(measurementLuma_[i]);

        int sign = 0;

        if (value >= kMidLevelVolts + kDiscoveryDeadBandVolts)
        {
            sign = 1;
        }
        else if (value <= kMidLevelVolts - kDiscoveryDeadBandVolts)
        {
            sign = -1;
        }

        if (sign == 0)
        {
            if (inLobe)
            {
                const Lobe lobe
                {
                    lobeStart,
                    i - 1,
                    lobeSign
                };

                if (lobe.length() >= kMinimumLobeSamples)
                {
                    lobes.push_back(lobe);
                }

                inLobe = false;
            }

            continue;
        }

        if (!inLobe)
        {
            inLobe = true;
            lobeStart = i;
            lobeSign = sign;
            continue;
        }

        if (sign != lobeSign)
        {
            const Lobe lobe
            {
                lobeStart,
                i - 1,
                lobeSign
            };

            if (lobe.length() >= kMinimumLobeSamples)
            {
                lobes.push_back(lobe);
            }

            lobeStart = i;
            lobeSign = sign;
        }
    }

    if (inLobe)
    {
        const Lobe lobe
        {
            lobeStart,
            sampleCount - 1,
            lobeSign
        };

        if (lobe.length() >= kMinimumLobeSamples)
        {
            lobes.push_back(lobe);
        }
    }

    if (lobes.size() < 2)
    {
        return layout;
    }

    const auto pairLooksPeriodic =
        [](const Lobe& a,
            const Lobe& b)
        {
            if (a.sign == b.sign)
            {
                return false;
            }

            const double widthA =
                static_cast<double>(a.length());
            const double widthB =
                static_cast<double>(b.length());

            const double widthRatio =
                (std::max)(widthA, widthB) /
                (std::max)(1.0, (std::min)(widthA, widthB));

            // Positive and negative half-cycles of both the square-wave Ref
            // and the sine bursts should be of the same order of duration.
            if (widthRatio > 1.75)
            {
                return false;
            }

            const int gap =
                b.first - a.last - 1;

            const double smallerWidth =
                (std::min)(widthA, widthB);

            // The 45 mV dead-band creates a short quiet gap around each
            // 50%-crossing.  Real separators between bursts are much wider.
            return
                gap <=
                (std::max)(
                    12,
                    static_cast<int>(
                        std::lround(smallerWidth * 0.85)));
        };

    QVector<SignalGroup> groups;

    int lobeIndex = 0;

    while (lobeIndex + 1 < lobes.size())
    {
        if (!pairLooksPeriodic(
                lobes[lobeIndex],
                lobes[lobeIndex + 1]))
        {
            ++lobeIndex;
            continue;
        }

        const int groupStart = lobeIndex;
        int groupEnd = lobeIndex + 1;

        QVector<double> halfPeriods;
        halfPeriods.push_back(
            lobes[groupEnd].center() -
            lobes[groupEnd - 1].center());

        while (groupEnd + 1 < lobes.size())
        {
            const Lobe& previous = lobes[groupEnd];
            const Lobe& next = lobes[groupEnd + 1];

            if (!pairLooksPeriodic(previous, next))
            {
                break;
            }

            const double nextHalfPeriod =
                next.center() - previous.center();

            const double meanHalfPeriod =
                std::accumulate(
                    halfPeriods.cbegin(),
                    halfPeriods.cend(),
                    0.0) /
                static_cast<double>(halfPeriods.size());

            if (relativeVariation(
                    nextHalfPeriod,
                    meanHalfPeriod) > 0.30)
            {
                break;
            }

            halfPeriods.push_back(nextHalfPeriod);
            ++groupEnd;
        }

        const double meanHalfPeriod =
            std::accumulate(
                halfPeriods.cbegin(),
                halfPeriods.cend(),
                0.0) /
            static_cast<double>(halfPeriods.size());

        if (meanHalfPeriod > 1.0)
        {
            SignalGroup group;
            group.firstLobe = groupStart;
            group.lastLobe = groupEnd;
            group.firstSample = lobes[groupStart].first;
            group.lastSample = lobes[groupEnd].last;
            group.halfPeriodSamples = meanHalfPeriod;
            group.frequencyHz =
                geometry.standard.sampleClockHz * 4.0 /
                (2.0 * meanHalfPeriod);
            group.lobeCount = groupEnd - groupStart + 1;
            groups.push_back(group);
        }

        lobeIndex = groupEnd + 1;
    }

    const auto indexToDisplayX =
        [&](int index)
        {
            const double normalized =
                static_cast<double>(index) /
                static_cast<double>(sampleCount - 1);

            return
                geometry.scopeRect.left() +
                normalized * geometry.scopeRect.width();
        };

    multiburstDebugCandidateCount_ =
        static_cast<int>(groups.size());

    multiburstDebugCandidateRects_.reserve(groups.size());

    for (const SignalGroup& group : groups)
    {
        multiburstDebugCandidateRects_.push_back(
            QRectF(
                QPointF(
                    indexToDisplayX(group.firstSample),
                    geometry.scopeRect.top()),
                QPointF(
                    indexToDisplayX(group.lastSample),
                    geometry.scopeRect.bottom()))
                .normalized());
    }

    if (groups.size() < kMinimumSignalCount)
    {
        return layout;
    }

    // If noise produced harmless extra periodic groups, keep at most the
    // reference plus six strongest structures. Lobe count is the primary
    // quality measure; horizontal duration breaks ties and favours the
    // actual multiburst blocks.
    if (groups.size() > kMaximumSignalCount)
    {
        std::sort(
            groups.begin(),
            groups.end(),
            [](const SignalGroup& a,
                const SignalGroup& b)
            {
                if (a.lobeCount != b.lobeCount)
                {
                    return a.lobeCount > b.lobeCount;
                }

                return a.length() > b.length();
            });

        groups.resize(kMaximumSignalCount);
    }

    // The reference is not hard-coded to a location or nominal frequency:
    // measure the discovered periodic groups and take the slowest one.
    int referenceGroupIndex = 0;

    for (int i = 1; i < groups.size(); ++i)
    {
        if (groups[i].frequencyHz <
            groups[referenceGroupIndex].frequencyHz)
        {
            referenceGroupIndex = i;
        }
    }

    const SignalGroup referenceGroup =
        groups[referenceGroupIndex];

    QVector<SignalGroup> burstGroups;
    burstGroups.reserve(kExpectedMultiburstBursts);

    for (int i = 0; i < groups.size(); ++i)
    {
        if (i != referenceGroupIndex)
        {
            burstGroups.push_back(groups[i]);
        }
    }

    std::sort(
        burstGroups.begin(),
        burstGroups.end(),
        [](const SignalGroup& a,
            const SignalGroup& b)
        {
            return a.firstSample < b.firstSample;
        });

    if (burstGroups.size() < kMinimumMultiburstBursts)
    {
        return layout;
    }

    const auto expandedGroupRect =
        [&](const SignalGroup& group,
            int leftLimit,
            int rightLimit)
        {
            const int desiredPadding =
                (std::max)(
                    sampleCount / 48,
                    group.length() / 2);

            int first =
                (std::max)(leftLimit, group.firstSample - desiredPadding);
            int last =
                (std::min)(rightLimit, group.lastSample + desiredPadding);

            if (last <= first)
            {
                first = group.firstSample;
                last = group.lastSample;
            }

            return QRectF(
                QPointF(
                    indexToDisplayX(first),
                    geometry.scopeRect.top()),
                QPointF(
                    indexToDisplayX(last),
                    geometry.scopeRect.bottom()))
                .normalized();
        };

    // Reference gets a generous window around its two dominant lobes.  The
    // existing reference analyser then determines the actual HIGH/LOW levels
    // over four frames; discovery does not invent the Ref amplitude.
    int referenceLeftLimit = 0;
    int referenceRightLimit = sampleCount - 1;

    for (const SignalGroup& group : groups)
    {
        if (group.firstSample > referenceGroup.lastSample)
        {
            referenceRightLimit =
                referenceGroup.lastSample +
                (group.firstSample - referenceGroup.lastSample) / 2;
            break;
        }
    }

    layout.referenceRect =
        expandedGroupRect(
            referenceGroup,
            referenceLeftLimit,
            referenceRightLimit);

    layout.referenceFrequencyMHz =
        referenceGroup.frequencyHz / 1.0e6;

    layout.burstRects.clear();
    layout.burstRects.reserve(kExpectedMultiburstBursts);

    for (int i = 0; i < burstGroups.size(); ++i)
    {
        const SignalGroup& group = burstGroups[i];

        int leftLimit = 0;
        int rightLimit = sampleCount - 1;

        if (i > 0)
        {
            const SignalGroup& previous = burstGroups[i - 1];
            leftLimit =
                previous.lastSample +
                (group.firstSample - previous.lastSample) / 2;
        }
        else if (referenceGroup.firstSample < group.firstSample)
        {
            leftLimit =
                referenceGroup.lastSample +
                (group.firstSample - referenceGroup.lastSample) / 2;
        }

        if (i + 1 < burstGroups.size())
        {
            const SignalGroup& next = burstGroups[i + 1];
            rightLimit =
                group.lastSample +
                (next.firstSample - group.lastSample) / 2;
        }

        layout.burstRects.push_back(
            expandedGroupRect(
                group,
                leftLimit,
                rightLimit));
    }

    layout.valid = true;
    return layout;
}

WaveformWidget::AreaAnalysisResult WaveformWidget::analyzeSelection(
    const QRectF& selectionRect) const
{
    AreaAnalysisResult result;
    result.attempted = true;
    result.selectionRect = selectionRect;

    if (measurementLuma_.size() < 16)
    {
        result.message = QStringLiteral("No reconstructed waveform data");
        return result;
    }

    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);

    const QRectF analysisRect =
        selectionRect.intersected(geometry.scopeRect);

    result.selectionRect = analysisRect;

    if (analysisRect.width() < 12.0)
    {
        result.message = QStringLiteral("Selection too small");
        return result;
    }

    const auto displayXToIndex =
        [&](double displayX)
        {
            const double normalized =
                std::clamp(
                    (displayX - geometry.scopeRect.left()) /
                        (std::max)(geometry.scopeRect.width(), 1.0),
                    0.0,
                    1.0);

            return static_cast<int>(
                std::lround(
                    normalized *
                    static_cast<double>(measurementLuma_.size() - 1)));
        };

    const int measurementSampleCount =
        static_cast<int>(measurementLuma_.size());

    int firstIndex = displayXToIndex(analysisRect.left());
    int lastIndex = displayXToIndex(analysisRect.right());

    if (lastIndex < firstIndex)
    {
        std::swap(firstIndex, lastIndex);
    }

    firstIndex = std::clamp(
        firstIndex,
        0,
        measurementSampleCount - 1);

    lastIndex = std::clamp(
        lastIndex,
        0,
        measurementSampleCount - 1);

    if (lastIndex - firstIndex < 12)
    {
        result.message = QStringLiteral("Selection too small");
        return result;
    }

    QVector<double> selected;
    selected.reserve(lastIndex - firstIndex + 1);

    for (int index = firstIndex; index <= lastIndex; ++index)
    {
        selected.append(
            static_cast<double>(measurementLuma_[index]));
    }

    const double mean =
        std::accumulate(
            selected.begin(),
            selected.end(),
            0.0) /
        static_cast<double>(selected.size());

    QVector<double> crossings;
    for (int i = 0; i + 1 < selected.size(); ++i)
    {
        const double a = selected[i] - mean;
        const double b = selected[i + 1] - mean;

        if (a <= 0.0 && b > 0.0)
        {
            const double denominator = b - a;
            const double fraction =
                std::abs(denominator) > 1.0e-12
                ? -a / denominator
                : 0.0;

            crossings.append(
                static_cast<double>(i) +
                std::clamp(fraction, 0.0, 1.0));
        }
    }

    if (crossings.size() < kMinimumStableCycles + 1)
    {
        result.message = QStringLiteral("No stable sinus found");
        return result;
    }

    QVector<CandidateCycle> cycles;

    const int selectedSampleCount =
        static_cast<int>(selected.size());

    for (int crossing = 0;
        crossing + 1 < crossings.size();
        ++crossing)
    {
        const double startCrossing = crossings[crossing];
        const double endCrossing = crossings[crossing + 1];

        const int cycleStart =
            std::clamp(
                static_cast<int>(std::floor(startCrossing)),
                0,
                selectedSampleCount - 1);

        const int cycleEnd =
            std::clamp(
                static_cast<int>(std::ceil(endCrossing)),
                0,
                selectedSampleCount - 1);

        if (cycleEnd - cycleStart < 4)
        {
            continue;
        }

        double minimum = selected[cycleStart];
        int minimumIndex = cycleStart;

        double cycleSum = 0.0;
        int cycleSampleCount = 0;

        for (int i = cycleStart; i <= cycleEnd; ++i)
        {
            minimum = (std::min)(minimum, selected[i]);

            if (selected[i] <= minimum)
            {
                minimumIndex = i;
            }

            cycleSum += selected[i];
            ++cycleSampleCount;
        }

        if (cycleSampleCount <= 0)
        {
            continue;
        }

        const double cycleMean =
            cycleSum /
            static_cast<double>(cycleSampleCount);

        double cycleSquaredAcSum = 0.0;

        for (int i = cycleStart; i <= cycleEnd; ++i)
        {
            const double ac =
                selected[i] - cycleMean;

            cycleSquaredAcSum +=
                ac * ac;
        }

        const double cycleRms =
            std::sqrt(
                cycleSquaredAcSum /
                static_cast<double>(cycleSampleCount));

        // For a sine wave:
        //   Vrms = Vpeak / sqrt(2)
        //   Vpp  = 2 * Vpeak
        // therefore:
        //   Vpp = 2 * sqrt(2) * Vrms
        //
        // This is much less sensitive than max-min to the reconstructed
        // sample phase landing between the true peaks at high frequencies.
        const double vppVolts =
            2.0 *
            std::sqrt(2.0) *
            cycleRms;

        const double peakVolts =
            vppVolts * 0.5;

        if (peakVolts < kMinimumPeakVolts)
        {
            continue;
        }

        CandidateCycle cycle;
        cycle.startIndex = cycleStart;
        cycle.endIndex = cycleEnd;
        cycle.troughIndex = minimumIndex;
        cycle.periodSamples = endCrossing - startCrossing;
        cycle.vppVolts = vppVolts;
        cycles.push_back(cycle);
    }

    if (cycles.size() < kMinimumStableCycles)
    {
        result.message = QStringLiteral("No stable sinus found");
        return result;
    }

    int bestRunStart = -1;
    int bestRunLength = 0;
    double bestMeanPeriod = 0.0;
    double bestMeanVpp = 0.0;

    for (int start = 0; start < cycles.size(); ++start)
    {
        double meanPeriod = cycles[start].periodSamples;
        double meanVpp = cycles[start].vppVolts;
        int count = 1;

        for (int i = start + 1; i < cycles.size(); ++i)
        {
            if (relativeVariation(
                    cycles[i].periodSamples,
                    meanPeriod) > kMaximumVariation ||
                relativeVariation(
                    cycles[i].vppVolts,
                    meanVpp) > kMaximumVariation)
            {
                break;
            }

            meanPeriod =
                (meanPeriod * count + cycles[i].periodSamples) /
                static_cast<double>(count + 1);

            meanVpp =
                (meanVpp * count + cycles[i].vppVolts) /
                static_cast<double>(count + 1);

            ++count;
        }

        if (count > bestRunLength)
        {
            bestRunStart = start;
            bestRunLength = count;
            bestMeanPeriod = meanPeriod;
            bestMeanVpp = meanVpp;
        }
    }

    if (bestRunStart < 0 ||
        bestRunLength < kMinimumStableCycles)
    {
        result.message = QStringLiteral("No stable sinus found");
        return result;
    }

    const int representativeCycleIndex =
        bestRunStart + bestRunLength / 2;

    const CandidateCycle& representative =
        cycles[representativeCycleIndex];

    const double reconstructedSampleClockHz =
        geometry.standard.sampleClockHz * 4.0;

    result.valid = true;
    result.frequencyMHz =
        reconstructedSampleClockHz /
        bestMeanPeriod /
        1.0e6;

    // Measure frequency first from the stable zero-crossing periods.
    // Then fit DC + sin + cos at that measured frequency over the complete
    // stable run.  This does not rely on a reconstructed sample landing on
    // the actual sine peak, and does not assume a nominal multiburst value.
    const CandidateCycle& firstStableCycle =
        cycles[bestRunStart];

    const CandidateCycle& lastStableCycle =
        cycles[bestRunStart + bestRunLength - 1];

    const int stableStartIndex =
        firstStableCycle.startIndex;

    const int stableEndIndex =
        lastStableCycle.endIndex;

    const SineFitResult sineFit =
        fitSineAtMeasuredPeriod(
            selected,
            stableStartIndex,
            stableEndIndex,
            bestMeanPeriod);

    if (!sineFit.valid ||
        sineFit.peak < kMinimumPeakVolts)
    {
        result.valid = false;
        result.message = QStringLiteral("Sine fit failed");
        return result;
    }

    result.vppMillivolts =
        static_cast<int>(
            std::lround(
                sineFit.vpp * 1000.0));

    const auto localIndexToDisplayX =
        [&](double localIndex)
        {
            const double absoluteIndex =
                static_cast<double>(firstIndex) +
                localIndex;

            const double normalized =
                absoluteIndex /
                static_cast<double>(measurementLuma_.size() - 1);

            return
                geometry.scopeRect.left() +
                normalized * geometry.scopeRect.width();
        };

    const double representativeStartLocal =
        static_cast<double>(representative.startIndex);

    const double representativeEndLocal =
        static_cast<double>(representative.endIndex);

    result.periodStart = QPointF(
        localIndexToDisplayX(representativeStartLocal),
        geometry.scopeRect.center().y());

    result.periodEnd = QPointF(
        localIndexToDisplayX(representativeEndLocal),
        geometry.scopeRect.center().y());

    const double fittedMaximum =
        sineFit.dc + sineFit.peak;

    const double fittedMinimum =
        sineFit.dc - sineFit.peak;

    const auto voltsToDisplayY =
        [&](double volts)
        {
            return
                geometry.zeroVoltY -
                volts /
                geometry.voltsPerDisplayPixel;
        };

    const double verticalArrowX =
        0.5 *
        (result.periodStart.x() +
            result.periodEnd.x());

    result.vppTop = QPointF(
        verticalArrowX,
        voltsToDisplayY(fittedMaximum));

    result.vppBottom = QPointF(
        verticalArrowX,
        voltsToDisplayY(fittedMinimum));

    result.lowVolts = fittedMinimum;
    result.highVolts = fittedMaximum;

    result.message = QStringLiteral("Stable sinus found");
    return result;
}

WaveformWidget::ReferenceAnalysisResult WaveformWidget::analyzeReferenceSelection(
    const QRectF& selectionRect) const
{
    ReferenceAnalysisResult result;

    if (measurementLuma_.size() < 16)
    {
        result.message = QStringLiteral("No reconstructed waveform data");
        return result;
    }

    const QRect displayRect = imageRect();
    const WaveformGeometry geometry =
        makeGeometry(image(), displayRect, this);

    const QRectF analysisRect =
        selectionRect.intersected(geometry.scopeRect);

    if (analysisRect.width() < 12.0)
    {
        result.message = QStringLiteral("Reference selection too small");
        return result;
    }

    const int sampleCount =
        static_cast<int>(measurementLuma_.size());

    const auto displayXToIndex =
        [&](double displayX)
        {
            const double normalized =
                std::clamp(
                    (displayX - geometry.scopeRect.left()) /
                        (std::max)(geometry.scopeRect.width(), 1.0),
                    0.0,
                    1.0);

            return static_cast<int>(
                std::lround(
                    normalized *
                    static_cast<double>(sampleCount - 1)));
        };

    int firstIndex = displayXToIndex(analysisRect.left());
    int lastIndex = displayXToIndex(analysisRect.right());
    if (lastIndex < firstIndex)
    {
        std::swap(firstIndex, lastIndex);
    }

    firstIndex = std::clamp(firstIndex, 0, sampleCount - 1);
    lastIndex = std::clamp(lastIndex, 0, sampleCount - 1);

    if (lastIndex - firstIndex < 12)
    {
        result.message = QStringLiteral("Reference selection too small");
        return result;
    }

    // Find stable horizontal runs first, then choose the two dominant
    // plateau levels by total horizontal occupancy. This prevents a short
    // third level (for example black before/after the actual reference)
    // from stealing LOW/HIGH merely because it is more extreme.
    QVector<double> referenceSamples;
    referenceSamples.reserve(lastIndex - firstIndex + 1);

    for (int index = firstIndex; index <= lastIndex; ++index)
    {
        referenceSamples.append(
            static_cast<double>(measurementLuma_[index]));
    }

    const auto [minimumIt, maximumIt] =
        std::minmax_element(
            referenceSamples.begin(),
            referenceSamples.end());

    const double selectionSpan =
        *maximumIt - *minimumIt;

    if (selectionSpan >= 0.1)
    {
        struct PlateauRun
        {
            double mean = 0.0;
            int sampleCount = 0;
        };

        struct PlateauCluster
        {
            double weightedSum = 0.0;
            int sampleCount = 0;

            [[nodiscard]] double mean() const
            {
                return sampleCount > 0
                    ? weightedSum / static_cast<double>(sampleCount)
                    : 0.0;
            }
        };

        // A stable sample may still contain normal video/noise ripple. The
        // slope threshold is relative to the complete selected excursion,
        // with a small absolute floor for quiet sources.
        const double stableSlopeThreshold =
            (std::max)(
                0.0025,
                selectionSpan * 0.018);

        constexpr int kMinimumStableRunSamples = 8;

        QVector<PlateauRun> runs;

        int runStart = -1;

        const auto finishRun =
            [&](int runEnd)
            {
                if (runStart < 0 || runEnd < runStart)
                {
                    return;
                }

                const int count =
                    runEnd - runStart + 1;

                if (count < kMinimumStableRunSamples)
                {
                    return;
                }

                double sum = 0.0;
                for (int index = runStart; index <= runEnd; ++index)
                {
                    sum += referenceSamples[index];
                }

                PlateauRun run;
                run.mean = sum / static_cast<double>(count);
                run.sampleCount = count;
                runs.append(run);
            };

        for (int index = 1;
            index + 1 < static_cast<int>(referenceSamples.size());
            ++index)
        {
            const double leftSlope =
                std::abs(
                    referenceSamples[index] -
                    referenceSamples[index - 1]);

            const double rightSlope =
                std::abs(
                    referenceSamples[index + 1] -
                    referenceSamples[index]);

            const bool stable =
                leftSlope <= stableSlopeThreshold &&
                rightSlope <= stableSlopeThreshold;

            if (stable)
            {
                if (runStart < 0)
                {
                    runStart = index;
                }
            }
            else if (runStart >= 0)
            {
                finishRun(index - 1);
                runStart = -1;
            }
        }

        if (runStart >= 0)
        {
            finishRun(
                static_cast<int>(referenceSamples.size()) - 2);
        }

        if (runs.size() >= 2)
        {
            std::sort(
                runs.begin(),
                runs.end(),
                [](const PlateauRun& a, const PlateauRun& b)
                {
                    return a.mean < b.mean;
                });

            // Merge stable runs that belong to the same physical plateau.
            // Keep this considerably tighter than the minimum useful REF
            // excursion so separate reference levels cannot collapse.
            const double mergeTolerance =
                (std::max)(
                    0.008,
                    selectionSpan * 0.045);

            QVector<PlateauCluster> clusters;

            for (const PlateauRun& run : runs)
            {
                if (clusters.isEmpty() ||
                    std::abs(
                        run.mean -
                        clusters.last().mean()) > mergeTolerance)
                {
                    PlateauCluster cluster;
                    cluster.weightedSum =
                        run.mean * static_cast<double>(run.sampleCount);
                    cluster.sampleCount = run.sampleCount;
                    clusters.append(cluster);
                }
                else
                {
                    PlateauCluster& cluster = clusters.last();
                    cluster.weightedSum +=
                        run.mean * static_cast<double>(run.sampleCount);
                    cluster.sampleCount += run.sampleCount;
                }
            }

            int bestFirst = -1;
            int bestSecond = -1;
            int bestCombinedSamples = -1;
            int bestSmallerPlateau = -1;

            constexpr double kMinimumReferenceSpanVolts = 0.1;

            for (int first = 0;
                first < static_cast<int>(clusters.size());
                ++first)
            {
                for (int second = first + 1;
                    second < static_cast<int>(clusters.size());
                    ++second)
                {
                    const double span =
                        clusters[second].mean() -
                        clusters[first].mean();

                    if (span < kMinimumReferenceSpanVolts)
                    {
                        continue;
                    }

                    const int combinedSamples =
                        clusters[first].sampleCount +
                        clusters[second].sampleCount;

                    const int smallerPlateau =
                        (std::min)(
                            clusters[first].sampleCount,
                            clusters[second].sampleCount);

                    // Primary score: how much of the selected X-range is
                    // explained by the pair. Tie-breaker: prefer a pair in
                    // which BOTH plateaus have substantial occupancy.
                    if (combinedSamples > bestCombinedSamples ||
                        (combinedSamples == bestCombinedSamples &&
                            smallerPlateau > bestSmallerPlateau))
                    {
                        bestCombinedSamples = combinedSamples;
                        bestSmallerPlateau = smallerPlateau;
                        bestFirst = first;
                        bestSecond = second;
                    }
                }
            }

            if (bestFirst >= 0 && bestSecond >= 0)
            {
                const double lowCenter =
                    clusters[bestFirst].mean();
                const double highCenter =
                    clusters[bestSecond].mean();
                const double dominantSpan =
                    highCenter - lowCenter;

                // Recollect actual samples near the two winning plateau
                // centres so RMS is still calculated from source samples,
                // not from run means.
                const double plateauBand =
                    (std::max)(
                        mergeTolerance,
                        dominantSpan * 0.06);

                QVector<double> lowCluster;
                QVector<double> highCluster;

                for (double sample : referenceSamples)
                {
                    if (std::abs(sample - lowCenter) <= plateauBand)
                    {
                        lowCluster.append(sample);
                    }
                    else if (std::abs(sample - highCenter) <= plateauBand)
                    {
                        highCluster.append(sample);
                    }
                }

                if (lowCluster.size() >= 8 &&
                    highCluster.size() >= 8)
                {
                    const double refinedLowCenter =
                        std::accumulate(
                            lowCluster.begin(),
                            lowCluster.end(),
                            0.0) /
                        static_cast<double>(lowCluster.size());

                    const double refinedHighCenter =
                        std::accumulate(
                            highCluster.begin(),
                            highCluster.end(),
                            0.0) /
                        static_cast<double>(highCluster.size());

                    const double refinedSpan =
                        refinedHighCenter - refinedLowCenter;
                    const double lowSigma =
                        standardDeviation(lowCluster, refinedLowCenter);
                    const double highSigma =
                        standardDeviation(highCluster, refinedHighCenter);

                    if (refinedSpan >= kMinimumReferenceSpanVolts &&
                        lowSigma <= refinedSpan * 0.10 &&
                        highSigma <= refinedSpan * 0.10)
                    {
                        const double lowRms = rms(lowCluster);
                        const double highRms = rms(highCluster);
                        const double referenceVpp =
                            std::abs(highRms - lowRms);

                        if (referenceVpp >= kMinimumReferenceSpanVolts)
                        {
                            result.valid = true;
                            result.selectionRect = analysisRect;
                            result.lowVolts =
                                (std::min)(lowRms, highRms);
                            result.highVolts =
                                (std::max)(lowRms, highRms);
                            result.vppVolts = referenceVpp;
                            result.vppMillivolts =
                                static_cast<int>(
                                    std::lround(
                                        referenceVpp * 1000.0));
                            result.message =
                                QStringLiteral(
                                    "Reference set from dominant plateaus");
                            return result;
                        }
                    }
                }
            }
        }
    }

    result.message = QStringLiteral("Reference plateaus not found");
    return result;



}

void WaveformWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (image().isNull())
    {
        return;
    }

    const QRect displayRect = imageRect();
    painter.drawImage(displayRect, image());

    const WaveformGeometry geometry = makeGeometry(image(), displayRect, this);

    const double measurementLabelLineGap =
        std::max(
            16.0,
            geometry.scopeRect.height() * 0.018);

    const auto displayYToVolts =
        [&](double displayY)
        {
            return (geometry.zeroVoltY - displayY) * geometry.voltsPerDisplayPixel;
        };

    const auto voltsToDisplayY =
        [&](double volts)
        {
            return geometry.zeroVoltY - volts / geometry.voltsPerDisplayPixel;
        };

    const double infoBandTop =
        std::min(
            voltsToDisplayY(0.2) + 8.0,
            geometry.scopeRect.bottom() - 32.0);

    WaveformGraticule measurementGraticule;
    QFont measurementLabelFont =
        measurementGraticule.labelFont(
            QApplication::font(),
            geometry.scopeRect.height());
    measurementLabelFont.setBold(false);
    measurementLabelFont.setPixelSize(
        (std::max)(
            1,
            static_cast<int>(
                std::lround(
                    static_cast<double>(measurementLabelFont.pixelSize()) *
                    0.80))));

    if (!multiburstDebugActivity_.isEmpty() &&
        multiburstDebugMaximum_ > 0.0)
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Exact 30 kHz FIR baseline used by M.  Plot it in waveform
        // coordinates so edge behaviour and baseline tracking are directly
        // visible against the source trace.
        if (multiburstDebugBaseline_.size() > 1 &&
            geometry.voltsPerDisplayPixel > 0.0)
        {
            QPolygonF baselineCurve;
            baselineCurve.reserve(multiburstDebugBaseline_.size());

            const int baselineCount =
                static_cast<int>(multiburstDebugBaseline_.size());

            for (int i = 0; i < baselineCount; ++i)
            {
                const double normalizedX =
                    static_cast<double>(i) /
                    static_cast<double>(baselineCount - 1);

                const double y =
                    geometry.zeroVoltY -
                    multiburstDebugBaseline_[i] /
                        geometry.voltsPerDisplayPixel;

                baselineCurve.push_back(
                    QPointF(
                        geometry.scopeRect.left() +
                            normalizedX * geometry.scopeRect.width(),
                        y));
            }

            painter.save();
            painter.setClipRect(geometry.scopeRect);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(180, 120, 255, 235), 1.5));
            painter.drawPolyline(baselineCurve);
            painter.restore();

            painter.setFont(measurementLabelFont);
            painter.setPen(QColor(180, 120, 255));
            painter.drawText(
                QRectF(
                    geometry.scopeRect.left() + 10.0,
                    geometry.scopeRect.top() + 34.0,
                    220.0,
                    24.0),
                Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("30 kHz FIR BASELINE"));
        }

        // Candidate islands: red vertical bands.  These are deliberately
        // full-height while debugging so a missed/merged island is obvious.
        painter.setPen(QPen(QColor(255, 80, 80, 230), 1.5, Qt::DashLine));
        painter.setBrush(QColor(255, 60, 60, 32));
        for (const QRectF& candidateRect : multiburstDebugCandidateRects_)
        {
            painter.drawRect(candidateRect.intersected(geometry.scopeRect));
        }

        // AC-energy scope at the bottom of the waveform.  The detector uses
        // this exact activity array, so this is a view of what M sees rather
        // than a second diagnostic calculation.
        const double debugBandHeight =
            geometry.scopeRect.height() * 0.24;
        const double debugBottom =
            geometry.scopeRect.bottom() - 8.0;
        const double debugTop =
            debugBottom - debugBandHeight;

        QPolygonF activityCurve;
        activityCurve.reserve(multiburstDebugActivity_.size());

        const int debugCount =
            static_cast<int>(multiburstDebugActivity_.size());

        for (int i = 0; i < debugCount; ++i)
        {
            const double normalizedX =
                debugCount > 1
                ? static_cast<double>(i) /
                    static_cast<double>(debugCount - 1)
                : 0.0;
            const double normalizedActivity =
                std::clamp(
                    multiburstDebugActivity_[i] /
                        multiburstDebugMaximum_,
                    0.0,
                    1.0);

            activityCurve.push_back(
                QPointF(
                    geometry.scopeRect.left() +
                        normalizedX * geometry.scopeRect.width(),
                    debugBottom -
                        normalizedActivity * debugBandHeight));
        }

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(80, 220, 255, 230), 1.5));
        painter.drawPolyline(activityCurve);

        const double normalizedThreshold =
            std::clamp(
                multiburstDebugThreshold_ /
                    multiburstDebugMaximum_,
                0.0,
                1.0);
        const double thresholdY =
            debugBottom -
            normalizedThreshold * debugBandHeight;

        painter.setPen(QPen(QColor(255, 220, 80, 230), 1.5, Qt::DashLine));
        painter.drawLine(
            QPointF(geometry.scopeRect.left(), thresholdY),
            QPointF(geometry.scopeRect.right(), thresholdY));

        // Small labels make a screen recording self-explanatory.
        painter.setFont(measurementLabelFont);
        painter.setPen(QColor(80, 220, 255));
        painter.drawText(
            QRectF(
                geometry.scopeRect.left() + 10.0,
                debugTop,
                180.0,
                24.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("AC ENERGY"));

        painter.setPen(QColor(255, 220, 80));
        painter.drawText(
            QRectF(
                geometry.scopeRect.left() + 10.0,
                thresholdY - 24.0,
                180.0,
                22.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("THRESHOLD"));

        painter.restore();
    }

    if (!multiburstStatus_.isEmpty())
    {
        painter.save();
        painter.setFont(measurementLabelFont);

        const QFontMetricsF statusMetrics(
            measurementLabelFont,
            painter.device());

        constexpr double kStatusPaddingX = 12.0;
        constexpr double kStatusPaddingY = 6.0;
        constexpr double kStatusMargin = 18.0;

        const QSizeF statusTextSize(
            statusMetrics.horizontalAdvance(multiburstStatus_),
            statusMetrics.height());

        QRectF statusRect(
            geometry.scopeRect.left() + kStatusMargin,
            geometry.scopeRect.top() + kStatusMargin,
            statusTextSize.width() + 2.0 * kStatusPaddingX,
            statusTextSize.height() + 2.0 * kStatusPaddingY);

        if (statusRect.right() > geometry.scopeRect.right())
        {
            statusRect.moveRight(geometry.scopeRect.right());
        }

        painter.setPen(QPen(QColor(255, 220, 80), 1.5));
        painter.setBrush(QColor(0, 0, 0, 220));
        painter.drawRoundedRect(statusRect, 4.0, 4.0);
        painter.drawText(
            statusRect.adjusted(
                kStatusPaddingX,
                kStatusPaddingY,
                -kStatusPaddingX,
                -kStatusPaddingY),
            Qt::AlignCenter,
            multiburstStatus_);
        painter.restore();
    }

    const bool showAreaModeLabel =
        areaMode_ && !areaModeLabelMuted_;
    const bool showReferenceModeLabel =
        referenceMode_ && !referenceModeLabelMuted_;

    if (showAreaModeLabel || showReferenceModeLabel)
    {
        const QString modeText =
            showAreaModeLabel
            ? QStringLiteral("AREA")
            : QStringLiteral("REF");

        const QColor modeColor =
            showAreaModeLabel
            ? QColor(80, 255, 120)
            : QColor(255, 120, 255);

        painter.save();
        painter.setFont(measurementLabelFont);
        painter.setPen(modeColor);

        const QFontMetricsF modeMetrics(
            measurementLabelFont,
            painter.device());

        constexpr double kModeGapX = 18.0;
        constexpr double kModeGapY = 14.0;
        constexpr double kModePaddingX = 6.0;
        constexpr double kModePaddingY = 3.0;

        const QSizeF textSize(
            modeMetrics.horizontalAdvance(modeText),
            modeMetrics.height());

        QRectF modeRect(
            hoverPosition_.x() -
                kModeGapX -
                textSize.width() -
                2.0 * kModePaddingX,
            hoverPosition_.y() -
                kModeGapY -
                textSize.height() -
                2.0 * kModePaddingY,
            textSize.width() + 2.0 * kModePaddingX,
            textSize.height() + 2.0 * kModePaddingY);

        if (modeRect.left() < geometry.scopeRect.left())
        {
            modeRect.moveLeft(geometry.scopeRect.left());
        }
        if (modeRect.top() < geometry.scopeRect.top())
        {
            modeRect.moveTop(geometry.scopeRect.top());
        }
        if (modeRect.right() > geometry.scopeRect.right())
        {
            modeRect.moveRight(geometry.scopeRect.right());
        }
        if (modeRect.bottom() > geometry.scopeRect.bottom())
        {
            modeRect.moveBottom(geometry.scopeRect.bottom());
        }

        painter.setBrush(QColor(0, 0, 0, 190));
        painter.drawRoundedRect(modeRect, 3.0, 3.0);
        painter.drawText(
            modeRect.adjusted(
                kModePaddingX,
                kModePaddingY,
                -kModePaddingX,
                -kModePaddingY),
            Qt::AlignCenter,
            modeText);
        painter.restore();
    }

    const double measurementScale =
        std::clamp(
            static_cast<double>(geometry.scopeRect.height()) / 576.0,
            0.85,
            3.0);
    const double measurementPenWidth =
        2.0 * measurementScale;

    const auto displayXToSourcePixels =
        [&](double displayX)
        {
            const double normalized =
                std::clamp(
                    (displayX - geometry.scopeRect.left()) /
                        (std::max)(geometry.scopeRect.width(), 1.0),
                    0.0,
                    1.0);

            const double visibleSourceWidth =
                static_cast<double>(geometry.standard.sampleWidth) /
                static_cast<double>((std::max)(zoomFactor_, 1));

            const double maxScrollSourcePixels =
                (std::max)(
                    static_cast<double>(geometry.standard.sampleWidth) - visibleSourceWidth,
                    0.0);

            return scrollPosition_ * maxScrollSourcePixels + normalized * visibleSourceWidth;
        };

    if (referenceSelecting_)
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(255, 120, 255), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(
            normalizedRect(
                referenceStartPosition_,
                referenceCurrentPosition_)
                .intersected(geometry.scopeRect));
        painter.restore();
    }

    if (referenceAnalysis_.valid &&
        !referenceAnalysis_.selectionRect.isEmpty())
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        const double levelLineWidth =
            (std::max)(1.5, measurementPenWidth * 0.65);

        painter.setPen(
            QPen(
                QColor(255, 120, 255),
                levelLineWidth));

        const double referenceLowY =
            voltsToDisplayY(referenceAnalysis_.lowVolts);

        const double referenceHighY =
            voltsToDisplayY(referenceAnalysis_.highVolts);

        painter.drawLine(
            QPointF(
                referenceAnalysis_.selectionRect.left(),
                referenceLowY),
            QPointF(
                referenceAnalysis_.selectionRect.right(),
                referenceLowY));

        painter.drawLine(
            QPointF(
                referenceAnalysis_.selectionRect.left(),
                referenceHighY),
            QPointF(
                referenceAnalysis_.selectionRect.right(),
                referenceHighY));

        QFont referenceTagFont =
            measurementLabelFont;
        referenceTagFont.setPixelSize(
            (std::max)(
                1,
                static_cast<int>(
                    std::lround(
                        static_cast<double>(
                            measurementLabelFont.pixelSize()) *
                        0.72))));
        painter.setFont(referenceTagFont);

        const QFontMetricsF tagMetrics(
            referenceTagFont,
            painter.device());

        constexpr double kTagPaddingX = 5.0;
        constexpr double kTagPaddingY = 2.0;
        constexpr double kTagGap = 9.0;

        const QString tagText =
            QStringLiteral("Ref");

        const double tagWidth =
            tagMetrics.horizontalAdvance(tagText) +
            2.0 * kTagPaddingX;

        const double tagCenterX =
            referenceAnalysis_.selectionRect.center().x();

        QRectF tagRect(
            tagCenterX - tagWidth * 0.5,
            referenceHighY -
                tagMetrics.height() -
                2.0 * kTagPaddingY -
                kTagGap,
            tagWidth,
            tagMetrics.height() +
                2.0 * kTagPaddingY);

        if (tagRect.left() < geometry.scopeRect.left())
        {
            tagRect.moveLeft(geometry.scopeRect.left());
        }
        if (tagRect.right() > geometry.scopeRect.right())
        {
            tagRect.moveRight(geometry.scopeRect.right());
        }
        if (tagRect.top() < geometry.scopeRect.top())
        {
            tagRect.moveTop(geometry.scopeRect.top());
        }

        painter.setPen(QPen(QColor(255, 120, 255), 1.0));
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(tagRect, 3.0, 3.0);
        painter.drawText(
            tagRect.adjusted(
                kTagPaddingX,
                kTagPaddingY,
                -kTagPaddingX,
                -kTagPaddingY),
            Qt::AlignCenter,
            tagText);

        painter.restore();
    }

    if (areaMode_ || areaAnalysis_.attempted)
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor measureColor(80, 255, 120);
        QPen boxPen(measureColor, 1.5, Qt::DashLine);
        painter.setPen(boxPen);
        painter.setBrush(Qt::NoBrush);

        if (areaSelecting_)
        {
            const QRectF box = normalizedRect(areaStartPosition_, areaCurrentPosition_).intersected(geometry.scopeRect);
            painter.drawRect(box);
        }
        else if (areaAnalysis_.attempted)
        {
            if (areaAnalysis_.valid)
            {
                const double levelLineWidth =
                    (std::max)(1.5, measurementPenWidth * 0.65);

                painter.setPen(
                    QPen(
                        measureColor,
                        levelLineWidth));

                painter.drawLine(
                    QPointF(
                        areaAnalysis_.selectionRect.left(),
                        areaAnalysis_.vppTop.y()),
                    QPointF(
                        areaAnalysis_.selectionRect.right(),
                        areaAnalysis_.vppTop.y()));

                painter.drawLine(
                    QPointF(
                        areaAnalysis_.selectionRect.left(),
                        areaAnalysis_.vppBottom.y()),
                    QPointF(
                        areaAnalysis_.selectionRect.right(),
                        areaAnalysis_.vppBottom.y()));

                const QString frequencyLabel =
                    QStringLiteral("%1 MHz")
                        .arg(areaAnalysis_.frequencyMHz, 0, 'f', 2);

                QString amplitudeLabel;

                if (referenceAnalysis_.valid &&
                    referenceAnalysis_.vppVolts > 0.0 &&
                    areaAnalysis_.vppMillivolts > 0)
                {
                    double decibels =
                        20.0 *
                        std::log10(
                            (static_cast<double>(areaAnalysis_.vppMillivolts) / 1000.0) /
                            referenceAnalysis_.vppVolts);

                    if (std::abs(decibels) < 0.05)
                    {
                        decibels = 0.0;
                    }

                    amplitudeLabel =
                        QStringLiteral("%1 dB")
                            .arg(decibels, 0, 'f', 1);
                }
                else
                {
                    amplitudeLabel =
                        QStringLiteral("%1 mV")
                            .arg(areaAnalysis_.vppMillivolts);
                }

                painter.setFont(measurementLabelFont);
                const QFontMetricsF metrics(
                    measurementLabelFont,
                    painter.device());

                constexpr double kPaddingX = 10.0;
                constexpr double kPaddingY = 5.0;

                const auto makeLabelRect =
                    [&](const QString& label,
                        double centerX,
                        double top)
                    {
                        const QSizeF textSize(
                            metrics.horizontalAdvance(label),
                            metrics.height());

                        QRectF rect(
                            centerX -
                                (textSize.width() + 2.0 * kPaddingX) * 0.5,
                            top,
                            textSize.width() + 2.0 * kPaddingX,
                            textSize.height() + 2.0 * kPaddingY);

                        if (rect.left() < geometry.scopeRect.left())
                        {
                            rect.moveLeft(geometry.scopeRect.left());
                        }
                        if (rect.right() > geometry.scopeRect.right())
                        {
                            rect.moveRight(geometry.scopeRect.right());
                        }

                        return rect;
                    };

                const double labelCenterX =
                    0.5 *
                    (areaAnalysis_.selectionRect.left() +
                        areaAnalysis_.selectionRect.right());

                const bool useReferenceAnchors =
                    referenceAnalysis_.valid &&
                    !referenceAnalysis_.selectionRect.isEmpty();

                const double amplitudeAnchorY =
                    useReferenceAnchors
                    ? voltsToDisplayY(referenceAnalysis_.highVolts)
                    : areaAnalysis_.vppTop.y();

                const double frequencyAnchorY =
                    useReferenceAnchors
                    ? voltsToDisplayY(referenceAnalysis_.lowVolts)
                    : areaAnalysis_.vppBottom.y();

                QRectF amplitudeRect =
                    makeLabelRect(
                        amplitudeLabel,
                        labelCenterX,
                        amplitudeAnchorY -
                            metrics.height() -
                            2.0 * kPaddingY -
                            measurementLabelLineGap);

                if (amplitudeRect.top() < geometry.scopeRect.top())
                {
                    amplitudeRect.moveTop(geometry.scopeRect.top());
                }

                QRectF frequencyRect =
                    makeLabelRect(
                        frequencyLabel,
                        labelCenterX,
                        frequencyAnchorY + measurementLabelLineGap);

                if (frequencyRect.bottom() > geometry.scopeRect.bottom())
                {
                    frequencyRect.moveBottom(geometry.scopeRect.bottom());
                }

                painter.setPen(QPen(measureColor, 1.5));
                painter.setBrush(QColor(0, 0, 0, 210));

                painter.drawRoundedRect(
                    frequencyRect,
                    4.0,
                    4.0);
                painter.drawText(
                    frequencyRect.adjusted(
                        kPaddingX,
                        kPaddingY,
                        -kPaddingX,
                        -kPaddingY),
                    Qt::AlignCenter,
                    frequencyLabel);

                painter.drawRoundedRect(
                    amplitudeRect,
                    4.0,
                    4.0);
                painter.drawText(
                    amplitudeRect.adjusted(
                        kPaddingX,
                        kPaddingY,
                        -kPaddingX,
                        -kPaddingY),
                    Qt::AlignCenter,
                    amplitudeLabel);
            }
            else
            {
                painter.setFont(measurementLabelFont);
                const QString text = areaAnalysis_.message;
                const QFontMetricsF metrics(measurementLabelFont, painter.device());
                constexpr double kPaddingX = 10.0;
                constexpr double kPaddingY = 6.0;
                const QSizeF textSize(metrics.horizontalAdvance(text), metrics.height());
                QRectF labelRect(
                    areaAnalysis_.selectionRect.center().x() - (textSize.width() + 2.0 * kPaddingX) * 0.5,
                    infoBandTop + textSize.height() + 12.0,
                    textSize.width() + 2.0 * kPaddingX,
                    textSize.height() + 2.0 * kPaddingY);
                if (labelRect.right() > geometry.scopeRect.right())
                {
                    labelRect.moveRight(geometry.scopeRect.right());
                }
                if (labelRect.left() < geometry.scopeRect.left())
                {
                    labelRect.moveLeft(geometry.scopeRect.left());
                }
                if (labelRect.bottom() > geometry.scopeRect.bottom())
                {
                    labelRect.moveBottom(geometry.scopeRect.bottom());
                }
                painter.setPen(QPen(measureColor, 1.5));
                painter.setBrush(QColor(0, 0, 0, 210));
                painter.drawRoundedRect(labelRect, 4.0, 4.0);
                painter.drawText(
                    labelRect.adjusted(kPaddingX, kPaddingY, -kPaddingX, -kPaddingY),
                    Qt::AlignCenter,
                    text);
            }
        }

        painter.restore();
    }

    if (!multiburstAnalyses_.isEmpty())
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QColor measureColor(80, 255, 120);
        const QColor referenceColor(255, 120, 255);
        const double levelLineWidth =
            (std::max)(1.5, measurementPenWidth * 0.65);

        QFont tagFont =
            measurementLabelFont;
        tagFont.setPixelSize(
            (std::max)(
                1,
                static_cast<int>(
                    std::lround(
                        static_cast<double>(
                            measurementLabelFont.pixelSize()) *
                        0.68))));

        const QFontMetricsF tagMetrics(
            tagFont,
            painter.device());

        constexpr double kTagPaddingX = 4.0;
        constexpr double kTagPaddingY = 1.5;
        constexpr double kTagGap = 9.0;

        int visibleMeasurementNumber = 0;

        for (const AreaAnalysisResult& analysis : multiburstAnalyses_)
        {
            if (!analysis.valid ||
                analysis.selectionRect.isEmpty())
            {
                continue;
            }

            ++visibleMeasurementNumber;

            painter.setPen(
                QPen(
                    measureColor,
                    levelLineWidth));

            painter.drawLine(
                QPointF(
                    analysis.selectionRect.left(),
                    analysis.vppTop.y()),
                QPointF(
                    analysis.selectionRect.right(),
                    analysis.vppTop.y()));

            painter.drawLine(
                QPointF(
                    analysis.selectionRect.left(),
                    analysis.vppBottom.y()),
                QPointF(
                    analysis.selectionRect.right(),
                    analysis.vppBottom.y()));

            const QString tagText =
                QStringLiteral("MB%1")
                    .arg(visibleMeasurementNumber);

            painter.setFont(tagFont);

            const double tagWidth =
                tagMetrics.horizontalAdvance(tagText) +
                2.0 * kTagPaddingX;

            const double tagCenterX =
                analysis.selectionRect.center().x();

            QRectF tagRect(
                tagCenterX - tagWidth * 0.5,
                analysis.vppTop.y() -
                    tagMetrics.height() -
                    2.0 * kTagPaddingY -
                    kTagGap,
                tagWidth,
                tagMetrics.height() +
                    2.0 * kTagPaddingY);

            if (tagRect.left() < geometry.scopeRect.left())
            {
                tagRect.moveLeft(geometry.scopeRect.left());
            }
            if (tagRect.right() > geometry.scopeRect.right())
            {
                tagRect.moveRight(geometry.scopeRect.right());
            }
            if (tagRect.top() < geometry.scopeRect.top())
            {
                tagRect.moveTop(geometry.scopeRect.top());
            }

            painter.setPen(QPen(measureColor, 1.0));
            painter.setBrush(QColor(0, 0, 0, 180));
            painter.drawRoundedRect(tagRect, 3.0, 3.0);
            painter.drawText(
                tagRect.adjusted(
                    kTagPaddingX,
                    kTagPaddingY,
                    -kTagPaddingX,
                    -kTagPaddingY),
                Qt::AlignCenter,
                tagText);
        }

        // Compact measurement table anchored to the right edge of the scope.
        // It is generated from the live analysis values, so dragging any
        // REF/MB level line immediately updates mV and relative dB.
        QFont tableFont =
            measurementLabelFont;
        tableFont.setPixelSize(
            (std::max)(
                1,
                static_cast<int>(
                    std::lround(
                        static_cast<double>(
                            measurementLabelFont.pixelSize()) *
                        0.76))));
        painter.setFont(tableFont);

        const QFontMetricsF tableMetrics(
            tableFont,
            painter.device());

        constexpr double kTablePaddingX = 8.0;
        constexpr double kTablePaddingY = 6.0;
        constexpr double kColumnGap = 10.0;
        constexpr double kRowGap = 2.0;
        constexpr double kRightMargin = 8.0;
        constexpr double kTopMargin = 8.0;

        struct TableRow
        {
            QString id;
            QString frequency;
            QString millivolts;
            QString decibels;
            QColor color;
        };

        QVector<TableRow> rows;

        if (referenceAnalysis_.valid)
        {
            TableRow refRow;
            refRow.id = QStringLiteral("Ref");
            refRow.frequency =
                referenceAnalysis_.frequencyMHz > 0.0
                ? QStringLiteral("%1 MHz")
                    .arg(referenceAnalysis_.frequencyMHz, 0, 'f', 3)
                : QStringLiteral("--");
            refRow.millivolts =
                QStringLiteral("%1 mV")
                    .arg(referenceAnalysis_.vppMillivolts);
            refRow.decibels = QStringLiteral("0.0 dB");
            refRow.color = referenceColor;
            rows.push_back(refRow);
        }

        int tableMeasurementNumber = 0;

        for (const AreaAnalysisResult& analysis : multiburstAnalyses_)
        {
            if (!analysis.valid)
            {
                continue;
            }

            ++tableMeasurementNumber;

            TableRow row;
            row.id =
                QStringLiteral("MB%1")
                    .arg(tableMeasurementNumber);
            row.frequency =
                QStringLiteral("%1 MHz")
                    .arg(analysis.frequencyMHz, 0, 'f', 2);
            row.millivolts =
                QStringLiteral("%1 mV")
                    .arg(analysis.vppMillivolts);

            if (referenceAnalysis_.valid &&
                referenceAnalysis_.vppVolts > 0.0 &&
                analysis.vppMillivolts > 0)
            {
                double decibels =
                    20.0 *
                    std::log10(
                        (static_cast<double>(
                            analysis.vppMillivolts) / 1000.0) /
                        referenceAnalysis_.vppVolts);

                if (std::abs(decibels) < 0.05)
                {
                    decibels = 0.0;
                }

                row.decibels =
                    QStringLiteral("%1 dB")
                        .arg(decibels, 0, 'f', 1);
            }
            else
            {
                row.decibels = QStringLiteral("--");
            }

            row.color = measureColor;
            rows.push_back(row);
        }

        measurementTableRect_ = {};

        if (!rows.isEmpty())
        {
            double idWidth =
                tableMetrics.horizontalAdvance(
                    QStringLiteral("ID"));
            double frequencyWidth =
                tableMetrics.horizontalAdvance(
                    QStringLiteral("Freq"));
            double millivoltsWidth =
                tableMetrics.horizontalAdvance(
                    QStringLiteral("Level"));
            double decibelsWidth =
                tableMetrics.horizontalAdvance(
                    QStringLiteral("Rel"));

            for (const TableRow& row : rows)
            {
                idWidth =
                    (std::max)(
                        idWidth,
                        tableMetrics.horizontalAdvance(row.id));
                frequencyWidth =
                    (std::max)(
                        frequencyWidth,
                        tableMetrics.horizontalAdvance(row.frequency));
                millivoltsWidth =
                    (std::max)(
                        millivoltsWidth,
                        tableMetrics.horizontalAdvance(row.millivolts));
                decibelsWidth =
                    (std::max)(
                        decibelsWidth,
                        tableMetrics.horizontalAdvance(row.decibels));
            }

            const double rowHeight =
                tableMetrics.height() + kRowGap;

            const double tableWidth =
                2.0 * kTablePaddingX +
                idWidth +
                frequencyWidth +
                millivoltsWidth +
                decibelsWidth +
                3.0 * kColumnGap;

            const double tableHeight =
                2.0 * kTablePaddingY +
                rowHeight *
                    static_cast<double>(rows.size() + 1);

            constexpr double kDefaultBottomMargin = 12.0;

            QPointF tableTopLeft(
                geometry.scopeRect.center().x() -
                    tableWidth * 0.5,
                geometry.scopeRect.bottom() -
                    kDefaultBottomMargin -
                    tableHeight);

            if (measurementTableUserPositioned_)
            {
                tableTopLeft =
                    measurementTablePosition_;
            }

            const double maximumX =
                (std::max)(
                    geometry.scopeRect.left(),
                    geometry.scopeRect.right() -
                        tableWidth);

            const double maximumY =
                (std::max)(
                    geometry.scopeRect.top(),
                    geometry.scopeRect.bottom() -
                        tableHeight);

            tableTopLeft.setX(
                std::clamp(
                    tableTopLeft.x(),
                    geometry.scopeRect.left(),
                    maximumX));

            tableTopLeft.setY(
                std::clamp(
                    tableTopLeft.y(),
                    geometry.scopeRect.top(),
                    maximumY));

            QRectF tableRect(
                tableTopLeft,
                QSizeF(
                    tableWidth,
                    tableHeight));

            measurementTableRect_ = tableRect;

            if (measurementTableUserPositioned_)
            {
                measurementTablePosition_ =
                    tableRect.topLeft();
            }

            painter.setPen(QPen(QColor(110, 110, 110), 1.0));
            painter.setBrush(QColor(0, 0, 0, 205));
            painter.drawRoundedRect(tableRect, 4.0, 4.0);

            const double xId =
                tableRect.left() + kTablePaddingX;
            const double xFrequency =
                xId + idWidth + kColumnGap;
            const double xMillivolts =
                xFrequency + frequencyWidth + kColumnGap;
            const double xDecibels =
                xMillivolts + millivoltsWidth + kColumnGap;

            double y =
                tableRect.top() + kTablePaddingY;

            const auto drawCell =
                [&](double x,
                    double width,
                    const QString& text,
                    Qt::Alignment alignment,
                    const QColor& color)
                {
                    painter.setPen(color);
                    painter.drawText(
                        QRectF(
                            x,
                            y,
                            width,
                            tableMetrics.height()),
                        alignment | Qt::AlignVCenter,
                        text);
                };

            const QColor headerColor(180, 180, 180);

            drawCell(
                xId,
                idWidth,
                QStringLiteral("ID"),
                Qt::AlignLeft,
                headerColor);
            drawCell(
                xFrequency,
                frequencyWidth,
                QStringLiteral("Freq"),
                Qt::AlignRight,
                headerColor);
            drawCell(
                xMillivolts,
                millivoltsWidth,
                QStringLiteral("Level"),
                Qt::AlignRight,
                headerColor);
            drawCell(
                xDecibels,
                decibelsWidth,
                QStringLiteral("Rel"),
                Qt::AlignRight,
                headerColor);

            y += rowHeight;

            for (const TableRow& row : rows)
            {
                drawCell(
                    xId,
                    idWidth,
                    row.id,
                    Qt::AlignLeft,
                    row.color);
                drawCell(
                    xFrequency,
                    frequencyWidth,
                    row.frequency,
                    Qt::AlignRight,
                    row.color);
                drawCell(
                    xMillivolts,
                    millivoltsWidth,
                    row.millivolts,
                    Qt::AlignRight,
                    row.color);
                drawCell(
                    xDecibels,
                    decibelsWidth,
                    row.decibels,
                    Qt::AlignRight,
                    row.color);

                y += rowHeight;
            }
        }

        painter.restore();
    }

    if (measureActive_ &&
        !areaMode_ &&
        !referenceMode_)
    {
        const QPointF startPoint = clampPointToRect(measureStartPosition_, geometry.scopeRect);
        const QPointF endPoint = clampPointToRect(measureCurrentPosition_, geometry.scopeRect);
        const QLineF measureLine(startPoint, endPoint);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor measureColor(80, 255, 120);
        painter.setPen(QPen(measureColor, measurementPenWidth));
        painter.drawLine(startPoint, endPoint);

        const double startVolts = displayYToVolts(startPoint.y());
        const double endVolts = displayYToVolts(endPoint.y());
        const int deltaMillivolts = static_cast<int>(std::lround((endVolts - startVolts) * 1000.0));
        const double startSourcePixels = displayXToSourcePixels(startPoint.x());
        const double endSourcePixels = displayXToSourcePixels(endPoint.x());
        const double deltaSourcePixels = std::abs(endSourcePixels - startSourcePixels);
        const double deltaSeconds = deltaSourcePixels / sampleClockHz(geometry.standard);

        QString frequencyText = QStringLiteral("∞ MHz");
        if (deltaSeconds > 1.0e-12)
        {
            frequencyText = QStringLiteral("%1 MHz").arg(1.0 / (deltaSeconds * 1.0e6), 0, 'f', 2);
        }

        const QString deltaMillivoltsText =
            deltaMillivolts >= 0
            ? QStringLiteral("+%1").arg(deltaMillivolts)
            : QString::number(deltaMillivolts);

        const QString measureText = QStringLiteral("ΔV %1 mV   %2").arg(deltaMillivoltsText).arg(frequencyText);
        painter.setFont(measurementLabelFont);
        const QFontMetricsF labelMetrics(measurementLabelFont, painter.device());
        constexpr double kPaddingX = 10.0;
        constexpr double kPaddingY = 6.0;
        const QSizeF textSize(labelMetrics.horizontalAdvance(measureText), labelMetrics.height());

        QPointF labelCenter = (startPoint + endPoint) * 0.5;
        const double lineLength = measureLine.length();
        if (lineLength > 0.001)
        {
            const QPointF normal(-measureLine.dy() / lineLength, measureLine.dx() / lineLength);
            labelCenter += normal * 18.0;
        }

        QRectF labelRect(
            labelCenter.x() - (textSize.width() + 2.0 * kPaddingX) * 0.5,
            infoBandTop,
            textSize.width() + 2.0 * kPaddingX,
            textSize.height() + 2.0 * kPaddingY);

        if (labelRect.left() < geometry.scopeRect.left()) labelRect.moveLeft(geometry.scopeRect.left());
        if (labelRect.right() > geometry.scopeRect.right()) labelRect.moveRight(geometry.scopeRect.right());
        if (labelRect.bottom() > geometry.scopeRect.bottom()) labelRect.moveBottom(geometry.scopeRect.bottom());
        painter.setPen(QPen(measureColor, 1.5));
        painter.setBrush(QColor(0, 0, 0, 210));
        painter.drawRoundedRect(labelRect, 4.0, 4.0);
        painter.drawText(labelRect.adjusted(kPaddingX, kPaddingY, -kPaddingX, -kPaddingY), Qt::AlignCenter, measureText);
        painter.restore();
    }

    if (!hoverActive_ || panActive_ || areaMode_ || referenceMode_ || !displayRect.contains(hoverPosition_.toPoint()))
    {
        return;
    }

    const double volts = displayYToVolts(hoverPosition_.y());
    const int millivolts =
        static_cast<int>(
            std::lround(volts * 1000.0));
    const double percent =
        (volts - kBlackLevelVolts) *
        100.0 /
        (kWhiteLevelVolts - kBlackLevelVolts);

    QString text =
        QStringLiteral("%1 mV   %2")
            .arg(millivolts)
            .arg(formatPercent(percent));

    if (probeDetailsMode_)
    {
        const double sourcePixelPosition =
            displayXToSourcePixels(
                hoverPosition_.x());

        const int sourcePixel =
            std::clamp(
                static_cast<int>(
                    std::lround(sourcePixelPosition)),
                0,
                geometry.standard.sampleWidth - 1);

        const double lineTimeMicroseconds =
            static_cast<double>(sourcePixel) /
            sampleClockHz(geometry.standard) *
            1.0e6;

        text +=
            QStringLiteral("   %1 us   px %2")
                .arg(lineTimeMicroseconds, 0, 'f', 2)
                .arg(sourcePixel);
    }

    painter.save();
    painter.setFont(measurementLabelFont);

    const QFontMetricsF metrics(measurementLabelFont, painter.device());
    constexpr double kPaddingX = 10.0;
    constexpr double kPaddingY = 6.0;
    constexpr double kCursorGap = 12.0;
    const QSizeF textSize(metrics.horizontalAdvance(text), metrics.height());

    QRectF labelRect(
        hoverPosition_.x() - (textSize.width() + 2.0 * kPaddingX) * 0.5,
        infoBandTop,
        textSize.width() + 2.0 * kPaddingX,
        textSize.height() + 2.0 * kPaddingY);

    if (labelRect.right() > geometry.scopeRect.right())
    {
        labelRect.moveRight(geometry.scopeRect.right());
    }
    if (labelRect.left() < geometry.scopeRect.left())
    {
        labelRect.moveLeft(geometry.scopeRect.left());
    }
    if (labelRect.bottom() > geometry.scopeRect.bottom())
    {
        labelRect.moveBottom(geometry.scopeRect.bottom());
    }
    painter.setPen(QPen(QColor(80, 170, 255), 1.5));
    painter.setBrush(QColor(0, 0, 0, 210));
    painter.drawRoundedRect(labelRect, 4.0, 4.0);
    painter.drawText(
        labelRect.adjusted(kPaddingX, kPaddingY, -kPaddingX, -kPaddingY),
        Qt::AlignCenter,
        text);
    painter.restore();
}

void WaveformWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    clearMeasurements();
    emitOutputSize();
}
