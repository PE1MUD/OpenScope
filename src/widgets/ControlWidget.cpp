#include "ControlWidget.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

ControlWidget::ControlWidget(
    bool vintageLook,
    int chromaRenderIntensity,
    QWidget* parent)
    : QWidget(parent)
{
    auto* layout =
        new QVBoxLayout(this);

    layout->setContentsMargins(
        20,
        20,
        20,
        20);

    layout->setSpacing(
        12);

    auto* title =
        new QLabel(
            "Waveform",
            this);

    auto* vintageCheckBox =
        new QCheckBox(
            "Vintage look",
            this);

    vintageCheckBox->setChecked(
        vintageLook);

    connect(
        vintageCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::vintageLookChanged);

    auto* intensityRow =
        new QWidget(this);

    auto* intensityLayout =
        new QHBoxLayout(intensityRow);

    intensityLayout->setContentsMargins(
        0,
        0,
        0,
        0);

    intensityLayout->setSpacing(
        12);

    auto* intensityLabel =
        new QLabel(
            "Color carrier intensity",
            intensityRow);

    auto* intensitySlider =
        new QSlider(
            Qt::Horizontal,
            intensityRow);

    intensitySlider->setRange(
        0,
        200);

    intensitySlider->setValue(
        chromaRenderIntensity);

    intensityLayout->addWidget(
        intensityLabel);

    intensityLayout->addWidget(
        intensitySlider,
        1);

    connect(
        intensitySlider,
        &QSlider::valueChanged,
        this,
        &ControlWidget::chromaRenderIntensityChanged);

    layout->addWidget(
        title);

    layout->addSpacing(
        4);

    layout->addWidget(
        vintageCheckBox);

    layout->addSpacing(
        4);

    layout->addWidget(
        intensityRow);

    performanceCheckBox_ =
        new QCheckBox(
            "Show performance",
            this);

    performanceCheckBox_->setChecked(
        true);

    connect(
        performanceCheckBox_,
        &QCheckBox::toggled,
        this,
        &ControlWidget::performanceVisibilityChanged);

    layout->addWidget(
        performanceCheckBox_);

    auto* noiseReductionCheckBox =
        new QCheckBox(
            "Enable noise reduction",
            this);

    noiseReductionCheckBox->setChecked(
        false);

    connect(
        noiseReductionCheckBox,
        &QCheckBox::toggled,
        this,
        &ControlWidget::noiseReductionChanged);

    layout->addWidget(
        noiseReductionCheckBox);

    layout->addStretch();
}

void ControlWidget::setPerformanceVisible(
    bool visible)
{
    performanceCheckBox_->setChecked(
        visible);
}