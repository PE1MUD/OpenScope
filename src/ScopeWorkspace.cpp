#include "ScopeWorkspace.h"

#include "ScopeViewport.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace
{

    QWidget* createPlaceholder(const QString& text)
    {
        auto* frame = new QFrame;

        frame->setFrameShape(QFrame::StyledPanel);

        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(0, 0, 0, 0);

        auto* label = new QLabel(text, frame);
        label->setAlignment(Qt::AlignCenter);

        layout->addWidget(label);

        return frame;
    }

}

ScopeWorkspace::ScopeWorkspace(
    QWidget* videoWidget,
    QWidget* waveformWidget,
    QWidget* parent)
    : QWidget(parent)
{
    layout_ = new QGridLayout(this);

    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(4);

    videoViewport_ =
        new ScopeViewport(videoWidget, this);

    waveformViewport_ =
        new ScopeViewport(waveformWidget, this);

    vectorscopeViewport_ =
        new ScopeViewport(
            createPlaceholder("Vectorscope"),
            this);

    yuvViewport_ =
        new ScopeViewport(
            createPlaceholder("Y / U / V"),
            this);

    layout_->addWidget(videoViewport_, 0, 0);
    layout_->addWidget(waveformViewport_, 0, 1);
    layout_->addWidget(vectorscopeViewport_, 1, 0);
    layout_->addWidget(yuvViewport_, 1, 1);

    layout_->setRowStretch(0, 1);
    layout_->setRowStretch(1, 1);

    layout_->setColumnStretch(0, 1);
    layout_->setColumnStretch(1, 1);
}