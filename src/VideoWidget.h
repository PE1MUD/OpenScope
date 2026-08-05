#pragma once

#include <QImage>
#include <QWidget>

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setImage(const QImage& image);

protected:
    void paintEvent(QPaintEvent* event) override;
    const QImage& image() const;

private:
    QImage image_;
};