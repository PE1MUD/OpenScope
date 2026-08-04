#pragma once

#include <QObject>
#include <QImage>
#include <atomic>
#include "video/DisplayConverter.h"
#include "video/Yuv444Frame.h"
#include "video/FrameBufferPool.h"

class VideoEngine : public QObject
{
    Q_OBJECT

public:
    explicit VideoEngine(QObject* parent = nullptr);

    void submitFrame(const Yuv444Frame& frame); 
    void setFrame(const QImage& frame);
    void setFrame(const Yuv444Frame& frame);
    Yuv444Frame* tryAcquireWriteFrame();
    void submitWriteFrame();
    void cancelWriteFrame();

    const QImage& currentFrame() const;

signals:
    void frameChanged(const QImage& frame);

private:
    QImage currentFrame_;
    DisplayConverter displayConverter_;
    FrameBufferPool frameBufferPool_{ 720, 576 };
    std::atomic_bool framePending_{ false };
};