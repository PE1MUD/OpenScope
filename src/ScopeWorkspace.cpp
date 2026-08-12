#include "ScopeWorkspace.h"

#include "ScopeViewport.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace
{

    QWidget* createPlaceholder(const QString& text)
    {
        auto* frame =
            new QFrame;

        frame->setFrameShape(
            QFrame::StyledPanel);

        auto* layout =
            new QVBoxLayout(frame);

        layout->setContentsMargins(
            0,
            0,
            0,
            0);

        auto* label =
            new QLabel(
                text,
                frame);

        label->setAlignment(
            Qt::AlignCenter);

        layout->addWidget(label);

        return frame;
    }

}

ScopeWorkspace::ScopeWorkspace(
    QWidget* videoWidget,
    QWidget* waveformWidget,
    QWidget* vectorscopeWidget,
    QWidget* parent)
    : QWidget(parent)
{
    layout_ =
        new QGridLayout(this);

    setMinimumSize(
        768,
        576);

    layout_->setContentsMargins(
        0,
        0,
        0,
        0);

    layout_->setSpacing(4);

    videoViewport_ =
        new ScopeViewport(
            videoWidget,
            this);

    waveformViewport_ =
        new ScopeViewport(
            waveformWidget,
            this);

    vectorscopeViewport_ =
        new ScopeViewport(
            vectorscopeWidget,
            this);

    auto* controls =
        new QWidget(this);

    auto* controlsLayout =
        new QVBoxLayout(controls);

    controlsLayout->setContentsMargins(
        20,
        20,
        20,
        20);

    controlsLayout->setSpacing(12);

    auto* title =
        new QLabel(
            "Waveform",
            controls);

    auto* vintageCheckBox =
        new QCheckBox(
            "Vintage look",
            controls);

    vintageCheckBox->setChecked(true);

    connect(
        vintageCheckBox,
        &QCheckBox::toggled,
        this,
        [this](bool enabled)
        {
            emit waveformColorChanged(!enabled);
        });

    auto* intensityRow =
        new QWidget(controls);

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
        96);

    intensityLayout->addWidget(
        intensityLabel);

    intensityLayout->addWidget(
        intensitySlider,
        1);

    connect(
        intensitySlider,
        &QSlider::valueChanged,
        this,
        &ScopeWorkspace::waveformChromaFillIntensityChanged);

    controlsLayout->addWidget(
        title);

    controlsLayout->addSpacing(
        4);

    controlsLayout->addWidget(
        vintageCheckBox);

    controlsLayout->addSpacing(
        4);

    controlsLayout->addWidget(
        intensityRow);

    controlsLayout->addStretch();

    yuvViewport_ =
        new ScopeViewport(
            controls,
            this);

    layout_->addWidget(
        videoViewport_,
        0,
        0);

    layout_->addWidget(
        waveformViewport_,
        0,
        1);

    layout_->addWidget(
        vectorscopeViewport_,
        1,
        0);

    layout_->addWidget(
        yuvViewport_,
        1,
        1);

    layout_->setRowStretch(
        0,
        1);

    layout_->setRowStretch(
        1,
        1);

    layout_->setColumnStretch(
        0,
        1);

    layout_->setColumnStretch(
        1,
        1);

    connect(
        videoViewport_,
        &ScopeViewport::doubleClicked,
        this,
        &ScopeWorkspace::toggleMaximized);

    connect(
        waveformViewport_,
        &ScopeViewport::doubleClicked,
        this,
        &ScopeWorkspace::toggleMaximized);

    connect(
        vectorscopeViewport_,
        &ScopeViewport::doubleClicked,
        this,
        &ScopeWorkspace::toggleMaximized);

    connect(
        yuvViewport_,
        &ScopeViewport::doubleClicked,
        this,
        &ScopeWorkspace::toggleMaximized);
}

void ScopeWorkspace::toggleMaximized(
    ScopeViewport* viewport)
{
    if (maximizedViewport_ == viewport)
    {
        showGrid();
    }
    else
    {
        showMaximized(
            viewport);
    }
}

void ScopeWorkspace::showMaximized(
    ScopeViewport* viewport)
{
    videoViewport_->hide();
    waveformViewport_->hide();
    vectorscopeViewport_->hide();
    yuvViewport_->hide();

    layout_->removeWidget(
        viewport);

    viewport->show();

    layout_->addWidget(
        viewport,
        0,
        0,
        2,
        2);

    maximizedViewport_ =
        viewport;
}

void ScopeWorkspace::showGrid()
{
    layout_->removeWidget(
        videoViewport_);

    layout_->removeWidget(
        waveformViewport_);

    layout_->removeWidget(
        vectorscopeViewport_);

    layout_->removeWidget(
        yuvViewport_);

    layout_->addWidget(
        videoViewport_,
        0,
        0);

    layout_->addWidget(
        waveformViewport_,
        0,
        1);

    layout_->addWidget(
        vectorscopeViewport_,
        1,
        0);

    layout_->addWidget(
        yuvViewport_,
        1,
        1);

    videoViewport_->show();
    waveformViewport_->show();
    vectorscopeViewport_->show();
    yuvViewport_->show();

    maximizedViewport_ =
        nullptr;
}