#pragma once

#include <QWidget>
#include <QImage>

void setImage(const QImage& image);

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setImage(const QImage& image);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
};