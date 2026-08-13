#pragma once

#include "rendering/VectorscopeGraticule.h"

#include <QImage>
#include <QWidget>

class QPaintEvent;
class QResizeEvent;

class VectorscopeWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit VectorscopeWidget(QWidget* parent = nullptr);
    void setGraticuleLineWidth(double width);

public slots:
    void setImage(const QImage& image);

signals:
    void renderSizeChanged(
        int width,
        int height);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage image_;
    VectorscopeGraticule graticule_;
};