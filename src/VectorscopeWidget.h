#pragma once

#include <QImage>
#include <QWidget>

class VectorscopeWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit VectorscopeWidget(QWidget* parent = nullptr);

public slots:
    void setImage(const QImage& image);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
};