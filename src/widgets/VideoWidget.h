#pragma once

#include "settings/OpenScopeSettings.h"

#include <QImage>
#include <QSize>
#include <QTimer>
#include <QWidget>

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void setInputSignalValid(bool valid);

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

    void zoomInRequested();
    void zoomOutRequested();
    void lineUpRequested();
    void lineDownRequested();
    void panLeftRequested();
    void panRightRequested();
    void multiburstRequested();
    void spectrumRequested();

protected:
    void focusOutEvent(
        QFocusEvent* event) override;

    void keyPressEvent(
        QKeyEvent* event) override;

    void keyReleaseEvent(
        QKeyEvent* event) override;

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

    void emitHeldArrowRequests();
    void stopArrowRepeatIfIdle();

    QImage image_;
    bool inputSignalValid_ = true;

    QTimer arrowRepeatTimer_;

    bool upHeld_ = false;
    bool downHeld_ = false;
    bool leftHeld_ = false;
    bool rightHeld_ = false;

    OpenScopeSettings::AspectRatio aspectRatio_ =
        OpenScopeSettings::AspectRatio::Ratio16x9;
};
