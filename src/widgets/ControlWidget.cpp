#include "widgets/ControlWidget.h"

#include <algorithm>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
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
                26);
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

        void paintEvent(
            QPaintEvent* event) override
        {
            QSlider::paintEvent(
                event);

            QStyleOptionSlider option;
            initStyleOption(
                &option);

            QRect handleRect =
                style()->subControlRect(
                    QStyle::CC_Slider,
                    &option,
                    QStyle::SC_SliderHandle,
                    this);

            constexpr int kValueWidth = 38;

            handleRect.setWidth(
                kValueWidth);

            handleRect.moveCenter(
                style()->subControlRect(
                    QStyle::CC_Slider,
                    &option,
                    QStyle::SC_SliderHandle,
                    this).center());

            QPainter painter(
                this);

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
                QString::number(
                    value()));
        }

    private:
        void setValueFromMouse(
            const QPointF& position)
        {
            constexpr int kValueWidth = 38;

            const int span =
                (std::max)(
                    width() - kValueWidth,
                    1);

            const int pixelPosition =
                std::clamp(
                    static_cast<int>(
                        std::lround(
                            position.x() -
                            kValueWidth * 0.5)),
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
        QWidget* parent)
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

        layout->setSpacing(12);

        auto* label =
            new QLabel(
                labelText,
                row);

        slider =
            new ValueSlider(
                Qt::Horizontal,
                row);

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
        8,
        8,
        8,
        8);

    outerLayout->setSpacing(0);

    auto* tabs =
        new QTabWidget(this);

    outerLayout->addWidget(tabs);

    // ------------------------------------------------------------
    // Display
    // ------------------------------------------------------------
    auto* displayTab =
        new QWidget(tabs);

    auto* displayLayout =
        new QVBoxLayout(displayTab);

    displayLayout->setContentsMargins(
        16,
        16,
        16,
        16);

    displayLayout->setSpacing(12);

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
            settings.control
                .processing
                .noiseFilter
                .strength,
            displayTab);

    noiseReductionSlider->setEnabled(
        noiseReductionCheckBox->isChecked());

    displayLayout->addWidget(
        noiseReductionRow);

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

    tabs->addTab(
        displayTab,
        "Display");

    // ------------------------------------------------------------
    // Instrument
    // ------------------------------------------------------------
    auto* instrumentTab =
        new QWidget(tabs);

    auto* instrumentLayout =
        new QVBoxLayout(instrumentTab);

    instrumentLayout->setContentsMargins(
        16,
        16,
        16,
        16);

    instrumentLayout->setSpacing(12);

    auto* lineRow =
        new QWidget(instrumentTab);

    auto* lineLayout =
        new QHBoxLayout(lineRow);

    lineLayout->setContentsMargins(
        0,
        0,
        0,
        0);

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

    lineSelector_->setValue(
        settings.control
            .instrument
            .lineNumber);

    lineLayout->addWidget(
        lineSelector_,
        1);

    instrumentLayout->addWidget(
        lineRow);

    auto* zoomRow =
        new QWidget(instrumentTab);

    auto* zoomLayout =
        new QHBoxLayout(zoomRow);

    zoomLayout->setContentsMargins(
        0,
        0,
        0,
        0);

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

    QSlider* persistenceSlider = nullptr;

    QWidget* persistenceRow =
        createSliderRow(
            "Scopephor",
            persistenceSlider,
            0,
            255,
            settings.control
                .instrument
                .waveform
                .persistenceFrames,
            instrumentTab);

    instrumentLayout->addWidget(
        persistenceRow);

    auto* vintageCheckBox =
        new QCheckBox(
            "Vintage look",
            instrumentTab);

    vintageCheckBox->setChecked(
        settings.control
            .instrument
            .waveform
            .vintageLook);

    instrumentLayout->addWidget(
        vintageCheckBox);

    QSlider* chromaIntensitySlider = nullptr;

    QWidget* chromaIntensityRow =
        createSliderRow(
            "Color carrier intensity",
            chromaIntensitySlider,
            0,
            200,
            settings.control
                .instrument
                .waveform
                .chromaRenderIntensity,
            instrumentTab);

    instrumentLayout->addWidget(
        chromaIntensityRow);

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
        &ControlWidget::waveformPersistenceChanged);

    connect(
        vintageCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::vintageLookChanged);

    connect(
        chromaIntensitySlider,
        &QSlider::valueChanged,
        this,
        &ControlWidget::chromaRenderIntensityChanged);

    tabs->addTab(
        instrumentTab,
        "Instrument");

    // ------------------------------------------------------------
    // Misc
    // ------------------------------------------------------------
    auto* miscTab =
        new QWidget(tabs);

    auto* miscLayout =
        new QVBoxLayout(miscTab);

    miscLayout->setContentsMargins(
        16,
        16,
        16,
        16);

    miscLayout->setSpacing(12);

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
        true);

    miscLayout->addWidget(
        performanceCheckBox_);

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

    tabs->addTab(
        miscTab,
        "Misc");
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
