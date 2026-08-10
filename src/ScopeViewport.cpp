#include "ScopeViewport.h"

#include <QMouseEvent>
#include <QVBoxLayout>
#include <QSizePolicy>
#include <QPaintEvent>
#include <QPainter>

ScopeViewport::ScopeViewport(
    QWidget* contentWidget,
    QWidget* parent)
    : QWidget(parent)
    , contentWidget_(contentWidget)
{
    setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Ignored);

    setMinimumSize(
        0,
        0);

    auto* layout =
        new QVBoxLayout(this);

    setAutoFillBackground(true);

    QPalette palette =
        this->palette();

    palette.setColor(
        QPalette::Window,
        Qt::red);

    setPalette(
        palette);

    layout->setContentsMargins(2, 2, 2, 2);

    layout->setSpacing(0);

    if (contentWidget_)
    {
        contentWidget_->setParent(this);

        contentWidget_->setSizePolicy(
            QSizePolicy::Ignored,
            QSizePolicy::Ignored);

        contentWidget_->setMinimumSize(
            0,
            0);

        layout->addWidget(
            contentWidget_);
    }
}

QWidget* ScopeViewport::contentWidget() const
{
    return contentWidget_;
}

void ScopeViewport::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(this);
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void ScopeViewport::paintEvent(
    QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);

    painter.setPen(
        QPen(
            Qt::red,
            2));

    painter.drawRect(
        rect().adjusted(
            1,
            1,
            -2,
            -2));
}