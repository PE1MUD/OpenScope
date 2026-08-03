#include "VideoEngine.h"

VideoEngine::VideoEngine(QObject* parent)
    : QObject(parent)
{}

void VideoEngine::setFrame(const QImage& frame)
{
    currentFrame_ = frame;
    emit frameChanged(currentFrame_);
}

const QImage& VideoEngine::currentFrame() const
{
    return currentFrame_;
}