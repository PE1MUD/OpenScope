#pragma once

#include <QWidget>

class QGridLayout;
class ScopeViewport;

class ScopeWorkspace final : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeWorkspace(
        QWidget* videoWidget,
        QWidget* waveformWidget,
        QWidget* parent = nullptr);

private:
    QGridLayout* layout_ = nullptr;

    ScopeViewport* videoViewport_ = nullptr;
    ScopeViewport* waveformViewport_ = nullptr;
    ScopeViewport* vectorscopeViewport_ = nullptr;
    ScopeViewport* yuvViewport_ = nullptr;
};