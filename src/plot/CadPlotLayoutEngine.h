#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <QVector>

namespace CadPlot {

enum class PaperUnit {
    Millimeter,
    Inch
};

enum class PlotOrientation {
    Portrait,
    Landscape
};

struct PaperSize {
    QString name;
    QSizeF sizeMillimeters;
};

struct ViewportDefinition {
    QString name;
    QRectF paperRect;
    QRectF modelViewRect;
    double customScale = 1.0;
    bool locked = false;
    bool visible = true;
    QJsonObject toJson() const;
    static ViewportDefinition fromJson(const QJsonObject& obj);
};

struct PlotLayout {
    QString name = QStringLiteral("Layout1");
    PaperSize paper;
    PlotOrientation orientation = PlotOrientation::Landscape;
    QRectF printableAreaMillimeters;
    QVector<ViewportDefinition> viewports;
    bool centerPlot = true;
    bool fitToPaper = false;
    double plotScale = 1.0;
    QJsonObject toJson() const;
    static PlotLayout fromJson(const QJsonObject& obj);
};

QVector<PaperSize> standardPaperSizes();
PaperSize paperSizeByName(const QString& name);
QSizeF orientedPaperSize(const PlotLayout& layout);
QRectF defaultPrintableArea(const PaperSize& paper, double marginMillimeters = 5.0);
QTransform modelToPaperTransform(const QRectF& modelRect, const QRectF& paperViewportRect, bool preserveAspect = true);
QTransform paperToModelTransform(const QRectF& modelRect, const QRectF& paperViewportRect, bool preserveAspect = true);
ViewportDefinition makeViewportForExtents(const QString& name, const QRectF& modelExtents, const QRectF& paperRect);
QString scaleToString(double scale);
double scaleFromString(const QString& text, bool* ok = nullptr);

} // namespace CadPlot
