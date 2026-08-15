#pragma once

#include <QImage>
#include <QWidget>
#include "settings/OpenScopeSettings.h"

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void setAspectRatio(
        OpenScopeSettings::AspectRatio aspectRatio);

signals:
    void outputSizeChanged(
        int width,
        int height);

protected:
    const QImage& image() const;

    void paintEvent(
        QPaintEvent* event) override;

    void resizeEvent(
        QResizeEvent* event) override;

private:
    QImage image_;
    OpenScopeSettings::AspectRatio aspectRatio_ =
        OpenScopeSettings::AspectRatio::Ratio4x3;
};