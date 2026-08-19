#include "YSpectrumWindow.h"

#include <QComboBox>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QString>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.1415926535897932384626433832795;

QString amplitudeText(double voltsPeak)
{
    if (voltsPeak >= 1.0)
    {
        return QStringLiteral("%1 Vpk").arg(voltsPeak, 0, 'f', 3);
    }

    if (voltsPeak >= 0.001)
    {
        return QStringLiteral("%1 mVpk").arg(voltsPeak * 1000.0, 0, 'f', 2);
    }

    return QStringLiteral("%1 uVpk").arg(voltsPeak * 1'000'000.0, 0, 'f', 1);
}

QString rmsText(double voltsRms)
{
    if (voltsRms >= 0.001)
    {
        return QStringLiteral("%1 mVrms").arg(voltsRms * 1000.0, 0, 'f', 3);
    }

    return QStringLiteral("%1 uVrms").arg(voltsRms * 1'000'000.0, 0, 'f', 1);
}
}

YSpectrumWindow::YSpectrumWindow(QWidget* parent)
    : QWidget(parent)
    , sourceCombo_(new QComboBox(this))
    , averageCombo_(new QComboBox(this))
    , maxHoldButton_(new QPushButton(QStringLiteral("Max Hold"), this))
    , clearButton_(new QPushButton(QStringLiteral("Clear"), this))
    , measurePeaksButton_(new QPushButton(QStringLiteral("Measure Peaks"), this))
{
    setWindowTitle(QStringLiteral("OpenScope - Y Spectrum (experimental)"));
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(1600, 900);
    setMinimumSize(900, 520);
    setMouseTracking(true);

    sourceCombo_->addItem(QStringLiteral("Full active line"));
    sourceCombo_->addItem(QStringLiteral("Visible portion"));
    sourceCombo_->setCurrentIndex(0);

    averageCombo_->addItem(QStringLiteral("AVG 1"), 1);
    averageCombo_->addItem(QStringLiteral("AVG 4"), 4);
    averageCombo_->addItem(QStringLiteral("AVG 16"), 16);
    averageCombo_->addItem(QStringLiteral("AVG 64"), 64);
    averageCombo_->setCurrentIndex(2);

    maxHoldButton_->setCheckable(true);

    updateControlGeometry();

    connect(
        sourceCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            resetAverage();
            clearMaxHold();
            clearPeakMeasurement();
            rebuildSpectrum();
            update();
        });

    connect(
        averageCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            resetAverage();
            clearPeakMeasurement();
            rebuildSpectrum();
            update();
        });

    connect(
        maxHoldButton_,
        &QPushButton::toggled,
        this,
        [this](bool enabled)
        {
            if (enabled)
            {
                clearMaxHold();
            }
            update();
        });

    connect(
        clearButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            resetAverage();
            clearMaxHold();
            clearPeakMeasurement();
            rebuildSpectrum();
            update();
        });

    connect(
        measurePeaksButton_,
        &QPushButton::clicked,
        this,
        [this]()
        {
            measurePeaks();
            update();
        });

    auto* hideShortcut =
        new QShortcut(QKeySequence(Qt::Key_F), this);
    hideShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(
        hideShortcut,
        &QShortcut::activated,
        this,
        &QWidget::hide);

    auto* escapeShortcut =
        new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);

    connect(
        escapeShortcut,
        &QShortcut::activated,
        this,
        &QWidget::hide);
}

void YSpectrumWindow::setSamples(
    const QVector<float>& fullLine,
    const QVector<float>& visiblePart,
    int selectedLine,
    int zoomFactor,
    bool inputSignalValid)
{
    fullLine_ = fullLine;
    visiblePart_ = visiblePart;

    const bool lineChanged =
        selectedLine_ != selectedLine;

    const bool zoomChanged =
        zoomFactor_ != zoomFactor;

    const bool inputValidityChanged =
        inputSignalValid_ != inputSignalValid;

    inputSignalValid_ =
        inputSignalValid;

    if (lineChanged || zoomChanged || inputValidityChanged)
    {
        resetAverage();
        clearPeakMeasurement();
    }

    if (lineChanged || inputValidityChanged)
    {
        clearMaxHold();
    }

    selectedLine_ = selectedLine;
    zoomFactor_ = zoomFactor;

    if (isVisible())
    {
        rebuildSpectrum();
        update();
    }
}


void YSpectrumWindow::setSnrMeasurementEnabled(
    bool enabled,
    const QString& disabledReason)
{
    if (snrMeasurementEnabled_ == enabled &&
        snrDisabledReason_ == disabledReason)
    {
        return;
    }

    snrMeasurementEnabled_ = enabled;
    snrDisabledReason_ = enabled
        ? QString()
        : disabledReason;

    rebuildSpectrum();
    update();
}

void YSpectrumWindow::resetAverage()
{
    averagedAmplitudePower_.clear();
    averagedNoisePower_.clear();
    averageValid_ = false;
    averageLine_ = -2;
    averageZoomFactor_ = -1;
    averageSourceIndex_ = -1;
}

void YSpectrumWindow::clearMaxHold()
{
    maxHoldAmplitudePower_.clear();
}

void YSpectrumWindow::clearPeakMeasurement()
{
    measuredPeaks_.clear();
}

void YSpectrumWindow::measurePeaks()
{
    measuredPeaks_.clear();

    if (spectrum_.size() < 3)
    {
        return;
    }

    struct Candidate
    {
        int bin = 0;
        double db = kMinimumDb;
    };

    QVector<Candidate> candidates;

    for (int bin = 1; bin < spectrum_.size() - 1; ++bin)
    {
        const SpectrumPoint& previous = spectrum_[bin - 1];
        const SpectrumPoint& point = spectrum_[bin];
        const SpectrumPoint& next = spectrum_[bin + 1];

        if (point.frequencyHz < kMinimumSnrFrequencyHz ||
            point.frequencyHz > kMaximumDisplayFrequencyHz)
        {
            continue;
        }

        if (point.db > previous.db &&
            point.db >= next.db)
        {
            candidates.append({ bin, point.db });
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate& a, const Candidate& b)
        {
            return a.db > b.db;
        });

    const QVector<float>& source =
        sourceCombo_->currentIndex() == 0
        ? fullLine_
        : visiblePart_;

    const double trueResolutionHz =
        source.isEmpty()
        ? kReconstructedSampleClockHz / static_cast<double>(kFftSize)
        : kReconstructedSampleClockHz / static_cast<double>(source.size());

    const double minimumPeakSpacingHz =
        3.0 * trueResolutionHz;

    for (const Candidate& candidate : candidates)
    {
        const SpectrumPoint& point = spectrum_[candidate.bin];

        bool tooClose = false;
        for (const MeasuredPeak& accepted : measuredPeaks_)
        {
            if (std::abs(accepted.frequencyHz - point.frequencyHz) <
                minimumPeakSpacingHz)
            {
                tooClose = true;
                break;
            }
        }

        if (tooClose)
        {
            continue;
        }

        measuredPeaks_.append(
            {
                point.frequencyHz,
                point.amplitudeVoltsPeak,
                point.db
            });

        if (measuredPeaks_.size() >= kMaximumMeasuredPeaks)
        {
            break;
        }
    }

    // Detection selects the strongest peaks first, but presentation is easier to
    // read when marker numbers increase from left to right across the spectrum.
    std::sort(
        measuredPeaks_.begin(),
        measuredPeaks_.end(),
        [](const MeasuredPeak& a, const MeasuredPeak& b)
        {
            return a.frequencyHz < b.frequencyHz;
        });
}

void YSpectrumWindow::updateControlGeometry()
{
    sourceCombo_->setGeometry(16, 12, 190, 28);
    averageCombo_->setGeometry(216, 12, 92, 28);
    maxHoldButton_->setGeometry(318, 12, 92, 28);
    clearButton_->setGeometry(420, 12, 72, 28);
    measurePeaksButton_->setGeometry(502, 12, 122, 28);
}

void YSpectrumWindow::resizeEvent(QResizeEvent* event)
{
    updateControlGeometry();
    QWidget::resizeEvent(event);
}

int YSpectrumWindow::averageDepth() const
{
    return averageCombo_->currentData().toInt();
}

void YSpectrumWindow::rebuildSpectrum()
{
    const int sourceIndex = sourceCombo_->currentIndex();
    const QVector<float>& source =
        sourceIndex == 0
        ? fullLine_
        : visiblePart_;

    spectrum_.clear();
    snrValid_ = false;
    flatRegion_ = false;
    flatnessSpanVolts_ = 0.0;
    noiseRmsVolts_ = 0.0;
    snrDb_ = 0.0;

    if (source.size() < 8)
    {
        return;
    }

    const int sampleCount = std::min<int>(source.size(), kFftSize);

    double mean = 0.0;
    for (int i = 0; i < sampleCount; ++i)
    {
        mean += static_cast<double>(source[i]);
    }
    mean /= static_cast<double>(sampleCount);

    // SNR is only meaningful on a genuinely flat luminance region.
    // Use a robust central 90% span so a few isolated noisy samples do not
    // invalidate an otherwise flat line, while obvious picture content does.
    QVector<double> sortedSamples;
    sortedSamples.reserve(sampleCount);

    for (int i = 0; i < sampleCount; ++i)
    {
        sortedSamples.append(static_cast<double>(source[i]));
    }

    std::sort(sortedSamples.begin(), sortedSamples.end());

    const int lowIndex = std::clamp(
        static_cast<int>(std::floor(0.05 * static_cast<double>(sampleCount - 1))),
        0,
        sampleCount - 1);
    const int highIndex = std::clamp(
        static_cast<int>(std::ceil(0.95 * static_cast<double>(sampleCount - 1))),
        0,
        sampleCount - 1);

    flatnessSpanVolts_ =
        sortedSamples[highIndex] - sortedSamples[lowIndex];
    flatRegion_ =
        flatnessSpanVolts_ <= kMaximumFlatnessSpanVolts;

    QVector<double> real(kFftSize, 0.0);
    QVector<double> imag(kFftSize, 0.0);

    double windowSum = 0.0;
    double windowPowerSum = 0.0;

    for (int i = 0; i < sampleCount; ++i)
    {
        const double window =
            sampleCount > 1
            ? 0.5 - 0.5 * std::cos(
                2.0 * kPi * static_cast<double>(i) /
                static_cast<double>(sampleCount - 1))
            : 1.0;

        real[i] =
            (static_cast<double>(source[i]) - mean) * window;

        windowSum += window;
        windowPowerSum += window * window;
    }

    if (windowSum <= 0.0 || windowPowerSum <= 0.0)
    {
        return;
    }

    fftInPlace(real, imag);

    const double binWidthHz =
        kReconstructedSampleClockHz /
        static_cast<double>(kFftSize);

    const int maximumBin = std::min(
        kFftSize / 2,
        static_cast<int>(
            std::floor(kMaximumDisplayFrequencyHz / binWidthHz)));

    QVector<double> instantaneousAmplitudePower(maximumBin + 1, 0.0);
    QVector<double> instantaneousNoisePower(maximumBin + 1, 0.0);

    for (int bin = 0; bin <= maximumBin; ++bin)
    {
        const double magnitudeSquared =
            real[bin] * real[bin] +
            imag[bin] * imag[bin];

        const double magnitude = std::sqrt(magnitudeSquared);

        const double amplitude =
            bin == 0
            ? magnitude / windowSum
            : 2.0 * magnitude / windowSum;

        instantaneousAmplitudePower[bin] = amplitude * amplitude;

        const double oneSidedFactor =
            (bin == 0 || bin == kFftSize / 2)
            ? 1.0
            : 2.0;

        instantaneousNoisePower[bin] =
            oneSidedFactor * magnitudeSquared /
            (static_cast<double>(kFftSize) * windowPowerSum);
    }

    const bool averageIdentityChanged =
        !averageValid_ ||
        averageLine_ != selectedLine_ ||
        averageZoomFactor_ != zoomFactor_ ||
        averageSourceIndex_ != sourceIndex ||
        averagedAmplitudePower_.size() != instantaneousAmplitudePower.size();

    if (averageIdentityChanged)
    {
        averagedAmplitudePower_ = instantaneousAmplitudePower;
        averagedNoisePower_ = instantaneousNoisePower;
        averageValid_ = true;
        averageLine_ = selectedLine_;
        averageZoomFactor_ = zoomFactor_;
        averageSourceIndex_ = sourceIndex;
    }
    else
    {
        const double alpha = 1.0 / static_cast<double>(averageDepth());

        for (qsizetype i = 0; i < averagedAmplitudePower_.size(); ++i)
        {
            averagedAmplitudePower_[i] +=
                alpha *
                (instantaneousAmplitudePower[i] - averagedAmplitudePower_[i]);

            averagedNoisePower_[i] +=
                alpha *
                (instantaneousNoisePower[i] - averagedNoisePower_[i]);
        }
    }

    if (maxHoldButton_->isChecked())
    {
        if (maxHoldAmplitudePower_.size() != instantaneousAmplitudePower.size())
        {
            maxHoldAmplitudePower_ = instantaneousAmplitudePower;
        }
        else
        {
            for (qsizetype i = 0; i < maxHoldAmplitudePower_.size(); ++i)
            {
                maxHoldAmplitudePower_[i] = std::max(
                    maxHoldAmplitudePower_[i],
                    instantaneousAmplitudePower[i]);
            }
        }
    }

    spectrum_.reserve(maximumBin + 1);

    double integratedNoisePower = 0.0;

    for (int bin = 0; bin <= maximumBin; ++bin)
    {
        const double frequencyHz =
            static_cast<double>(bin) * binWidthHz;

        const double amplitude =
            std::sqrt(std::max(0.0, averagedAmplitudePower_[bin]));

        // OpenScope video level convention: 700 mV = 0 dB.
        // This is intentionally not dBV; dBV would use 1 Vrms as reference.
        const double db =
            amplitude > 1.0e-12
            ? 20.0 * std::log10(amplitude / kReferenceVolts)
            : kMinimumDb;

        SpectrumPoint point;
        point.frequencyHz = frequencyHz;
        point.amplitudeVoltsPeak = amplitude;
        point.db = db;
        point.noisePowerVoltsSquared = averagedNoisePower_[bin];
        spectrum_.append(point);

        if (frequencyHz >= kMinimumSnrFrequencyHz &&
            frequencyHz <= kMaximumSnrFrequencyHz)
        {
            integratedNoisePower += averagedNoisePower_[bin];
        }
    }

    if (integratedNoisePower > 0.0)
    {
        noiseRmsVolts_ = std::sqrt(integratedNoisePower);
        snrDb_ = 20.0 * std::log10(
            kReferenceVolts / noiseRmsVolts_);
        snrValid_ =
            snrMeasurementEnabled_ &&
            inputSignalValid_ &&
            flatRegion_ &&
            noiseRmsVolts_ >= kMinimumMeaningfulNoiseRmsVolts &&
            std::isfinite(snrDb_);
    }
}

void YSpectrumWindow::fftInPlace(
    QVector<double>& real,
    QVector<double>& imag) const
{
    const int n = real.size();

    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;

        if (i < j)
        {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    for (int length = 2; length <= n; length <<= 1)
    {
        const double angle = -2.0 * kPi / static_cast<double>(length);
        const double wLengthReal = std::cos(angle);
        const double wLengthImag = std::sin(angle);

        for (int i = 0; i < n; i += length)
        {
            double wReal = 1.0;
            double wImag = 0.0;

            for (int j = 0; j < length / 2; ++j)
            {
                const int even = i + j;
                const int odd = even + length / 2;

                const double oddReal =
                    real[odd] * wReal - imag[odd] * wImag;
                const double oddImag =
                    real[odd] * wImag + imag[odd] * wReal;

                const double evenReal = real[even];
                const double evenImag = imag[even];

                real[even] = evenReal + oddReal;
                imag[even] = evenImag + oddImag;
                real[odd] = evenReal - oddReal;
                imag[odd] = evenImag - oddImag;

                const double nextWReal =
                    wReal * wLengthReal - wImag * wLengthImag;
                const double nextWImag =
                    wReal * wLengthImag + wImag * wLengthReal;

                wReal = nextWReal;
                wImag = nextWImag;
            }
        }
    }
}

int YSpectrumWindow::nearestBinForFrequency(double frequencyHz) const
{
    if (spectrum_.isEmpty())
    {
        return -1;
    }

    const double binWidthHz =
        kReconstructedSampleClockHz /
        static_cast<double>(kFftSize);

    return std::clamp(
        static_cast<int>(std::llround(frequencyHz / binWidthHz)),
        0,
        static_cast<int>(spectrum_.size()) - 1);
}

double YSpectrumWindow::frequencyForX(double x) const
{
    const double left = 84.0;
    const double right = static_cast<double>(width()) - 28.0;
    const double normalized = std::clamp(
        (x - left) / std::max(1.0, right - left),
        0.0,
        1.0);

    return normalized * kMaximumDisplayFrequencyHz;
}

double YSpectrumWindow::xForFrequency(double frequencyHz) const
{
    const double left = 84.0;
    const double right = static_cast<double>(width()) - 28.0;
    return left +
        std::clamp(
            frequencyHz / kMaximumDisplayFrequencyHz,
            0.0,
            1.0) *
        (right - left);
}

double YSpectrumWindow::yForDb(double db) const
{
    const double top = 76.0;
    const double bottom = static_cast<double>(height()) - 132.0;
    const double normalized = std::clamp(
        (kMaximumDb - db) /
            (kMaximumDb - kMinimumDb),
        0.0,
        1.0);
    return top + normalized * (bottom - top);
}

void YSpectrumWindow::setMarkerFromX(int markerIndex, double x)
{
    markerFrequencyHz_[markerIndex] = frequencyForX(x);
    update();
}

void YSpectrumWindow::mousePressEvent(QMouseEvent* event)
{
    const double frequency = frequencyForX(event->position().x());
    const double distanceA = std::abs(frequency - markerFrequencyHz_[0]);
    const double distanceB = std::abs(frequency - markerFrequencyHz_[1]);

    activeMarker_ = distanceA <= distanceB ? 0 : 1;
    setMarkerFromX(activeMarker_, event->position().x());
    event->accept();
}

void YSpectrumWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        setMarkerFromX(activeMarker_, event->position().x());
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void YSpectrumWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(9, 12, 14));
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF plotRect(
        84.0,
        76.0,
        std::max(1, width() - 112),
        std::max(1, height() - 208));

    QPen gridPen(QColor(55, 62, 66));
    gridPen.setWidthF(1.0);
    painter.setPen(gridPen);

    for (int mhz = 0; mhz <= 7; ++mhz)
    {
        const double x = xForFrequency(
            static_cast<double>(mhz) * 1'000'000.0);
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));
        painter.drawText(
            QRectF(x - 30.0, plotRect.bottom() + 6.0, 60.0, 22.0),
            Qt::AlignHCenter | Qt::AlignTop,
            QString::number(mhz));
    }

    for (int db = 0; db >= static_cast<int>(kMinimumDb); db -= 20)
    {
        const double y = yForDb(static_cast<double>(db));
        painter.drawLine(QPointF(plotRect.left(), y), QPointF(plotRect.right(), y));
        painter.drawText(
            QRectF(10.0, y - 10.0, 64.0, 20.0),
            Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("%1 dB").arg(db));
    }

    // The reconstructed stream runs at 54 MHz, but its original video
    // information came from 13.5 MHz samples.  Mark that original Nyquist
    // limit explicitly: reconstruction can make the roll-off visible, but it
    // cannot create new source information above 6.75 MHz.
    const double nyquistX =
        xForFrequency(kOriginalNyquistFrequencyHz);

    QPen nyquistPen(QColor(125, 148, 158));
    nyquistPen.setWidthF(1.2);
    nyquistPen.setStyle(Qt::DashLine);
    painter.setPen(nyquistPen);
    painter.drawLine(
        QPointF(nyquistX, plotRect.top()),
        QPointF(nyquistX, plotRect.bottom()));

    painter.setPen(QColor(175, 194, 201));
    painter.drawText(
        QRectF(nyquistX - 132.0, plotRect.top() + 4.0, 124.0, 20.0),
        Qt::AlignRight | Qt::AlignVCenter,
        QStringLiteral("Nyquist 6.75 MHz"));

    const QVector<float>& source =
        sourceCombo_->currentIndex() == 0
        ? fullLine_
        : visiblePart_;

    const double trueResolutionHz =
        source.isEmpty()
        ? 0.0
        : kReconstructedSampleClockHz /
            static_cast<double>(source.size());

    painter.setPen(QColor(220, 225, 228));
    painter.drawText(
        QRectF(642.0, 12.0, std::max(1, width() - 658), 28.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("Line %1   Zoom x%2   N=%3   resolution ~%4 kHz   FFT=4096   Hann   DC removed")
            .arg(selectedLine_)
            .arg(zoomFactor_)
            .arg(source.size())
            .arg(trueResolutionHz / 1000.0, 0, 'f', 1));


    if (spectrum_.size() > 1)
    {
        // Make the SNR integration band visible in the trace itself:
        //   0.10 .. 5.00 MHz  = active SNR band (green)
        //   outside that band = displayed for context only (grey)
        // The colour split is presentation-only; the SNR calculation above
        // already integrates exactly kMinimumSnrFrequencyHz..kMaximumSnrFrequencyHz.
        QPen activeTracePen(QColor(87, 255, 118));
        activeTracePen.setWidthF(1.4);

        QPen contextTracePen(QColor(126, 136, 141));
        contextTracePen.setWidthF(1.4);

        for (qsizetype i = 1; i < spectrum_.size(); ++i)
        {
            const SpectrumPoint& previous = spectrum_[i - 1];
            const SpectrumPoint& current = spectrum_[i];

            const double segmentFrequencyHz =
                0.5 * (previous.frequencyHz + current.frequencyHz);

            const bool inSnrBand =
                segmentFrequencyHz >= kMinimumSnrFrequencyHz &&
                segmentFrequencyHz <= kMaximumSnrFrequencyHz;

            painter.setPen(inSnrBand ? activeTracePen : contextTracePen);
            painter.drawLine(
                QPointF(
                    xForFrequency(previous.frequencyHz),
                    yForDb(previous.db)),
                QPointF(
                    xForFrequency(current.frequencyHz),
                    yForDb(current.db)));
        }
    }

    if (maxHoldButton_->isChecked() &&
        maxHoldAmplitudePower_.size() == spectrum_.size())
    {
        const double binWidthHz =
            kReconstructedSampleClockHz /
            static_cast<double>(kFftSize);

        QPen activeHoldPen(QColor(255, 190, 70, 185));
        activeHoldPen.setWidthF(1.1);

        QPen contextHoldPen(QColor(126, 136, 141, 155));
        contextHoldPen.setWidthF(1.1);

        for (qsizetype bin = 1; bin < maxHoldAmplitudePower_.size(); ++bin)
        {
            const auto pointForBin =
                [&](qsizetype index)
                {
                    const double amplitude =
                        std::sqrt(
                            std::max(
                                0.0,
                                maxHoldAmplitudePower_[index]));

                    const double db =
                        amplitude > 1.0e-12
                        ? 20.0 * std::log10(amplitude / kReferenceVolts)
                        : kMinimumDb;

                    return QPointF(
                        xForFrequency(
                            static_cast<double>(index) * binWidthHz),
                        yForDb(db));
                };

            const double segmentFrequencyHz =
                (static_cast<double>(bin) - 0.5) * binWidthHz;

            const bool inSnrBand =
                segmentFrequencyHz >= kMinimumSnrFrequencyHz &&
                segmentFrequencyHz <= kMaximumSnrFrequencyHz;

            painter.setPen(inSnrBand ? activeHoldPen : contextHoldPen);
            painter.drawLine(
                pointForBin(bin - 1),
                pointForBin(bin));
        }
    }

    const QColor markerColors[2] = {
        QColor(255, 210, 70),
        QColor(90, 190, 255)
    };

    QString markerTexts[2];
    double markerDb[2] = { kMinimumDb, kMinimumDb };
    double markerActualHz[2] = { 0.0, 0.0 };

    for (int marker = 0; marker < 2; ++marker)
    {
        const int bin = nearestBinForFrequency(markerFrequencyHz_[marker]);
        const double x = xForFrequency(markerFrequencyHz_[marker]);

        QPen markerPen(markerColors[marker]);
        markerPen.setWidthF(marker == activeMarker_ ? 2.0 : 1.2);
        painter.setPen(markerPen);
        painter.drawLine(QPointF(x, plotRect.top()), QPointF(x, plotRect.bottom()));

        if (bin >= 0)
        {
            const SpectrumPoint& point = spectrum_[bin];
            painter.drawEllipse(
                QPointF(xForFrequency(point.frequencyHz), yForDb(point.db)),
                4.0,
                4.0);

            markerDb[marker] = point.db;
            markerActualHz[marker] = point.frequencyHz;

            markerTexts[marker] =
                QStringLiteral("%1: %2 MHz   %3 dB   %4")
                    .arg(marker == 0 ? QStringLiteral("A") : QStringLiteral("B"))
                    .arg(point.frequencyHz / 1'000'000.0, 0, 'f', 4)
                    .arg(point.db, 0, 'f', 1)
                    .arg(amplitudeText(point.amplitudeVoltsPeak));
        }
    }

    painter.setPen(markerColors[0]);
    painter.drawText(
        QRectF(plotRect.left(), 44.0, plotRect.width() * 0.5, 24.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        markerTexts[0]);

    painter.setPen(markerColors[1]);
    painter.drawText(
        QRectF(plotRect.left() + plotRect.width() * 0.5, 44.0, plotRect.width() * 0.5, 24.0),
        Qt::AlignRight | Qt::AlignVCenter,
        markerTexts[1]);

    // Instrument-style SNR card.  The medium-grey panel separates the
    // measurement from the trace while keeping the white readout highly
    // legible. Never show a plausible-looking number when the selected data
    // is not a valid analogue SNR measurement source.
    const QFont normalFont = painter.font();

    const QRectF snrCardRect(
        plotRect.left() + 12.0,
        plotRect.top() + 10.0,
        372.0,
        82.0);

    painter.setPen(QPen(QColor(135, 142, 146), 1.0));
    painter.setBrush(QColor(68, 74, 78, 235));
    painter.drawRoundedRect(snrCardRect, 9.0, 9.0);

    QFont snrFont = normalFont;
    snrFont.setBold(true);
    snrFont.setPointSizeF(28.0);
    painter.setFont(snrFont);
    painter.setPen(QColor(248, 250, 250));
    painter.drawText(
        snrCardRect.adjusted(14.0, 4.0, -12.0, -32.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        snrValid_
        ? QStringLiteral("SNR %1 dB").arg(snrDb_, 0, 'f', 1)
        : QStringLiteral("SNR -- dB"));

    QFont snrDetailFont = normalFont;
    snrDetailFont.setBold(false);
    snrDetailFont.setPointSizeF(9.0);
    painter.setFont(snrDetailFont);
    painter.setPen(QColor(224, 228, 230));

    QString snrDetail;
    if (!snrMeasurementEnabled_)
    {
        snrDetail = snrDisabledReason_.isEmpty()
            ? QStringLiteral("SNR DISABLED FOR THIS SOURCE")
            : snrDisabledReason_;
    }
    else if (!inputSignalValid_)
    {
        snrDetail =
            QStringLiteral("NO INPUT SOURCE   DeckLink status   SNR suppressed");
    }
    else if (!flatRegion_)
    {
        snrDetail =
            QStringLiteral("NOT FLAT   span %1 mV   SNR suppressed")
                .arg(flatnessSpanVolts_ * 1000.0, 0, 'f', 1);
    }
    else if (noiseRmsVolts_ < kMinimumMeaningfulNoiseRmsVolts)
    {
        snrDetail =
            QStringLiteral("NO MEASURABLE ANALOGUE NOISE   SNR suppressed");
    }
    else
    {
        snrDetail =
            QStringLiteral("0.10-5.00 MHz   Ref 700 mV   flat span %1 mV")
                .arg(flatnessSpanVolts_ * 1000.0, 0, 'f', 1);
    }

    painter.drawText(
        snrCardRect.adjusted(16.0, 50.0, -12.0, -7.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        snrDetail);

    painter.setBrush(Qt::NoBrush);
    painter.setFont(normalFont);

    // Peak measurement is intentionally snapshot-based. The live spectrum keeps
    // moving, while the measured top-five list remains readable until the next
    // Measure Peaks action or an explicit/automatic clear. Peak numbering is in
    // ascending frequency order, matching the markers from left to right.
    if (!measuredPeaks_.isEmpty())
    {
        const double tableWidth = 380.0;
        const double rowHeight = 20.0;
        const double tableHeight = 28.0 +
            rowHeight * static_cast<double>(measuredPeaks_.size());
        const QRectF tableRect(
            plotRect.right() - tableWidth - 12.0,
            plotRect.top() + 12.0,
            tableWidth,
            tableHeight);

        painter.fillRect(tableRect, QColor(5, 8, 10, 205));
        painter.setPen(QColor(95, 105, 110));
        painter.drawRect(tableRect);

        const double left = tableRect.left() + 10.0;
        const double usableWidth = tableRect.width() - 20.0;
        const double rankWidth = 34.0;
        const double frequencyWidth = 92.0;
        const double levelWidth = 76.0;
        const double amplitudeWidth =
            usableWidth - rankWidth - frequencyWidth - levelWidth;

        const QRectF rankHeader(
            left, tableRect.top() + 4.0, rankWidth, 20.0);
        const QRectF frequencyHeader(
            rankHeader.right(), tableRect.top() + 4.0, frequencyWidth, 20.0);
        const QRectF levelHeader(
            frequencyHeader.right(), tableRect.top() + 4.0, levelWidth, 20.0);
        const QRectF amplitudeHeader(
            levelHeader.right(), tableRect.top() + 4.0, amplitudeWidth, 20.0);

        painter.setPen(QColor(225, 225, 225));
        painter.drawText(
            rankHeader,
            Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("#"));
        painter.drawText(
            frequencyHeader,
            Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("MHz"));
        painter.drawText(
            levelHeader,
            Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("dB"));
        painter.drawText(
            amplitudeHeader,
            Qt::AlignRight | Qt::AlignVCenter,
            QStringLiteral("Amplitude"));

        for (int i = 0; i < measuredPeaks_.size(); ++i)
        {
            const MeasuredPeak& peak = measuredPeaks_[i];
            const double y = tableRect.top() + 26.0 +
                static_cast<double>(i) * rowHeight;

            const QRectF rankCell(left, y, rankWidth, rowHeight);
            const QRectF frequencyCell(
                rankCell.right(), y, frequencyWidth, rowHeight);
            const QRectF levelCell(
                frequencyCell.right(), y, levelWidth, rowHeight);
            const QRectF amplitudeCell(
                levelCell.right(), y, amplitudeWidth, rowHeight);

            painter.setPen(QColor(205, 210, 212));
            painter.drawText(
                rankCell,
                Qt::AlignLeft | Qt::AlignVCenter,
                QString::number(i + 1));
            painter.drawText(
                frequencyCell,
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(
                    peak.frequencyHz / 1'000'000.0,
                    'f',
                    4));
            painter.drawText(
                levelCell,
                Qt::AlignRight | Qt::AlignVCenter,
                QString::number(peak.db, 'f', 1));
            painter.drawText(
                amplitudeCell,
                Qt::AlignRight | Qt::AlignVCenter,
                amplitudeText(peak.amplitudeVoltsPeak));

            const QPointF peakPosition(
                xForFrequency(peak.frequencyHz),
                yForDb(peak.db));

            painter.setPen(QColor(255, 245, 160));
            painter.setBrush(QColor(9, 12, 14));
            painter.drawEllipse(peakPosition, 4.0, 4.0);
            painter.drawText(
                QRectF(
                    peakPosition.x() - 10.0,
                    peakPosition.y() - 24.0,
                    20.0,
                    18.0),
                Qt::AlignHCenter | Qt::AlignVCenter,
                QString::number(i + 1));
            painter.setBrush(Qt::NoBrush);
        }
    }

    const double row1Y = plotRect.bottom() + 34.0;
    const double row2Y = plotRect.bottom() + 60.0;
    const double row3Y = plotRect.bottom() + 86.0;

    painter.setPen(QColor(210, 210, 210));
    painter.drawText(
        QRectF(plotRect.left(), row1Y, plotRect.width() * 0.58, 22.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        measuredPeaks_.isEmpty()
        ? QStringLiteral("Peaks: press Measure Peaks for a stable top-5 snapshot")
        : QStringLiteral("Peaks: measured snapshot (%1 found)").arg(measuredPeaks_.size()));

    painter.setPen(QColor(180, 180, 180));
    painter.drawText(
        QRectF(plotRect.left(), row2Y, plotRect.width() * 0.55, 22.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("A-B: df=%1 MHz   dB=%2 dB")
            .arg(std::abs(markerActualHz[1] - markerActualHz[0]) / 1'000'000.0, 0, 'f', 4)
            .arg(markerDb[1] - markerDb[0], 0, 'f', 1));

    painter.setPen(QColor(225, 225, 225));
    painter.drawText(
        QRectF(plotRect.left() + plotRect.width() * 0.58, row1Y, plotRect.width() * 0.42, 22.0),
        Qt::AlignRight | Qt::AlignVCenter,
        noiseRmsVolts_ > 0.0
        ? (snrMeasurementEnabled_
            ? QStringLiteral("Noise: %1").arg(rmsText(noiseRmsVolts_))
            : QStringLiteral("Integrated AC: %1").arg(rmsText(noiseRmsVolts_)))
        : QStringLiteral("Noise: ---"));

    painter.setPen(QColor(170, 175, 178));
    painter.drawText(
        QRectF(plotRect.left() + plotRect.width() * 0.55, row2Y, plotRect.width() * 0.45, 22.0),
        Qt::AlignRight | Qt::AlignVCenter,
        !snrMeasurementEnabled_
        ? QStringLiteral("SNR N/A: digital ROM source")
        : (!inputSignalValid_
            ? QStringLiteral("SNR invalid: DeckLink reports no input source")
            : (!flatRegion_
                ? QStringLiteral("SNR invalid: selected line/ROI is not flat")
                : (noiseRmsVolts_ < kMinimumMeaningfulNoiseRmsVolts
                    ? QStringLiteral("SNR invalid: no measurable analogue noise")
                    : QStringLiteral("SNR BW 0.10-5.00 MHz   Ref 700 mV   AVG %1")
                        .arg(averageDepth())))));

    painter.setPen(QColor(150, 155, 158));
    painter.drawText(
        QRectF(plotRect.left(), row3Y, plotRect.width(), 22.0),
        Qt::AlignHCenter | Qt::AlignVCenter,
        QStringLiteral("Click/drag nearest marker   Measure Peaks snapshots top 5   Clear resets AVG/Hold/Peaks   line change auto-clears   F/Esc hides"));

    painter.setPen(QColor(190, 195, 198));
    painter.drawText(
        QRectF(plotRect.left(), plotRect.bottom() + 7.0, plotRect.width(), 22.0),
        Qt::AlignHCenter | Qt::AlignTop,
        QStringLiteral("Frequency (MHz)"));
}
