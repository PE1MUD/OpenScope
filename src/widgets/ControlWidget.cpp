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
#include <QPushButton>
#include <QSignalBlocker>
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

    zoomLayout->addSpacing(
        32);

    auto* vintageCheckBox =
        new QCheckBox(
            "Vintage look",
            zoomRow);

    vintageCheckBox->setChecked(
        settings.control
            .instrument
            .waveform
            .vintageLook);

    zoomLayout->addWidget(
        vintageCheckBox);

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
            std::clamp(
                settings.control
                    .instrument
                    .waveform
                    .chromaRenderIntensity,
                0,
                200) /
                2,
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
        "Set Color carrier intensity, Scopephor and Beam Glow to 50");

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
            const int normalizedValue =
                std::clamp(
                    value,
                    0,
                    100);

            emit chromaRenderIntensityChanged(
                normalizedValue *
                2);
        });

    connect(
        defaultsButton,
        &QPushButton::clicked,
        this,
        [chromaIntensitySlider,
         persistenceSlider,
         vectorscopeGlowSlider,
         vintageCheckBox]()
        {
            constexpr int defaultValue = 50;

            chromaIntensitySlider->setValue(
                defaultValue);

            persistenceSlider->setValue(
                defaultValue);

            vectorscopeGlowSlider->setValue(
                defaultValue);

            vintageCheckBox->setChecked(
                false);
        });

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
Sets the strength of the chroma-envelope rendering.</p>

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
            1);
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
