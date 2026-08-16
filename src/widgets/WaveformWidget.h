#pragma once

#include "widgets/VideoWidget.h"

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class WaveformWidget final : public VideoWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    bool isZoomed() const;
    int zoomFactor() const;

    void setZoomed(bool zoomed);
    void setZoomFactor(int factor);
    void setZoomEnabled(bool enabled);
    void setScrollPosition(double position);
    void setMeasurementLuma(const QVector<float>& samples);

signals:
    void zoomChanged(bool zoomed);
    void zoomFactorChanged(int factor);
    void scrollPositionChanged(double position);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct AreaAnalysisResult
    {
        bool attempted = false;
        bool valid = false;
        QString message;
        QRectF selectionRect;
        QPointF periodStart;
        QPointF periodEnd;
        QPointF vppTop;
        QPointF vppBottom;
        double lowVolts = 0.0;
        double highVolts = 0.0;
        double frequencyMHz = 0.0;
        int vppMillivolts = 0;
    };

    struct ReferenceAnalysisResult
    {
        bool valid = false;
        QString message;
        QRectF selectionRect;
        double lowVolts = 0.0;
        double highVolts = 0.0;
        int vppMillivolts = 0;
        double vppVolts = 0.0;
    };

    struct TemporalAreaMeasurement
    {
        bool active = false;
        QRectF selectionRect;
        int framesSeen = 0;
        int validFrames = 0;
        double sumFrequencyMHz = 0.0;
        double sumVppVolts = 0.0;
        double sumLowVolts = 0.0;
        double sumHighVolts = 0.0;
        double sumVppTopY = 0.0;
        double sumVppBottomY = 0.0;
        AreaAnalysisResult representative;
    };

    struct TemporalReferenceMeasurement
    {
        bool active = false;
        QRectF selectionRect;
        int framesSeen = 0;
        int validFrames = 0;
        double sumVppVolts = 0.0;
        double sumLowVolts = 0.0;
        double sumHighVolts = 0.0;
        ReferenceAnalysisResult representative;
    };

    QRect imageRect() const;
    QRectF scopeRect(const QRect& displayRect) const;
    void updateHover(const QPointF& position);
    void updateInteractionCursor();
    void clearAreaAnalysis();
    void clearMeasurements();
    void processTemporalMeasurements();
    AreaAnalysisResult analyzeSelection(const QRectF& selectionRect) const;
    ReferenceAnalysisResult analyzeReferenceSelection(const QRectF& selectionRect) const;
    int referenceLevelHit(const QPointF& position) const;
    int areaLevelHit(const QPointF& position) const;
    double displayYToVolts(double displayY) const;
    double voltsToDisplayY(double volts) const;

    int zoomFactor_ = 1;
    bool zoomEnabled_ = true;
    double scrollPosition_ = 0.0;

    bool panActive_ = false;
    double panStartX_ = 0.0;
    double panStartScrollPosition_ = 0.0;

    bool hoverActive_ = false;
    QPointF hoverPosition_;

    bool measureActive_ = false;
    QPointF measureStartPosition_;
    QPointF measureCurrentPosition_;

    bool areaMode_ = false;
    bool areaModeLabelMuted_ = false;
    bool areaSelecting_ = false;
    QPointF areaStartPosition_;
    QPointF areaCurrentPosition_;
    AreaAnalysisResult areaAnalysis_;

    bool referenceMode_ = false;
    bool referenceModeLabelMuted_ = false;
    bool referenceSelecting_ = false;
    QPointF referenceStartPosition_;
    QPointF referenceCurrentPosition_;
    ReferenceAnalysisResult referenceAnalysis_;
    bool referenceLevelDragging_ = false;
    int referenceLevelDragIndex_ = -1; // 0 = LOW, 1 = HIGH
    bool areaLevelDragging_ = false;
    int areaLevelDragIndex_ = -1; // 0 = LOW, 1 = HIGH

    QVector<float> measurementLuma_;
    TemporalAreaMeasurement temporalArea_;
    TemporalReferenceMeasurement temporalReference_;
};
