#pragma once

#include "rendering/VectorscopeGraticule.h"
#include "settings/OpenScopeSettings.h"

#include <QImage>
#include <QSize>
#include <QWidget>

class QPaintEvent;
class QResizeEvent;

class VectorscopeWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit VectorscopeWidget(QWidget* parent = nullptr);

    void setGraticuleLineWidth(double width);

    void setAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

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
    QSize fitAspectSize() const;
    void emitRenderSize();

    QImage image_;
    VectorscopeGraticule graticule_;

    OpenScopeSettings::AspectRatio aspectRatio_ =
        OpenScopeSettings::AspectRatio::Ratio16x9;
};
