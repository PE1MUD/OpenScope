#pragma once

#include <QWidget>
#include <QImage>

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QImage image_;
};