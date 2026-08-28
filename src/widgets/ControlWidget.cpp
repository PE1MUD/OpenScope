#include "widgets/ControlWidget.h"
#include "BuildConfig.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFont>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPaintEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
    int chromaUiToInternal(int value)
    {
        value = std::clamp(value, 0, 100);

        if (value == 0)
        {
            return 0;
        }

        // UI 1..100 maps to the previously useful UI range 20..100.
        const double oldUiValue =
            20.0 +
            static_cast<double>(value - 1) *
                (80.0 / 99.0);

        return std::clamp(
            static_cast<int>(
                std::lround(oldUiValue * 2.0)),
            40,
            200);
    }

    int chromaInternalToUi(int intensity)
    {
        intensity = std::clamp(intensity, 0, 200);

        if (intensity == 0)
        {
            return 0;
        }

        const double oldUiValue =
            static_cast<double>(intensity) / 2.0;

        return std::clamp(
            1 +
                static_cast<int>(
                    std::lround(
                        (oldUiValue - 20.0) *
                        (99.0 / 80.0))),
            1,
            100);
    }

    class ValueSlider final : public QSlider
    {
    public:
        explicit ValueSlider(
            Qt::Orientation orientation,
            QWidget* parent = nullptr)
            : QSlider(
                orientation,
                parent)
        {
            setMinimumHeight(
                22);

            setMaximumHeight(
                22);
        }

        void setValueFormatter(
            std::function<QString(int)> formatter)
        {
            valueFormatter_ =
                std::move(formatter);

            update();
        }

        void setDoubleClickResetsToMidpoint(bool enabled)
        {
            doubleClickResetsToMidpoint_ = enabled;
            doubleClickResetValue_.reset();
        }

        void setDoubleClickResetValue(int value)
        {
            doubleClickResetValue_ = value;
            doubleClickResetsToMidpoint_ = false;
        }

    protected:
        void mousePressEvent(
            QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton)
            {
                setSliderDown(
                    true);

                setValueFromMouse(
                    event->position());

                event->accept();
                return;
            }

            QSlider::mousePressEvent(
                event);
        }

        void mouseMoveEvent(
            QMouseEvent* event) override
        {
            if (isSliderDown() &&
                (event->buttons() & Qt::LeftButton) != 0)
            {
                setValueFromMouse(
                    event->position());

                event->accept();
                return;
            }

            QSlider::mouseMoveEvent(
                event);
        }

        void mouseReleaseEvent(
            QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton &&
                isSliderDown())
            {
                setValueFromMouse(
                    event->position());

                setSliderDown(
                    false);

                event->accept();
                return;
            }

            QSlider::mouseReleaseEvent(
                event);
        }

        void mouseDoubleClickEvent(
            QMouseEvent* event) override
        {
            if ((doubleClickResetsToMidpoint_ ||
                    doubleClickResetValue_.has_value()) &&
                event->button() == Qt::LeftButton)
            {
                const int resetValue =
                    doubleClickResetValue_.value_or(
                        minimum() +
                        (maximum() - minimum()) / 2);

                setValue(
                    std::clamp(
                        resetValue,
                        minimum(),
                        maximum()));
                emit sliderReleased();

                event->accept();
                return;
            }

            QSlider::mouseDoubleClickEvent(event);
        }

        void paintEvent(
            QPaintEvent* event) override
        {
            Q_UNUSED(event);

            QStyleOptionSlider option;
            initStyleOption(
                &option);

            QPainter painter(
                this);

            /*
             * Draw the native groove, but not the native handle.  The value
             * bubble below is the actual handle and needs a wider safe travel
             * range than the platform handle.  Letting Qt place the small
             * native handle at the absolute end while centring our wider
             * bubble on it caused the bubble to be clipped at 0 / 100.
             */
            QStyleOptionSlider grooveOption = option;
            grooveOption.subControls =
                QStyle::SC_SliderGroove;

            style()->drawComplexControl(
                QStyle::CC_Slider,
                &grooveOption,
                &painter,
                this);

            const int valueWidth =
                valueFormatter_
                ? 58
                : 38;

            const int valueHeight =
                std::max(
                    18,
                    height() - 2);

            const int span =
                std::max(
                    width() - valueWidth,
                    1);

            const int sliderPosition =
                QStyle::sliderPositionFromValue(
                    minimum(),
                    maximum(),
                    value(),
                    span,
                    invertedAppearance());

            QRect handleRect(
                sliderPosition,
                (height() - valueHeight) / 2,
                valueWidth,
                valueHeight);

            painter.setRenderHint(
                QPainter::Antialiasing);

            painter.setPen(
                palette().color(
                    QPalette::Mid));

            painter.setBrush(
                palette().color(
                    QPalette::Button));

            painter.drawRoundedRect(
                handleRect.adjusted(
                    0,
                    1,
                    -1,
                    -1),
                3.0,
                3.0);

            QFont font =
                painter.font();

            if (font.pointSizeF() > 0.0)
            {
                font.setPointSizeF(
                    std::max(
                        7.0,
                        font.pointSizeF() - 1.0));
            }

            font.setBold(
                true);

            painter.setFont(
                font);

            painter.setPen(
                isEnabled()
                ? palette().color(
                    QPalette::ButtonText)
                : palette().color(
                    QPalette::Disabled,
                    QPalette::ButtonText));

            painter.drawText(
                handleRect,
                Qt::AlignCenter,
                valueFormatter_
                ? valueFormatter_(value())
                : QString::number(value()));
        }

    private:
        std::function<QString(int)> valueFormatter_;
        bool doubleClickResetsToMidpoint_ = false;
        std::optional<int> doubleClickResetValue_;

        void setValueFromMouse(
            const QPointF& position)
        {
            const int valueWidth =
                valueFormatter_
                ? 58
                : 38;

            const int span =
                (std::max)(
                    width() - valueWidth,
                    1);

            const int pixelPosition =
                std::clamp(
                    static_cast<int>(
                        std::lround(
                            position.x() -
                            valueWidth * 0.5)),
                    0,
                    span);

            setValue(
                QStyle::sliderValueFromPosition(
                    minimum(),
                    maximum(),
                    pixelPosition,
                    span,
                    invertedAppearance()));
        }
    };

    QWidget* createSliderRow(
        const QString& labelText,
        QSlider*& slider,
        int minimum,
        int maximum,
        int value,
        QWidget* parent,
        std::function<QString(int)> valueFormatter = {})
    {
        auto* row =
            new QWidget(parent);

        auto* layout =
            new QHBoxLayout(row);

        layout->setContentsMargins(
            0,
            0,
            0,
            0);

        layout->setSpacing(8);

        auto* label =
            new QLabel(
                labelText,
                row);

        // Keep every user-facing slider on the same horizontal ruler.
        // Longest current label ("Color carrier intensity") fits here,
        // while still leaving useful slider travel in a 380 px matrix cell.
        label->setFixedWidth(
            150);

        auto* valueSlider =
            new ValueSlider(
                Qt::Horizontal,
                row);

        valueSlider->setValueFormatter(
            std::move(valueFormatter));

        slider =
            valueSlider;

        slider->setRange(
            minimum,
            maximum);

        slider->setValue(
            value);

        layout->addWidget(
            label);

        layout->addWidget(
            slider,
            1);

        return row;
    }
}

ControlWidget::ControlWidget(
    const OpenScopeSettings& settings,
    QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout =
        new QVBoxLayout(this);

    outerLayout->setContentsMargins(
        4,
        4,
        4,
        4);

    outerLayout->setSpacing(0);

    tabs_ =
        new QTabWidget(this);

    auto* tabs =
        tabs_;

    outerLayout->addWidget(tabs_);

    // ------------------------------------------------------------
    // Display
    // ------------------------------------------------------------
    auto* displayTab =
        new QWidget(tabs);

    auto* displayLayout =
        new QVBoxLayout(displayTab);

    displayLayout->setContentsMargins(
        6,
        4,
        6,
        4);

    displayLayout->setSpacing(4);

    auto* noiseReductionCheckBox =
        new QCheckBox(
            "Enable noise reduction",
            displayTab);

    noiseReductionCheckBox->setChecked(
        settings.control
            .processing
            .noiseFilter
            .enabled);

    displayLayout->addWidget(
        noiseReductionCheckBox);

    QSlider* noiseReductionSlider = nullptr;

    QWidget* noiseReductionRow =
        createSliderRow(
            "Noise reduction intensity",
            noiseReductionSlider,
            0,
            100,
            std::clamp(
                settings.control
                    .processing
                    .noiseFilter
                    .strength,
                0,
                100),
            displayTab);

    noiseReductionSlider->setEnabled(
        noiseReductionCheckBox->isChecked());

    displayLayout->addWidget(
        noiseReductionRow);

    auto* lineSelectorVisibleCheckBox =
        new QCheckBox(
            "Line Selector Visible",
            displayTab);
    lineSelectorVisibleCheckBox->setChecked(
        settings.local.display.lineSelectorVisible);
    displayLayout->addWidget(lineSelectorVisibleCheckBox);

    auto* safetyArea90CheckBox =
        new QCheckBox(
            "Safety area 90%",
            displayTab);
    safetyArea90CheckBox->setChecked(
        settings.local.display.safetyArea90);
    displayLayout->addWidget(safetyArea90CheckBox);

    auto* textSafetyArea80CheckBox =
        new QCheckBox(
            "Text safety area 80%",
            displayTab);
    textSafetyArea80CheckBox->setChecked(
        settings.local.display.textSafetyArea80);
    displayLayout->addWidget(textSafetyArea80CheckBox);

    displayLayout->addStretch();

    connect(
        noiseReductionCheckBox,
        &QCheckBox::toggled,
        noiseReductionSlider,
        &QSlider::setEnabled);

    connect(
        noiseReductionCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::noiseReductionChanged);

    connect(
        noiseReductionSlider,
        &QSlider::valueChanged,
        this,
        &ControlWidget::noiseReductionIntensityChanged);

    connect(lineSelectorVisibleCheckBox, &QCheckBox::toggled,
        this, &ControlWidget::lineSelectorVisibleChanged);
    connect(safetyArea90CheckBox, &QCheckBox::toggled,
        this, &ControlWidget::safetyArea90Changed);
    connect(textSafetyArea80CheckBox, &QCheckBox::toggled,
        this, &ControlWidget::textSafetyArea80Changed);

    tabs->addTab(
        displayTab,
        "Display");

    // ------------------------------------------------------------
    // Calibration
    // ------------------------------------------------------------
    auto* calibrationTab =
        new QWidget(tabs);

    auto* calibrationLayout =
        new QVBoxLayout(calibrationTab);

    calibrationLayout->setContentsMargins(
        6,
        4,
        6,
        4);

    calibrationLayout->setSpacing(4);

    auto* deckLinkHeading =
        new QLabel(
            "Blackmagic Level Controls",
            calibrationTab);

    QFont headingFont =
        deckLinkHeading->font();
    headingFont.setBold(true);
    deckLinkHeading->setFont(headingFont);

    calibrationLayout->addWidget(
        deckLinkHeading);

    compositeGainStatusLabel_ =
        new QLabel(
            "Waiting for DeckLink device...",
            calibrationTab);

    calibrationLayout->addWidget(
        compositeGainStatusLabel_);

    QWidget* compositeLumaGainRow =
        createSliderRow(
            "Luma gain",
            compositeLumaGainSlider_,
            0,
            0,
            0,
            calibrationTab,
            [this](int value)
            {
                if (compositeLumaGainSlider_ == nullptr)
                {
                    return QStringLiteral("1.00x");
                }

                const int minimum =
                    compositeLumaGainSlider_->minimum();

                const int maximum =
                    compositeLumaGainSlider_->maximum();

                const double gain =
                    maximum > minimum
                    ? 2.0 *
                        static_cast<double>(value - minimum) /
                        static_cast<double>(maximum - minimum)
                    : 1.0;

                return
                    QString::number(gain, 'f', 2) +
                    QStringLiteral("x");
            });

    static_cast<ValueSlider*>(
        compositeLumaGainSlider_)
        ->setDoubleClickResetsToMidpoint(true);

    compositeLumaGainSlider_->setEnabled(false);

    calibrationLayout->addWidget(
        compositeLumaGainRow);

    QWidget* compositeChromaGainRow =
        createSliderRow(
            "Chroma gain",
            compositeChromaGainSlider_,
            0,
            0,
            0,
            calibrationTab,
            [this](int value)
            {
                if (compositeChromaGainSlider_ == nullptr)
                {
                    return QStringLiteral("1.00x");
                }

                const int minimum =
                    compositeChromaGainSlider_->minimum();

                const int maximum =
                    compositeChromaGainSlider_->maximum();

                const double gain =
                    maximum > minimum
                    ? 2.0 *
                        static_cast<double>(value - minimum) /
                        static_cast<double>(maximum - minimum)
                    : 1.0;

                return
                    QString::number(gain, 'f', 2) +
                    QStringLiteral("x");
            });

    static_cast<ValueSlider*>(
        compositeChromaGainSlider_)
        ->setDoubleClickResetsToMidpoint(true);

    compositeChromaGainSlider_->setEnabled(false);

    calibrationLayout->addWidget(
        compositeChromaGainRow);

    auto* lumaCompensationHeading =
        new QLabel(
            "OpenScope luma response correction",
            calibrationTab);

    lumaCompensationHeading->setFont(headingFont);

    calibrationLayout->addSpacing(8);
    calibrationLayout->addWidget(
        lumaCompensationHeading);

    auto* lumaCompensationCheckBox =
        new QCheckBox(
            "Enable Y frequency response correction",
            calibrationTab);

    lumaCompensationCheckBox->setChecked(
        settings.control
            .processing
            .lumaCompensation
            .enabled);

    calibrationLayout->addWidget(
        lumaCompensationCheckBox);

    QSlider* lumaCompensationSlider = nullptr;

    QWidget* lumaCompensationRow =
        createSliderRow(
            "Y HF comp @ 5.8 MHz",
            lumaCompensationSlider,
            0,
            100,
            std::clamp(
                settings.control
                    .processing
                    .lumaCompensation
                    .gainHundredthsDb,
                0,
                100),
            calibrationTab,
            [](int value)
            {
                return
                    QString::number(
                        static_cast<double>(value) / 100.0,
                        'f',
                        2) +
                    " dB";
            });

    static_cast<ValueSlider*>(
        lumaCompensationSlider)
        ->setDoubleClickResetsToMidpoint(true);

    lumaCompensationSlider->setEnabled(
        lumaCompensationCheckBox->isChecked());

    calibrationLayout->addWidget(
        lumaCompensationRow);

    calibrationLayout->addStretch();

    connect(
        compositeLumaGainSlider_,
        &QSlider::valueChanged,
        this,
        &ControlWidget::compositeLumaGainChanged);

    connect(
        compositeChromaGainSlider_,
        &QSlider::valueChanged,
        this,
        &ControlWidget::compositeChromaGainChanged);

    connect(
        compositeLumaGainSlider_,
        &QSlider::sliderReleased,
        this,
        &ControlWidget::compositeGainCommitRequested);

    connect(
        compositeChromaGainSlider_,
        &QSlider::sliderReleased,
        this,
        &ControlWidget::compositeGainCommitRequested);

    connect(
        lumaCompensationCheckBox,
        &QCheckBox::toggled,
        lumaCompensationSlider,
        &QSlider::setEnabled);

    connect(
        lumaCompensationCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::lumaCompensationChanged);

    connect(
        lumaCompensationSlider,
        &QSlider::valueChanged,
        this,
        &ControlWidget::lumaCompensationGainChanged);

    // Calibration is inserted immediately before Help below.

    // ------------------------------------------------------------
    // Instrument
    // ------------------------------------------------------------
    auto* instrumentTab =
        new QWidget(tabs);

    auto* instrumentLayout =
        new QVBoxLayout(instrumentTab);

    instrumentLayout->setContentsMargins(
        6,
        4,
        6,
        4);

    instrumentLayout->setSpacing(4);

    auto* lineRow =
        new QWidget(instrumentTab);

    auto* lineLayout =
        new QHBoxLayout(lineRow);

    lineLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    lineLayout->setSpacing(
        8);

    lineLayout->addWidget(
        new QLabel(
            "Line selector",
            lineRow));

    lineSelector_ =
        new QSpinBox(lineRow);

    lineSelector_->setRange(
        -1,
        575);

    lineSelector_->setSpecialValueText(
        "All");

    lineSelector_->setFixedHeight(
        22);

    lineSelector_->setValue(
        settings.control
            .instrument
            .lineNumber);

    lineLayout->addWidget(
        lineSelector_,
        1);

    instrumentLayout->addWidget(
        lineRow);

    auto* antiAliasingCheckBox =
        new QCheckBox(
            "Anti Aliasing",
            instrumentTab);

    antiAliasingCheckBox->setChecked(
        settings.control
            .instrument
            .waveform
            .antiAliasing);

    instrumentLayout->addSpacing(
        8);

    instrumentLayout->addWidget(
        antiAliasingCheckBox);

    auto* vintageCheckBox =
        new QCheckBox(
            "Vintage Look",
            instrumentTab);

    vintageCheckBox->setChecked(
        settings.control
            .instrument
            .waveform
            .vintageLook);

    instrumentLayout->addWidget(
        vintageCheckBox);

    auto* colorizeIllegalLuminanceCheckBox =
        new QCheckBox(
            "Colorize Illegal Luminance",
            instrumentTab);

    colorizeIllegalLuminanceCheckBox->setChecked(
        settings.control
            .instrument
            .waveform
            .colorizeIllegalLuminance);

    instrumentLayout->addWidget(
        colorizeIllegalLuminanceCheckBox);

    auto* colorizeGamutErrorsCheckBox =
        new QCheckBox(
            "Colorize Gamut Errors",
            instrumentTab);

    colorizeGamutErrorsCheckBox->setChecked(
        settings.control
            .instrument
            .vectorscope
            .colorizeGamutErrors);

    instrumentLayout->addWidget(
        colorizeGamutErrorsCheckBox);

    instrumentLayout->addSpacing(
        8);

    connect(
        antiAliasingCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::antiAliasingChanged);

    connect(
        colorizeIllegalLuminanceCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::colorizeIllegalLuminanceChanged);

    connect(
        colorizeGamutErrorsCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::colorizeGamutErrorsChanged);

    auto* zoomRow =
        new QWidget(instrumentTab);

    auto* zoomLayout =
        new QHBoxLayout(zoomRow);

    zoomLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    zoomLayout->setSpacing(
        4);

    zoomLayout->addWidget(
        new QLabel(
            "Waveform X",
            zoomRow));

    waveformZoomButtonGroup_ =
        new QButtonGroup(this);

    waveformZoomButtonGroup_->setExclusive(
        true);

    waveformZoom1Button_ =
        new QToolButton(zoomRow);

    waveformZoom5Button_ =
        new QToolButton(zoomRow);

    waveformZoom10Button_ =
        new QToolButton(zoomRow);

    waveformZoom1Button_->setText(
        "X1");

    waveformZoom5Button_->setText(
        "X5");

    waveformZoom10Button_->setText(
        "X10");

    waveformZoom1Button_->setFixedHeight(
        22);

    waveformZoom5Button_->setFixedHeight(
        22);

    waveformZoom10Button_->setFixedHeight(
        22);

    waveformZoom1Button_->setCheckable(
        true);

    waveformZoom5Button_->setCheckable(
        true);

    waveformZoom10Button_->setCheckable(
        true);

    waveformZoomButtonGroup_->addButton(
        waveformZoom1Button_,
        1);

    waveformZoomButtonGroup_->addButton(
        waveformZoom5Button_,
        5);

    waveformZoomButtonGroup_->addButton(
        waveformZoom10Button_,
        10);

    const int waveformZoom =
        settings.control
            .instrument
            .waveform
            .zoom;

    QAbstractButton* checkedZoomButton =
        waveformZoomButtonGroup_->button(
            waveformZoom);

    if (checkedZoomButton == nullptr)
    {
        checkedZoomButton =
            waveformZoom1Button_;
    }

    checkedZoomButton->setChecked(
        true);

    const bool waveformZoomEnabled =
        lineSelector_->value() >= 0;

    waveformZoom1Button_->setEnabled(
        waveformZoomEnabled);

    waveformZoom5Button_->setEnabled(
        waveformZoomEnabled);

    waveformZoom10Button_->setEnabled(
        waveformZoomEnabled);

    zoomLayout->addWidget(
        waveformZoom1Button_);

    zoomLayout->addWidget(
        waveformZoom5Button_);

    zoomLayout->addWidget(
        waveformZoom10Button_);

    zoomLayout->addStretch(
        1);

    instrumentLayout->addWidget(
        zoomRow);

    QSlider* chromaIntensitySlider = nullptr;

    QWidget* chromaIntensityRow =
        createSliderRow(
            "Color carrier intensity",
            chromaIntensitySlider,
            0,
            100,
            chromaInternalToUi(
                settings.control
                    .instrument
                    .waveform
                    .chromaRenderIntensity),
            instrumentTab);

    instrumentLayout->addWidget(
        chromaIntensityRow);

    QSlider* persistenceSlider = nullptr;

    QWidget* persistenceRow =
        createSliderRow(
            "Scopephor",
            persistenceSlider,
            0,
            100,
            std::clamp(
                settings.control
                    .instrument
                    .waveform
                    .persistenceFrames,
                0,
                200) /
                2,
            instrumentTab);

    instrumentLayout->addWidget(
        persistenceRow);

    QSlider* coreIntensitySlider = nullptr;

    QWidget* coreIntensityRow =
        createSliderRow(
            "Core intensity",
            coreIntensitySlider,
            0,
            100,
            std::clamp(
                settings.control
                    .instrument
                    .waveform
                    .coreIntensity,
                0,
                100),
            instrumentTab);

    instrumentLayout->addWidget(
        coreIntensityRow);

    QSlider* vectorscopeGlowSlider = nullptr;

    QWidget* vectorscopeGlowRow =
        createSliderRow(
            "Beam Glow",
            vectorscopeGlowSlider,
            0,
            100,
            std::clamp(
                settings.control
                    .instrument
                    .vectorscope
                    .glow,
                0,
                100),
            instrumentTab);

    instrumentLayout->addWidget(
        vectorscopeGlowRow);

    auto* defaultsRow =
        new QWidget(
            instrumentTab);

    auto* defaultsLayout =
        new QHBoxLayout(
            defaultsRow);

    defaultsLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    defaultsLayout->setSpacing(
        0);

    defaultsLayout->addStretch(
        1);

    auto* defaultsButton =
        new QPushButton(
            "Defaults",
            defaultsRow);

    defaultsButton->setFixedHeight(
        22);

    defaultsButton->setToolTip(
        "Reset waveform display controls to defaults");

    defaultsLayout->addWidget(
        defaultsButton);

    instrumentLayout->addWidget(
        defaultsRow);

    instrumentLayout->addStretch();

    connect(
        lineSelector_,
        &QSpinBox::valueChanged,
        this,
        [this](int lineNumber)
        {
            const bool enabled =
                lineNumber >= 0;

            waveformZoom1Button_->setEnabled(
                enabled);

            waveformZoom5Button_->setEnabled(
                enabled);

            waveformZoom10Button_->setEnabled(
                enabled);

            if (!enabled &&
                !waveformZoom1Button_->isChecked())
            {
                waveformZoom1Button_->setChecked(
                    true);

                emit waveformZoomChanged(
                    1);
            }

            emit lineNumberChanged(
                lineNumber);
        });

    connect(
        waveformZoomButtonGroup_,
        &QButtonGroup::idClicked,
        this,
        [this](int zoomFactor)
        {
            emit waveformZoomChanged(
                zoomFactor);
        });

    connect(
        persistenceSlider,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            const int normalizedValue =
                std::clamp(
                    value,
                    0,
                    100);

            emit waveformPersistenceChanged(
                normalizedValue *
                2);
        });

    connect(
        coreIntensitySlider,
        &QSlider::valueChanged,
        this,
        &ControlWidget::waveformCoreIntensityChanged);

    connect(
        vectorscopeGlowSlider,
        &QSlider::valueChanged,
        this,
        &ControlWidget::vectorscopeGlowChanged);

    connect(
        vintageCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::vintageLookChanged);

    connect(
        chromaIntensitySlider,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            emit chromaRenderIntensityChanged(
                chromaUiToInternal(value));
        });

    connect(
        defaultsButton,
        &QPushButton::clicked,
        this,
        [chromaIntensitySlider,
         persistenceSlider,
         coreIntensitySlider,
         vectorscopeGlowSlider,
         vintageCheckBox,
         antiAliasingCheckBox,
         colorizeIllegalLuminanceCheckBox,
         colorizeGamutErrorsCheckBox]()
        {
            constexpr int defaultValue = 50;

            chromaIntensitySlider->setValue(
                50);

            persistenceSlider->setValue(
                defaultValue);

            coreIntensitySlider->setValue(
                100);

            vectorscopeGlowSlider->setValue(
                defaultValue);

            vintageCheckBox->setChecked(
                false);

            antiAliasingCheckBox->setChecked(
                true);

            colorizeIllegalLuminanceCheckBox->setChecked(
                true);

            colorizeGamutErrorsCheckBox->setChecked(
                true);
        });

    tabs->addTab(
        instrumentTab,
        "Instrument");

    // ------------------------------------------------------------
    // View FPS
    // ------------------------------------------------------------
    auto* viewFpsTab =
        new QWidget(tabs);

    auto* viewFpsLayout =
        new QGridLayout(viewFpsTab);

    viewFpsLayout->setContentsMargins(
        6,
        4,
        6,
        4);

    viewFpsLayout->setHorizontalSpacing(12);
    viewFpsLayout->setVerticalSpacing(6);

    auto* viewHeader =
        new QLabel("View", viewFpsTab);
    auto* openScopeHeader =
        new QLabel("OpenScope FPS", viewFpsTab);
    auto* spoutHeader =
        new QLabel("Spout FPS", viewFpsTab);

    QFont fpsHeaderFont = viewHeader->font();
    fpsHeaderFont.setBold(true);
    viewHeader->setFont(fpsHeaderFont);
    openScopeHeader->setFont(fpsHeaderFont);
    spoutHeader->setFont(fpsHeaderFont);

    // Keep the headers anchored to the same edge as the values.
    // Previously the numeric values were right-aligned while their headers
    // were left-aligned inside stretchable columns, so resizing made the
    // labels and numbers visibly drift apart.
    viewHeader->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    openScopeHeader->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    spoutHeader->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    viewFpsLayout->addWidget(viewHeader, 0, 0);
    viewFpsLayout->addWidget(openScopeHeader, 0, 1);
    viewFpsLayout->addWidget(spoutHeader, 0, 2);

    videoOpenScopeFpsLabel_ = new QLabel("0.0", viewFpsTab);
    videoSpoutFpsLabel_ = new QLabel("0.0", viewFpsTab);
    waveformOpenScopeFpsLabel_ = new QLabel("0.0", viewFpsTab);
    waveformSpoutFpsLabel_ = new QLabel("0.0", viewFpsTab);
    vectorscopeOpenScopeFpsLabel_ = new QLabel("0.0", viewFpsTab);
    vectorscopeSpoutFpsLabel_ = new QLabel("0.0", viewFpsTab);

    for (QLabel* valueLabel :
         {
             videoOpenScopeFpsLabel_,
             videoSpoutFpsLabel_,
             waveformOpenScopeFpsLabel_,
             waveformSpoutFpsLabel_,
             vectorscopeOpenScopeFpsLabel_,
             vectorscopeSpoutFpsLabel_
         })
    {
        valueLabel->setAlignment(
            Qt::AlignRight | Qt::AlignVCenter);
    }

    viewFpsLayout->addWidget(new QLabel("Video", viewFpsTab), 1, 0);
    viewFpsLayout->addWidget(videoOpenScopeFpsLabel_, 1, 1);
    viewFpsLayout->addWidget(videoSpoutFpsLabel_, 1, 2);

    viewFpsLayout->addWidget(new QLabel("Waveform", viewFpsTab), 2, 0);
    viewFpsLayout->addWidget(waveformOpenScopeFpsLabel_, 2, 1);
    viewFpsLayout->addWidget(waveformSpoutFpsLabel_, 2, 2);

    viewFpsLayout->addWidget(new QLabel("Vectorscope", viewFpsTab), 3, 0);
    viewFpsLayout->addWidget(vectorscopeOpenScopeFpsLabel_, 3, 1);
    viewFpsLayout->addWidget(vectorscopeSpoutFpsLabel_, 3, 2);

    viewFpsLayout->setColumnStretch(0, 2);
    viewFpsLayout->setColumnStretch(1, 1);
    viewFpsLayout->setColumnStretch(2, 1);

    const int fpsColumnMinimum =
        (std::max)(
            openScopeHeader->sizeHint().width(),
            spoutHeader->sizeHint().width());
    viewFpsLayout->setColumnMinimumWidth(1, fpsColumnMinimum);
    viewFpsLayout->setColumnMinimumWidth(2, fpsColumnMinimum);
    viewFpsLayout->setRowStretch(4, 1);

    // The tab is inserted after Calibration below so the existing
    // Display/Instrument/Misc/Calibration indices remain unchanged.

    // ------------------------------------------------------------
    // Misc
    // ------------------------------------------------------------
    auto* miscTab =
        new QWidget(tabs);

    auto* miscLayout =
        new QVBoxLayout(miscTab);

    miscLayout->setContentsMargins(
        6,
        4,
        6,
        4);

    miscLayout->setSpacing(4);

    legacyAspectRatioCheckBox_ =
        new QCheckBox(
            "Legacy Aspect Ratio",
            miscTab);

    legacyAspectRatioCheckBox_->setChecked(
        settings.local
            .display
            .aspectRatio ==
        OpenScopeSettings::AspectRatio::Ratio4x3);

    miscLayout->addWidget(
        legacyAspectRatioCheckBox_);

    performanceCheckBox_ =
        new QCheckBox(
            "Show Performance Floaty",
            miscTab);

    performanceCheckBox_->setChecked(
        settings.local
            .floaties
            .performanceVisible);

    miscLayout->addWidget(
        performanceCheckBox_);

    auto* preventDisplaySleepCheckBox =
        new QCheckBox(
            "Prevent display sleep / screensaver",
            miscTab);

    preventDisplaySleepCheckBox->setChecked(
        settings.local.display.preventDisplaySleep);

    preventDisplaySleepCheckBox->setToolTip(
        "Keep Windows display/system idle sleep from activating while OpenScope is running. Default is off.");

    miscLayout->addWidget(
        preventDisplaySleepCheckBox);

    if constexpr (OpenScopeBuild::kDebugBuild)
    {
        QSlider* coreWidthSlider = nullptr;

        QWidget* coreWidthRow =
            createSliderRow(
                "Core width",
                coreWidthSlider,
                5,
                30,
                std::clamp(
                    settings.control
                        .instrument
                        .waveform
                        .coreWidthTenths,
                    5,
                    30),
                miscTab,
                [](int value)
                {
                    return QString::number(
                        static_cast<double>(value) / 10.0,
                        'f',
                        1) +
                        " px";
                });

        static_cast<ValueSlider*>(coreWidthSlider)
            ->setDoubleClickResetValue(10);

        coreWidthSlider->setToolTip(
            "Waveform core width. Double-click to reset to 1.0 px.");

        miscLayout->addWidget(
            coreWidthRow);

        connect(
            coreWidthSlider,
            &QSlider::valueChanged,
            this,
            &ControlWidget::waveformCoreWidthChanged);

    }

    auto* floatiesHomeButton =
        new QPushButton(
            "Floaties 2 Home",
            miscTab);

    floatiesHomeButton->setToolTip(
        "Bring OpenScope floating windows back to visible positions on the current screen");

    miscLayout->addWidget(
        floatiesHomeButton);

    auto* spoutLabel =
        new QLabel(
            "Spout output",
            miscTab);

    miscLayout->addWidget(
        spoutLabel);

    auto* spoutVideoCheckBox =
        new QCheckBox(
            "Video",
            miscTab);

    spoutVideoCheckBox->setChecked(
        settings.local
            .spout
            .videoEnabled);

    miscLayout->addWidget(
        spoutVideoCheckBox);

    auto* spoutWaveformCheckBox =
        new QCheckBox(
            "Waveform",
            miscTab);

    spoutWaveformCheckBox->setChecked(
        settings.local
            .spout
            .waveformEnabled);

    miscLayout->addWidget(
        spoutWaveformCheckBox);

    auto* spoutVectorscopeCheckBox =
        new QCheckBox(
            "Vectorscope",
            miscTab);

    spoutVectorscopeCheckBox->setChecked(
        settings.local
            .spout
            .vectorscopeEnabled);


    miscLayout->addWidget(
        spoutVectorscopeCheckBox);

    auto* exportHighResolutionPngButton =
        new QPushButton(
            "Export high-res PNG...",
            miscTab);

    exportHighResolutionPngButton->setToolTip(
        "Choose a file name and export the next complete captured frame as a 2880 x 2304 PNG");

    miscLayout->addWidget(
        exportHighResolutionPngButton);

    auto* exportHighResolutionPngBamButton =
        new QPushButton(
            "Export high-res PNG BAM",
            miscTab);

    exportHighResolutionPngBamButton->setToolTip(
        "Immediately export the next complete captured frame to the remembered folder using the next free capture_0001.png number");

    miscLayout->addWidget(
        exportHighResolutionPngBamButton);

    if constexpr (OpenScopeBuild::kDebugBuild)
    {
        auto* waveformRawCaptureButton =
            new QPushButton(
                "Capture waveform RAW...",
                miscTab);

        waveformRawCaptureButton->setToolTip(
            "Capture 250 frames of the currently selected reconstructed 2880-sample Y line");

        miscLayout->addWidget(
            waveformRawCaptureButton);

        connect(
            waveformRawCaptureButton,
            &QPushButton::clicked,
            this,
            &ControlWidget::waveformRawCaptureRequested);
    }

    miscLayout->addStretch();

    connect(
        legacyAspectRatioCheckBox_,
        &QCheckBox::toggled,
        this,
        &ControlWidget::legacyAspectRatioChanged);

    connect(
        performanceCheckBox_,
        &QCheckBox::toggled,
        this,
        &ControlWidget::performanceVisibilityChanged);

    connect(
        preventDisplaySleepCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::preventDisplaySleepChanged);

    connect(
        floatiesHomeButton,
        &QPushButton::clicked,
        this,
        &ControlWidget::floatiesHomeRequested);

    connect(
        spoutVideoCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::spoutVideoEnabledChanged);

    connect(
        spoutWaveformCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::spoutWaveformEnabledChanged);

    connect(
        spoutVectorscopeCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::spoutVectorscopeEnabledChanged);

    connect(
        exportHighResolutionPngButton,
        &QPushButton::clicked,
        this,
        &ControlWidget::exportHighResolutionPngRequested);

    connect(
        exportHighResolutionPngBamButton,
        &QPushButton::clicked,
        this,
        &ControlWidget::exportHighResolutionPngQuickRequested);

    tabs->addTab(
        miscTab,
        "Misc");

    tabs->addTab(
        calibrationTab,
        "Calibration");

    tabs->addTab(
        viewFpsTab,
        "View FPS");

    // ------------------------------------------------------------
    // Help - only shown while the workspace is in quad view.
    // ------------------------------------------------------------
    auto* helpTab =
        new QWidget(tabs);

    auto* helpLayout =
        new QVBoxLayout(helpTab);

    helpLayout->setContentsMargins(
        6,
        4,
        6,
        4);

    auto* waveformHelp =
        new QTextBrowser(helpTab);

    waveformHelp->setOpenExternalLinks(false);
    waveformHelp->setFrameShape(QFrame::NoFrame);
    waveformHelp->setHtml(
        QStringLiteral(
            R"HTML(
<h3>Waveform</h3>
<p><b>Line selector</b><br>
Selects one video line. <b>All</b> shows all lines and disables X5/X10.</p>

<p><b>X1 / X5 / X10</b><br>
Horizontal waveform zoom only. In X5/X10, drag with the <b>right mouse button</b> to pan left/right.</p>

<p><b>Color carrier intensity</b><br>
0 disables the chroma-envelope rendering. Values 1-100 span the useful color range.</p>

<p><b>Scopephor</b><br>
Controls trace persistence.</p>

<p><b>Vintage look</b><br>
Enables the more analogue scope appearance.</p>

<h3>Waveform measurement</h3>
<p><b>D — Details</b><br>
Hold D to show &micro;s from line start and source pixel (0–719) in the blue probe.</p>

<p><b>Left mouse drag</b><br>
Manual point-to-point measurement of voltage difference and frequency.</p>

<p><b>R + left mouse drag</b><br>
Defines a reference area. The result is averaged over up to four frames.
Magenta reference level lines can be dragged vertically for manual correction.</p>

<p><b>A + left mouse drag</b><br>
Measures a sinusoidal area. Frequency is shown with amplitude in mV, or in dB when a reference exists.
Green measurement level lines can be dragged vertically and the result follows the correction.</p>

<p><b>M</b><br>
Automatic multiburst measurement. Periodic zones around 50% video level are detected,
the lowest-frequency signal becomes the reference, and the remaining valid bursts are measured.
Partial results are shown when at least four bursts are found.</p>

<p><b>C</b><br>
Clears all waveform measurements and references.</p>

<p><b>Double click</b><br>
Clears measurements and toggles the waveform viewport between quad and maximized view.</p>

<p><i>Measurements are also cleared when the selected line changes or the waveform view is resized.</i></p>
)HTML"));

    helpLayout->addWidget(
        waveformHelp);

    helpTabIndex_ =
        tabs->addTab(
            helpTab,
            "Help");

    // ------------------------------------------------------------
    // About
    // ------------------------------------------------------------
    auto* aboutTab = new QWidget(tabs);
    auto* aboutLayout = new QVBoxLayout(aboutTab);
    aboutLayout->setContentsMargins(16, 16, 16, 16);
    aboutLayout->setSpacing(12);

    aboutLogoLabel_ = new QLabel(aboutTab);
    QPixmap initialAboutLogo(
        QStringLiteral(":/branding/OpenScopeAboutLogo.png"));
    if (!initialAboutLogo.isNull())
    {
        aboutLogoLabel_->setPixmap(
            initialAboutLogo.scaled(
                128,
                128,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
        aboutLogoLabel_->setFixedSize(128, 128);
    }
    aboutLogoLabel_->setAlignment(Qt::AlignTop | Qt::AlignRight);
    aboutLogoLabel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    aboutLayout->addWidget(
        aboutLogoLabel_,
        0,
        Qt::AlignTop | Qt::AlignRight);

    auto* aboutText = new QLabel(
        QStringLiteral(
            "OpenScope is a software waveform monitor, vectorscope and video toolbox. "
            "It was created because nothing is available for free that provides proper "
            "video monitoring and vectorscope functionality. Since it works on a BT.656 "
            "digital representation of analog video, some restrictions apply. "
            "There is no burst phase check, no 8fs indicator and no VITS line viewing."),
        aboutTab);
    aboutText->setWordWrap(true);
    aboutText->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    aboutLayout->addWidget(aboutText);
    aboutLayout->addStretch();

    aboutTabIndex_ =
        tabs->addTab(
            aboutTab,
            "About");

    cornerLogoLabel_ = new QLabel(this);
    cornerLogoLabel_->setAlignment(Qt::AlignCenter);
    cornerLogoLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    cornerLogoLabel_->hide();

    connect(
        tabs_,
        &QTabWidget::currentChanged,
        this,
        [this](int)
        {
            updateBrandingLayout();
        });

    updateBrandingLayout();
}

void ControlWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateBrandingLayout();
}

void ControlWidget::updateBrandingLayout()
{
    if (aboutLogoLabel_ != nullptr)
    {
        const int availableWidth = std::max(width() - 32, 0);
        const int availableHeight = std::max(height() - 72, 0);

        if (availableWidth < 360 || availableHeight < 180)
        {
            aboutLogoLabel_->hide();
        }
        else
        {
            const int target =
                std::clamp(
                    std::min(
                        availableWidth / 4,
                        availableHeight / 3),
                    80,
                    192);

            QPixmap logoPixmap(
                QStringLiteral(":/branding/OpenScopeAboutLogo.png"));

            if (logoPixmap.isNull())
            {
                aboutLogoLabel_->hide();
            }
            else
            {
                aboutLogoLabel_->setFixedSize(target, target);
                aboutLogoLabel_->setPixmap(
                    logoPixmap.scaled(
                        target,
                        target,
                        Qt::KeepAspectRatio,
                        Qt::SmoothTransformation));
                aboutLogoLabel_->show();
            }
        }
    }

    if (cornerLogoLabel_ == nullptr ||
        tabs_ == nullptr)
    {
        return;
    }

    const int availableWidth = width();
    const int availableHeight = height();
    const bool enoughRoom =
        availableWidth >= 620 &&
        availableHeight >= 300;

    // Reserve a real right-hand strip in every normal settings page.
    // The logo therefore never floats on top of sliders, labels or buttons.
    for (int index = 0;
         index < tabs_->count();
         ++index)
    {
        if (index == aboutTabIndex_)
        {
            continue;
        }

        QWidget* page =
            tabs_->widget(index);

        if (page == nullptr ||
            page->layout() == nullptr)
        {
            continue;
        }

        QLayout* pageLayout =
            page->layout();

        if (!pageLayout->property(
                "OpenScopeOriginalRightMargin").isValid())
        {
            pageLayout->setProperty(
                "OpenScopeOriginalRightMargin",
                pageLayout->contentsMargins().right());
        }

        const int originalRightMargin =
            pageLayout->property(
                "OpenScopeOriginalRightMargin").toInt();

        const int reserveWidth =
            enoughRoom
                ? std::clamp(
                      std::min(
                          availableWidth / 7,
                          availableHeight / 4),
                      72,
                      128) + 34
                : originalRightMargin;

        QMargins margins =
            pageLayout->contentsMargins();

        margins.setRight(
            reserveWidth);

        pageLayout->setContentsMargins(
            margins);
    }

    if (tabs_->currentIndex() == aboutTabIndex_ ||
        !enoughRoom)
    {
        cornerLogoLabel_->hide();
        return;
    }

    const int target =
        std::clamp(
            std::min(
                availableWidth / 7,
                availableHeight / 4),
            72,
            128);

    QPixmap logoPixmap(
        QStringLiteral(":/branding/OpenScopeAboutLogo.png"));

    if (logoPixmap.isNull())
    {
        cornerLogoLabel_->hide();
        return;
    }

    cornerLogoLabel_->setFixedSize(
        target,
        target);

    cornerLogoLabel_->setPixmap(
        logoPixmap.scaled(
            target,
            target,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));

    cornerLogoLabel_->move(
        availableWidth - target - 18,
        48);

    cornerLogoLabel_->show();
    cornerLogoLabel_->raise();
}

void ControlWidget::setViewFps(
    double videoOpenScopeFps,
    double videoSpoutFps,
    double waveformOpenScopeFps,
    double waveformSpoutFps,
    double vectorscopeOpenScopeFps,
    double vectorscopeSpoutFps)
{
    const auto setFps =
        [](QLabel* label, double fps)
        {
            if (label == nullptr)
            {
                return;
            }

            label->setText(
                QString::number(
                    (std::max)(0.0, fps),
                    'f',
                    1));
        };

    setFps(videoOpenScopeFpsLabel_, videoOpenScopeFps);
    setFps(videoSpoutFpsLabel_, videoSpoutFps);
    setFps(waveformOpenScopeFpsLabel_, waveformOpenScopeFps);
    setFps(waveformSpoutFpsLabel_, waveformSpoutFps);
    setFps(vectorscopeOpenScopeFpsLabel_, vectorscopeOpenScopeFps);
    setFps(vectorscopeSpoutFpsLabel_, vectorscopeSpoutFps);
}


void ControlWidget::setHelpTabVisible(
    bool visible)
{
    if (tabs_ == nullptr ||
        helpTabIndex_ < 0)
    {
        return;
    }

    if (!visible &&
        tabs_->currentIndex() == helpTabIndex_)
    {
        tabs_->setCurrentIndex(
            2);
    }

    tabs_->setTabVisible(
        helpTabIndex_,
        visible);
}


void ControlWidget::setLineNumber(
    int lineNumber)
{
    lineSelector_->setValue(
        std::clamp(
            lineNumber,
            lineSelector_->minimum(),
            lineSelector_->maximum()));
}

void ControlWidget::setWaveformZoomFactor(
    int zoomFactor)
{
    if (waveformZoomButtonGroup_ == nullptr)
    {
        return;
    }

    QAbstractButton* button =
        waveformZoomButtonGroup_->button(
            zoomFactor);

    if (button == nullptr ||
        button->isChecked())
    {
        return;
    }

    button->setChecked(
        true);

    emit waveformZoomChanged(
        zoomFactor);
}

void ControlWidget::setPerformanceVisible(
    bool visible)
{
    if (performanceCheckBox_ == nullptr)
    {
        return;
    }

    const QSignalBlocker blocker(
        performanceCheckBox_);

    performanceCheckBox_->setChecked(
        visible);
}

void ControlWidget::setAspectRatio(
    OpenScopeSettings::AspectRatio aspectRatio)
{
    if (legacyAspectRatioCheckBox_ == nullptr)
    {
        return;
    }

    const QSignalBlocker blocker(
        legacyAspectRatioCheckBox_);

    legacyAspectRatioCheckBox_->setChecked(
        aspectRatio ==
        OpenScopeSettings::AspectRatio::Ratio4x3);
}

void ControlWidget::setCompositeInputGainState(
    bool lumaAvailable,
    bool chromaAvailable,
    int minimumHundredthsDb,
    int maximumHundredthsDb,
    int lumaHundredthsDb,
    int chromaHundredthsDb)
{
    if (compositeLumaGainSlider_ == nullptr ||
        compositeChromaGainSlider_ == nullptr ||
        compositeGainStatusLabel_ == nullptr)
    {
        return;
    }

    if (maximumHundredthsDb < minimumHundredthsDb)
    {
        std::swap(
            minimumHundredthsDb,
            maximumHundredthsDb);
    }

    {
        const QSignalBlocker lumaBlocker(
            compositeLumaGainSlider_);

        // When switching away from DeckLink (for example to the Philips
        // ROM source), keep the last hardware range and thumb position
        // visible and simply disable the control.  Collapsing the range to
        // 0..0 made both sliders jump to the far left, which falsely looked
        // like the calibration had changed.
        if (lumaAvailable)
        {
            compositeLumaGainSlider_->setRange(
                minimumHundredthsDb,
                maximumHundredthsDb);

            compositeLumaGainSlider_->setValue(
                std::clamp(
                    lumaHundredthsDb,
                    minimumHundredthsDb,
                    maximumHundredthsDb));
        }

        compositeLumaGainSlider_->setEnabled(
            lumaAvailable);
    }

    {
        const QSignalBlocker chromaBlocker(
            compositeChromaGainSlider_);

        if (chromaAvailable)
        {
            compositeChromaGainSlider_->setRange(
                minimumHundredthsDb,
                maximumHundredthsDb);

            compositeChromaGainSlider_->setValue(
                std::clamp(
                    chromaHundredthsDb,
                    minimumHundredthsDb,
                    maximumHundredthsDb));
        }

        compositeChromaGainSlider_->setEnabled(
            chromaAvailable);
    }

    if (lumaAvailable || chromaAvailable)
    {
        compositeGainStatusLabel_->setText(
            QStringLiteral(
                "DeckLink hardware control  range 0.00x to 2.00x"));
    }
    else
    {
        const bool hasRememberedDeckLinkRange =
            compositeLumaGainSlider_->maximum() >
                compositeLumaGainSlider_->minimum() ||
            compositeChromaGainSlider_->maximum() >
                compositeChromaGainSlider_->minimum();

        compositeGainStatusLabel_->setText(
            hasRememberedDeckLinkRange
            ? QStringLiteral(
                "Blackmagic composite gain disabled for current source")
            : QStringLiteral(
                "Composite gain control not available on this DeckLink device"));
    }
}

