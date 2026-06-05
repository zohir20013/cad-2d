#include "plot/CadPlotLayoutEngine.h"

#include <QJsonArray>
#include <QtMath>
#include <QStringList>

namespace CadPlot {

QJsonObject ViewportDefinition::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["paperX"] = paperRect.x();
    obj["paperY"] = paperRect.y();
    obj["paperW"] = paperRect.width();
    obj["paperH"] = paperRect.height();
    obj["modelX"] = modelViewRect.x();
    obj["modelY"] = modelViewRect.y();
    obj["modelW"] = modelViewRect.width();
    obj["modelH"] = modelViewRect.height();
    obj["customScale"] = customScale;
    obj["locked"] = locked;
    obj["visible"] = visible;
    return obj;
}

ViewportDefinition ViewportDefinition::fromJson(const QJsonObject& obj)
{
    ViewportDefinition vp;
    vp.name = obj.value("name").toString(QStringLiteral("Viewport"));
    vp.paperRect = QRectF(obj.value("paperX").toDouble(), obj.value("paperY").toDouble(),
                          obj.value("paperW").toDouble(), obj.value("paperH").toDouble()).normalized();
    vp.modelViewRect = QRectF(obj.value("modelX").toDouble(), obj.value("modelY").toDouble(),
                              obj.value("modelW").toDouble(), obj.value("modelH").toDouble()).normalized();
    vp.customScale = qMax(1.0e-9, obj.value("customScale").toDouble(1.0));
    vp.locked = obj.value("locked").toBool(false);
    vp.visible = obj.value("visible").toBool(true);
    return vp;
}

QJsonObject PlotLayout::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["paperName"] = paper.name;
    obj["paperWidthMm"] = paper.sizeMillimeters.width();
    obj["paperHeightMm"] = paper.sizeMillimeters.height();
    obj["orientation"] = orientation == PlotOrientation::Landscape ? QStringLiteral("Landscape") : QStringLiteral("Portrait");
    obj["printableX"] = printableAreaMillimeters.x();
    obj["printableY"] = printableAreaMillimeters.y();
    obj["printableW"] = printableAreaMillimeters.width();
    obj["printableH"] = printableAreaMillimeters.height();
    obj["centerPlot"] = centerPlot;
    obj["fitToPaper"] = fitToPaper;
    obj["plotScale"] = plotScale;
    QJsonArray arr;
    for (const ViewportDefinition& vp : viewports) arr.append(vp.toJson());
    obj["viewports"] = arr;
    return obj;
}

PlotLayout PlotLayout::fromJson(const QJsonObject& obj)
{
    PlotLayout layout;
    layout.name = obj.value("name").toString(QStringLiteral("Layout1"));
    layout.paper.name = obj.value("paperName").toString(QStringLiteral("A4"));
    layout.paper.sizeMillimeters = QSizeF(obj.value("paperWidthMm").toDouble(210.0), obj.value("paperHeightMm").toDouble(297.0));
    layout.orientation = obj.value("orientation").toString(QStringLiteral("Landscape")) == QStringLiteral("Portrait")
        ? PlotOrientation::Portrait : PlotOrientation::Landscape;
    layout.printableAreaMillimeters = QRectF(obj.value("printableX").toDouble(5.0), obj.value("printableY").toDouble(5.0),
                                             obj.value("printableW").toDouble(200.0), obj.value("printableH").toDouble(287.0));
    layout.centerPlot = obj.value("centerPlot").toBool(true);
    layout.fitToPaper = obj.value("fitToPaper").toBool(false);
    layout.plotScale = qMax(1.0e-9, obj.value("plotScale").toDouble(1.0));
    const QJsonArray arr = obj.value("viewports").toArray();
    for (const QJsonValue& value : arr) layout.viewports.push_back(ViewportDefinition::fromJson(value.toObject()));
    return layout;
}

QVector<PaperSize> standardPaperSizes()
{
    return {
        { QStringLiteral("A0"), QSizeF(841.0, 1189.0) },
        { QStringLiteral("A1"), QSizeF(594.0, 841.0) },
        { QStringLiteral("A2"), QSizeF(420.0, 594.0) },
        { QStringLiteral("A3"), QSizeF(297.0, 420.0) },
        { QStringLiteral("A4"), QSizeF(210.0, 297.0) },
        { QStringLiteral("A5"), QSizeF(148.0, 210.0) },
        { QStringLiteral("Letter"), QSizeF(215.9, 279.4) },
        { QStringLiteral("Legal"), QSizeF(215.9, 355.6) },
        { QStringLiteral("Tabloid"), QSizeF(279.4, 431.8) },
        { QStringLiteral("ARCH A"), QSizeF(228.6, 304.8) },
        { QStringLiteral("ARCH B"), QSizeF(304.8, 457.2) },
        { QStringLiteral("ARCH C"), QSizeF(457.2, 609.6) },
        { QStringLiteral("ARCH D"), QSizeF(609.6, 914.4) },
        { QStringLiteral("ARCH E"), QSizeF(914.4, 1219.2) }
    };
}

PaperSize paperSizeByName(const QString& name)
{
    for (const PaperSize& size : standardPaperSizes()) {
        if (size.name.compare(name, Qt::CaseInsensitive) == 0) return size;
    }
    return { QStringLiteral("A4"), QSizeF(210.0, 297.0) };
}

QSizeF orientedPaperSize(const PlotLayout& layout)
{
    QSizeF size = layout.paper.sizeMillimeters;
    if (layout.orientation == PlotOrientation::Landscape && size.height() > size.width()) size.transpose();
    if (layout.orientation == PlotOrientation::Portrait && size.width() > size.height()) size.transpose();
    return size;
}

QRectF defaultPrintableArea(const PaperSize& paper, double marginMillimeters)
{
    const double margin = qMax(0.0, marginMillimeters);
    return QRectF(margin, margin,
                  qMax(0.0, paper.sizeMillimeters.width() - 2.0 * margin),
                  qMax(0.0, paper.sizeMillimeters.height() - 2.0 * margin));
}

QTransform modelToPaperTransform(const QRectF& modelRect, const QRectF& paperViewportRect, bool preserveAspect)
{
    if (modelRect.isNull() || paperViewportRect.isNull()) return QTransform();
    const double sx = paperViewportRect.width() / modelRect.width();
    const double sy = paperViewportRect.height() / modelRect.height();
    const double scale = preserveAspect ? qMin(qAbs(sx), qAbs(sy)) : 1.0;
    const double finalSx = preserveAspect ? scale : sx;
    const double finalSy = preserveAspect ? -scale : -sy;
    const QPointF modelCenter = modelRect.center();
    const QPointF paperCenter = paperViewportRect.center();
    QTransform t;
    t.translate(paperCenter.x(), paperCenter.y());
    t.scale(finalSx, finalSy);
    t.translate(-modelCenter.x(), -modelCenter.y());
    return t;
}

QTransform paperToModelTransform(const QRectF& modelRect, const QRectF& paperViewportRect, bool preserveAspect)
{
    return modelToPaperTransform(modelRect, paperViewportRect, preserveAspect).inverted();
}

ViewportDefinition makeViewportForExtents(const QString& name, const QRectF& modelExtents, const QRectF& paperRect)
{
    ViewportDefinition vp;
    vp.name = name.trimmed().isEmpty() ? QStringLiteral("Viewport") : name.trimmed();
    vp.paperRect = paperRect.normalized();
    vp.modelViewRect = modelExtents.normalized();
    if (!modelExtents.isNull()) {
        const double sx = paperRect.width() / modelExtents.width();
        const double sy = paperRect.height() / modelExtents.height();
        vp.customScale = qMin(qAbs(sx), qAbs(sy));
    }
    return vp;
}

QString scaleToString(double scale)
{
    if (scale <= 0.0) return QStringLiteral("Custom");
    const double inverse = 1.0 / scale;
    if (qAbs(inverse - qRound(inverse)) < 1.0e-6 && inverse >= 1.0) {
        return QStringLiteral("1:%1").arg(qRound(inverse));
    }
    return QStringLiteral("%1:1").arg(scale, 0, 'f', 4);
}

double scaleFromString(const QString& text, bool* ok)
{
    QString s = text.trimmed();
    bool localOk = false;
    double result = s.toDouble(&localOk);
    if (!localOk && s.contains(QLatin1Char(':'))) {
        const QStringList parts = s.split(QLatin1Char(':'));
        if (parts.size() == 2) {
            bool okA = false;
            bool okB = false;
            const double a = parts[0].trimmed().toDouble(&okA);
            const double b = parts[1].trimmed().toDouble(&okB);
            if (okA && okB && b > 0.0) {
                result = a / b;
                localOk = true;
            }
        }
    }
    if (ok) *ok = localOk;
    return localOk ? result : 1.0;
}

} // namespace CadPlot
