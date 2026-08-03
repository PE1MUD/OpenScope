#pragma once

#include <QObject>
#include <QImage>

class VideoEngine : public QObject
{
    Q_OBJECT

public:
    explicit VideoEngine(QObject* parent = nullptr);

    void setFrame(const QImage& frame);
    const QImage& currentFrame() const;

signals:
    void frameChanged(const QImage& frame);

private:
    QImage currentFrame_;
};