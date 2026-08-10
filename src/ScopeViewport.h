#pragma once

#include <QWidget>

class QMouseEvent;

class ScopeViewport final : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeViewport(
        QWidget* contentWidget,
        QWidget* parent = nullptr);

    QWidget* contentWidget() const;

signals:
    void doubleClicked(ScopeViewport* viewport);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(
        QPaintEvent* event) override;

private:
    QWidget* contentWidget_ = nullptr;
};