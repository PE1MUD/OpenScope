#pragma once

#include "settings/OpenScopeSettings.h"

#include <QImage>
#include <QSize>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

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

    void imageClicked(
        double normalizedX,
        double normalizedY);

    void leftInteractionStarted();
    void doubleClickRestoreRequested();

    void rightClicked();

protected:
    const QImage& image() const;

    QSize fitAspectSize(
        int availableWidth,
        int availableHeight) const;

    void emitOutputSize();

    void mousePressEvent(
        QMouseEvent* event) override;

    void mouseMoveEvent(
        QMouseEvent* event) override;

    void mouseDoubleClickEvent(
        QMouseEvent* event) override;

    void paintEvent(
        QPaintEvent* event) override;

    void resizeEvent(
        QResizeEvent* event) override;

private:
    bool emitImagePosition(
        const QPointF& position);

    QImage image_;

    OpenScopeSettings::AspectRatio aspectRatio_ =
        OpenScopeSettings::AspectRatio::Ratio16x9;
};
