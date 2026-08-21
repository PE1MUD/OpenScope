#include "rendering/VectorscopeRenderer.h"

#include "VectorscopeSettings.h"
#include "ui/ViewportOverlay.h"

#include <QPainter>
#include <QPointF>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace
{
    QRectF snapSquare(const QRectF& desired)
    {
        const int diameter =
            std::max(
                2,
                static_cast<int>(
                    std::floor(
                        std::min(
                            desired.width(),
                            desired.height()))));

        const QPointF center = desired.center();

        const double left =
            std::round(
                center.x() -
                static_cast<double>(diameter) * 0.5);

        const double top =
            std::round(
                center.y() -
                static_cast<double>(diameter) * 0.5);

        return QRectF(
            left,
            top,
            static_cast<double>(diameter),
            static_cast<double>(diameter));
    }
}

VectorscopeRenderer::VectorscopeRenderer(Profile profile)
    : profile_(profile)
{
    image_.fill(Qt::black);

    graticule_.setScale(
        VectorscopeSettings::scale);

    graticule_.setLineWidth(
        VectorscopeSettings::graticuleLineWidth);

    // Preserve the established screen geometry. The dedicated PAL/video
    // renderer keeps the PAL-specific scale that was already in use before
    // this refactor; the refactor changes ownership, not calibration.
    analyzer_.setScale(
        profile_ == Profile::Video
        ? VectorscopeSettings::scale * (36.0 / 35.0)
        : VectorscopeSettings::scale);
}

void VectorscopeRenderer::setOutputSize(int width, int height)
{
    outputWidth_ = std::max(width, 1);
    outputHeight_ = std::max(height, 1);

    if (image_.width() == outputWidth_ &&
        image_.height() == outputHeight_)
    {
        return;
    }

    image_ = QImage(
        outputWidth_,
        outputHeight_,
        QImage::Format_RGB32);

    image_.fill(Qt::black);
}

void VectorscopeRenderer::setContentScale(
    double horizontalScale,
    double verticalScale)
{
    contentScaleX_ = std::clamp(horizontalScale, 0.10, 1.0);
    contentScaleY_ = std::clamp(verticalScale, 0.10, 1.0);
}

void VectorscopeRenderer::setSelectedLine(int line)
{
    selectedLine_ = line;
    analyzer_.setSelectedLine(line);
}

void VectorscopeRenderer::setPersistence(int persistence)
{
    analyzer_.setPersistence(persistence);
}

void VectorscopeRenderer::setGlow(int glow)
{
    analyzer_.setGlow(glow);
}

void VectorscopeRenderer::setHorizontalWindow(
    int zoomFactor,
    double scrollPosition)
{
    analyzer_.setHorizontalWindow(
        zoomFactor,
        scrollPosition);
}

void VectorscopeRenderer::setPresentationInfo(
    const VectorscopePresentationInfo& info)
{
    presentation_ = info;
}

void VectorscopeRenderer::moveAnalyzerToThread(QThread* thread)
{
    analyzer_.moveToThread(thread);
}

const QImage& VectorscopeRenderer::image() const
{
    return image_;
}

QRectF VectorscopeRenderer::contentRect() const
{
    if (profile_ == Profile::Screen)
    {
        return QRectF(
            0.0,
            0.0,
            static_cast<double>(outputWidth_),
            static_cast<double>(outputHeight_));
    }

    return QRectF(
        (1.0 - contentScaleX_) * static_cast<double>(outputWidth_) * 0.5,
        (1.0 - contentScaleY_) * static_cast<double>(outputHeight_) * 0.5,
        contentScaleX_ * static_cast<double>(outputWidth_),
        contentScaleY_ * static_cast<double>(outputHeight_));
}

QRectF VectorscopeRenderer::screenScopeRect(const QRectF& bounds) const
{
    return snapSquare(
        ViewportOverlay::vectorscopeScopeRect(
            bounds,
            false,
            0.0));
}

QRectF VectorscopeRenderer::videoScopeRect(
    const QRectF& bounds,
    QRectF* topLeftCard,
    QRectF* topRightCard,
    QRectF* bottomLeftCard,
    QRectF* bottomRightCard) const
{
    const double cardGap =
        std::max(6.0, bounds.height() * 0.012);

    const QVector<ViewportOverlay::InfoRow> topLeftRows
    {
        { presentation_.source, QString() },
        { presentation_.input, QString() }
    };

    const QVector<ViewportOverlay::InfoRow> topRightRows
    {
        { presentation_.standard, QString() },
        { presentation_.matrix, QString() }
    };

    const QVector<ViewportOverlay::InfoRow> bottomLeftRows
    {
        {
            QStringLiteral("LINE"),
            selectedLine_ >= 0
            ? QString::number(selectedLine_)
            : QStringLiteral("ALL")
        }
    };

    const QVector<ViewportOverlay::InfoRow> bottomRightRows
    {
        { presentation_.processing, QString() }
    };

    const QSizeF topLeftSize =
        ViewportOverlay::infoCardRequiredSize(
            topLeftRows,
            bounds.height(),
            true);

    const QSizeF topRightSize =
        ViewportOverlay::infoCardRequiredSize(
            topRightRows,
            bounds.height(),
            true);

    const QSizeF bottomLeftSize =
        ViewportOverlay::infoCardRequiredSize(
            bottomLeftRows,
            bounds.height(),
            true);

    const QSizeF bottomRightSize =
        ViewportOverlay::infoCardRequiredSize(
            bottomRightRows,
            bounds.height(),
            true);

    *topLeftCard = QRectF(
        bounds.left(),
        bounds.top(),
        topLeftSize.width(),
        topLeftSize.height());

    *topRightCard = QRectF(
        bounds.right() - topRightSize.width(),
        bounds.top(),
        topRightSize.width(),
        topRightSize.height());

    *bottomLeftCard = QRectF(
        bounds.left(),
        bounds.bottom() - bottomLeftSize.height(),
        bottomLeftSize.width(),
        bottomLeftSize.height());

    *bottomRightCard = QRectF(
        bounds.right() - bottomRightSize.width(),
        bounds.bottom() - bottomRightSize.height(),
        bottomRightSize.width(),
        bottomRightSize.height());

    const QPointF center = bounds.center();

    auto distanceToCenter =
        [&center](const QPointF& point)
        {
            return std::hypot(
                point.x() - center.x(),
                point.y() - center.y());
        };

    double radius =
        std::min(bounds.width() * 0.5, bounds.height() * 0.5);

    radius = std::min(
        radius,
        distanceToCenter(
            QPointF(
                topLeftCard->right() + cardGap,
                topLeftCard->bottom() + cardGap)));

    radius = std::min(
        radius,
        distanceToCenter(
            QPointF(
                topRightCard->left() - cardGap,
                topRightCard->bottom() + cardGap)));

    radius = std::min(
        radius,
        distanceToCenter(
            QPointF(
                bottomLeftCard->right() + cardGap,
                bottomLeftCard->top() - cardGap)));

    radius = std::min(
        radius,
        distanceToCenter(
            QPointF(
                bottomRightCard->left() - cardGap,
                bottomRightCard->top() - cardGap)));

    radius = std::max(1.0, radius - 1.0);

    const int diameter =
        std::max(
            2,
            2 * static_cast<int>(std::floor(radius)));

    const double snappedRadius =
        static_cast<double>(diameter) * 0.5;

    return QRectF(
        std::round(center.x() - snappedRadius),
        std::round(center.y() - snappedRadius),
        static_cast<double>(diameter),
        static_cast<double>(diameter));
}

void VectorscopeRenderer::analyze(const Yuv444Frame& frame)
{
    const QRectF bounds = contentRect();

    QRectF topLeftCard;
    QRectF topRightCard;
    QRectF bottomLeftCard;
    QRectF bottomRightCard;

    const QRectF scopeRect =
        profile_ == Profile::Screen
        ? screenScopeRect(bounds)
        : videoScopeRect(
            bounds,
            &topLeftCard,
            &topRightCard,
            &bottomLeftCard,
            &bottomRightCard);

    const int scopeWidth =
        std::max(1, static_cast<int>(scopeRect.width()));

    const int scopeHeight =
        std::max(1, static_cast<int>(scopeRect.height()));

    analyzer_.setOutputSize(
        scopeWidth,
        scopeHeight);

    analyzer_.analyze(frame);

    image_.fill(Qt::black);

    QPainter painter(&image_);

    if (!analyzer_.image().isNull() &&
        analyzer_.image().width() == scopeWidth &&
        analyzer_.image().height() == scopeHeight)
    {
        painter.drawImage(
            scopeRect.topLeft(),
            analyzer_.image());
    }

    painter.setRenderHint(
        QPainter::Antialiasing,
        true);

    graticule_.draw(
        painter,
        scopeRect,
        profile_ == Profile::Video ? 1.8 : 1.0,
        profile_ == Profile::Video ? 1.35 : 1.0);

    if (profile_ == Profile::Screen)
    {
        composeScreen(painter, bounds, scopeRect);
    }
    else
    {
        composeVideo(
            painter,
            bounds,
            scopeRect,
            topLeftCard,
            topRightCard,
            bottomLeftCard,
            bottomRightCard);
    }
}

void VectorscopeRenderer::composeScreen(
    QPainter& painter,
    const QRectF& bounds,
    const QRectF& scopeRect)
{
    const double cardGap =
        std::max(8.0, bounds.height() * 0.016);

    const double ownerPadding =
        std::max(10.0, bounds.height() * 0.018);

    const QVector<ViewportOverlay::InfoRow> sourceRows
    {
        { QStringLiteral("SOURCE"), presentation_.source },
        { QStringLiteral("INPUT"), presentation_.input },
        { QStringLiteral("STANDARD"), presentation_.standard }
    };

    const QVector<ViewportOverlay::InfoRow> scopeRows
    {
        {
            QStringLiteral("LINE"),
            selectedLine_ >= 0
            ? QString::number(selectedLine_)
            : QStringLiteral("ALL")
        },
        { QStringLiteral("TARGETS"), presentation_.targets },
        { QStringLiteral("MATRIX"), presentation_.matrix }
    };

    const QVector<ViewportOverlay::InfoRow> processingRows
    {
        { QStringLiteral("PROCESSING"), QString() },
        { presentation_.processing, QString() }
    };

    const QSizeF sourceSize =
        ViewportOverlay::infoCardRequiredSize(
            sourceRows,
            bounds.height(),
            false);

    const QSizeF scopeSize =
        ViewportOverlay::infoCardRequiredSize(
            scopeRows,
            bounds.height(),
            false);

    const QSizeF processingSize =
        ViewportOverlay::infoCardRequiredSize(
            processingRows,
            bounds.height(),
            false);

    const double requiredCardWidth =
        std::max(
            sourceSize.width(),
            std::max(
                scopeSize.width(),
                processingSize.width()));

    const double measuredOwnerWidth =
        requiredCardWidth + 2.0 * ownerPadding;

    const QRectF infoRect =
        ViewportOverlay::vectorscopeInfoRect(
            bounds,
            false,
            0.0);

    const double ownerWidth =
        std::min(infoRect.width(), measuredOwnerWidth);

    const double innerWidth =
        std::max(1.0, ownerWidth - 2.0 * ownerPadding);

    const bool mainGroupsFitWidth =
        sourceSize.width() <= innerWidth &&
        scopeSize.width() <= innerWidth;

    const bool processingFitsWidth =
        processingSize.width() <= innerWidth;

    const double twoCardHeight =
        sourceSize.height() +
        cardGap +
        scopeSize.height();

    const double threeCardHeight =
        twoCardHeight +
        cardGap +
        processingSize.height();

    const bool processingFitsHeight =
        threeCardHeight + 2.0 * ownerPadding <= infoRect.height();

    const bool drawProcessing =
        processingFitsWidth &&
        processingFitsHeight;

    const double cardsHeight =
        drawProcessing
        ? threeCardHeight
        : twoCardHeight;

    const double ownerHeight =
        cardsHeight + 2.0 * ownerPadding;

    if (!mainGroupsFitWidth ||
        ownerHeight > infoRect.height())
    {
        return;
    }

    const QRectF ownerRect(
        infoRect.left(),
        infoRect.top(),
        ownerWidth,
        ownerHeight);

    ViewportOverlay::drawOwnerPanel(
        painter,
        ownerRect,
        false);

    double y = ownerRect.top() + ownerPadding;

    const QRectF first(
        ownerRect.left() + ownerPadding,
        y,
        innerWidth,
        sourceSize.height());

    ViewportOverlay::drawInfoCard(
        painter,
        first,
        sourceRows,
        bounds.height(),
        false);

    y += sourceSize.height() + cardGap;

    const QRectF second(
        ownerRect.left() + ownerPadding,
        y,
        innerWidth,
        scopeSize.height());

    ViewportOverlay::drawInfoCard(
        painter,
        second,
        scopeRows,
        bounds.height(),
        false);

    if (!drawProcessing)
    {
        return;
    }

    y += scopeSize.height() + cardGap;

    const QRectF third(
        ownerRect.left() + ownerPadding,
        y,
        innerWidth,
        processingSize.height());

    ViewportOverlay::drawInfoCard(
        painter,
        third,
        processingRows,
        bounds.height(),
        false);
}

void VectorscopeRenderer::composeVideo(
    QPainter& painter,
    const QRectF& bounds,
    const QRectF&,
    const QRectF& topLeftCard,
    const QRectF& topRightCard,
    const QRectF& bottomLeftCard,
    const QRectF& bottomRightCard)
{
    const QVector<ViewportOverlay::InfoRow> topLeftRows
    {
        { presentation_.source, QString() },
        { presentation_.input, QString() }
    };

    const QVector<ViewportOverlay::InfoRow> topRightRows
    {
        { presentation_.standard, QString() },
        { presentation_.matrix, QString() }
    };

    const QVector<ViewportOverlay::InfoRow> bottomLeftRows
    {
        {
            QStringLiteral("LINE"),
            selectedLine_ >= 0
            ? QString::number(selectedLine_)
            : QStringLiteral("ALL")
        }
    };

    const QVector<ViewportOverlay::InfoRow> bottomRightRows
    {
        { presentation_.processing, QString() }
    };

    ViewportOverlay::drawInfoCard(
        painter,
        topLeftCard,
        topLeftRows,
        bounds.height(),
        true);

    ViewportOverlay::drawInfoCard(
        painter,
        topRightCard,
        topRightRows,
        bounds.height(),
        true);

    ViewportOverlay::drawInfoCard(
        painter,
        bottomLeftCard,
        bottomLeftRows,
        bounds.height(),
        true);

    ViewportOverlay::drawInfoCard(
        painter,
        bottomRightCard,
        bottomRightRows,
        bounds.height(),
        true);
}
