#pragma once

#include "VideoWidget.h"

class QPaintEvent;

class WaveformWidget final : public VideoWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};