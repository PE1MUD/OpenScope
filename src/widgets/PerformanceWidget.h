#pragma once

#include <QWidget>

#include "util/PerformanceStats.h"

class QCloseEvent;

class PerformanceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PerformanceWidget(
        QWidget* parent = nullptr);

    void setPerformanceSnapshot(
        const PerformanceSnapshot& snapshot);
    QSize sizeHint() const override;

signals:
    void visibilityChanged(
        bool visible);

protected:
    void paintEvent(
        QPaintEvent* event) override;
    void mouseMoveEvent(
        QMouseEvent* event) override;
    void closeEvent(
        QCloseEvent* event) override;

private:
    PerformanceSnapshot snapshot_;
};