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

    void focusContent();

signals:
    void doubleClicked(ScopeViewport* viewport);

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QWidget* contentWidget_ = nullptr;
};