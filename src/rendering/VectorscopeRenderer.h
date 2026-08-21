#pragma once

#include "analysis/Analyzer.h"
#include "analysis/VectorscopeAnalyzer.h"
#include "rendering/VectorscopeGraticule.h"

#include <QImage>
#include <QRectF>
#include <QString>
#include <QThread>

class QPainter;

struct VectorscopePresentationInfo
{
    QString source = QStringLiteral("BMD IP 4K");
    QString input = QStringLiteral("Composite");
    QString standard = QStringLiteral("PAL 625i");
    QString targets = QStringLiteral("100%");
    QString matrix = QStringLiteral("BT.601");
    QString processing = QStringLiteral("YUV 4:2:2 10 bit");
};

class VectorscopeRenderer final : public Analyzer
{
public:
    enum class Profile
    {
        Screen,
        Video
    };

    explicit VectorscopeRenderer(Profile profile);

    void analyze(const Yuv444Frame& frame) override;

    void setOutputSize(int width, int height);
    void setContentScale(double horizontalScale, double verticalScale);
    void setSelectedLine(int line);
    void setPersistence(int persistence);
    void setGlow(int glow);
    void setHorizontalWindow(int zoomFactor, double scrollPosition);
    void setPresentationInfo(const VectorscopePresentationInfo& info);

    void moveAnalyzerToThread(QThread* thread);

    [[nodiscard]] const QImage& image() const;

private:
    [[nodiscard]] QRectF contentRect() const;
    [[nodiscard]] QRectF screenScopeRect(const QRectF& bounds) const;
    [[nodiscard]] QRectF videoScopeRect(
        const QRectF& bounds,
        QRectF* topLeftCard,
        QRectF* topRightCard,
        QRectF* bottomLeftCard,
        QRectF* bottomRightCard) const;

    void composeScreen(
        QPainter& painter,
        const QRectF& bounds,
        const QRectF& scopeRect);
    void composeVideo(
        QPainter& painter,
        const QRectF& bounds,
        const QRectF& scopeRect,
        const QRectF& topLeftCard,
        const QRectF& topRightCard,
        const QRectF& bottomLeftCard,
        const QRectF& bottomRightCard);

    Profile profile_ = Profile::Screen;
    QImage image_{1, 1, QImage::Format_RGB32};
    VectorscopeAnalyzer analyzer_;
    VectorscopeGraticule graticule_;
    VectorscopePresentationInfo presentation_;

    int outputWidth_ = 1;
    int outputHeight_ = 1;
    double contentScaleX_ = 1.0;
    double contentScaleY_ = 1.0;
    int selectedLine_ = -1;
};
