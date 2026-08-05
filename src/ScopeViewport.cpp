#include "ScopeViewport.h"

#include <QMouseEvent>
#include <QVBoxLayout>

ScopeViewport::ScopeViewport(
    QWidget* contentWidget,
    QWidget* parent)
    : QWidget(parent)
    , contentWidget_(contentWidget)
{
    auto* layout = new QVBoxLayout(this);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (contentWidget_) {
        contentWidget_->setParent(this);
        layout->addWidget(contentWidget_);
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