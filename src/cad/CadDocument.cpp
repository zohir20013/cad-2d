#include "CadDocument.h"
#include "CadRenderStyle.h"

#include <QFile>
#include <QGraphicsScene>
#include <QJsonArray>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>
#include <QBrush>
#include <QPixmap>
#include <QLineF>
#include <QStyleOptionGraphicsItem>
#include <QFont>
#include <QPen>
#include <QHash>
#include <QPair>
#include <QJsonDocument>
#include <QTextStream>
#include <QStringConverter>
#include <QSizeF>
#include <QSet>
#include <QCoreApplication>
#include <algorithm>
#include <functional>
#include <QtGlobal>
#include <QtMath>
#include <cmath>
#include <limits>

namespace {
constexpr double kCadPi = 3.141592653589793238462643383279502884;
QJsonArray rectToJson(const QRectF& r)
{
    QJsonArray a;
    a.append(r.x());
    a.append(r.y());
    a.append(r.width());
    a.append(r.height());
    return a;
}


QString dxfNumber(double value)
{
    if (!qIsFinite(value)) value = 0.0;
    QString s = QString::number(value, 'g', 17);
    if (s == QStringLiteral("-0")) s = QStringLiteral("0");
    return s;
}

QString dxfText(QString text)
{
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\P"));
    return text;
}

void dxfPair(QTextStream& out, int code, const QString& value)
{
    out << code << '\n' << value << '\n';
}

void dxfPair(QTextStream& out, int code, double value)
{
    dxfPair(out, code, dxfNumber(value));
}

void dxfPair(QTextStream& out, int code, int value)
{
    dxfPair(out, code, QString::number(value));
}


double toDxfY(double y)
{
    // Internal scene coordinates are Y-down; DXF/CAD coordinates are Y-up.
    return -y;
}

int rgbTrueColor(const QColor& color)
{
    const QColor c = color.isValid() ? color : QColor(Qt::white);
    return (qBound(0, c.red(), 255) << 16) | (qBound(0, c.green(), 255) << 8) | qBound(0, c.blue(), 255);
}

QString safeDxfLayerName(const QString& layer)
{
    const QString trimmed = layer.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("0") : trimmed;
}

QRectF rectFromJson(const QJsonValue& v);

void writeDxfEntityCommon(QTextStream& out, const CadEntity& entity)
{
    dxfPair(out, 8, safeDxfLayerName(entity.layer()));
    const QColor c = entity.color().isValid() ? entity.color() : QColor(Qt::white);
    dxfPair(out, 62, 256);       // BYLAYER-compatible ACI fallback.
    dxfPair(out, 420, rgbTrueColor(c));
    if (entity.lineWeight() > 0.0)
        dxfPair(out, 370, qBound(0, int(std::round(entity.lineWeight() * 100.0)), 211));
    if (!entity.lineType().trimmed().isEmpty() && entity.lineType().compare(QStringLiteral("Continuous"), Qt::CaseInsensitive) != 0)
        dxfPair(out, 6, entity.lineType().trimmed());
}

void writeDxfLineEntity(QTextStream& out, const QPointF& a, const QPointF& b, const CadEntity& style)
{
    dxfPair(out, 0, QStringLiteral("LINE"));
    writeDxfEntityCommon(out, style);
    dxfPair(out, 10, a.x());
    dxfPair(out, 20, toDxfY(a.y()));
    dxfPair(out, 30, 0.0);
    dxfPair(out, 11, b.x());
    dxfPair(out, 21, toDxfY(b.y()));
    dxfPair(out, 31, 0.0);
}

void writeDxfLwPolyline(QTextStream& out, const QVector<QPointF>& points, bool closed, const CadEntity& style)
{
    if (points.size() < 2) return;
    dxfPair(out, 0, QStringLiteral("LWPOLYLINE"));
    writeDxfEntityCommon(out, style);
    dxfPair(out, 100, QStringLiteral("AcDbEntity"));
    dxfPair(out, 100, QStringLiteral("AcDbPolyline"));
    dxfPair(out, 90, int(points.size()));
    dxfPair(out, 70, closed ? 1 : 0);
    for (const QPointF& p : points) {
        dxfPair(out, 10, p.x());
        dxfPair(out, 20, toDxfY(p.y()));
    }
}

void writeDxfTextEntity(QTextStream& out,
                        const QPointF& pos,
                        const QString& text,
                        double height,
                        double rotationDeg,
                        const CadEntity& style,
                        double widthFactor = 1.0,
                        int hJust = 0,
                        int vJust = 0,
                        double obliqueAngleDeg = 0.0,
                        const QString& textStyleName = QString())
{
    if (text.trimmed().isEmpty()) return;
    dxfPair(out, 0, QStringLiteral("TEXT"));
    writeDxfEntityCommon(out, style);
    dxfPair(out, 10, pos.x());
    dxfPair(out, 20, toDxfY(pos.y()));
    dxfPair(out, 30, 0.0);
    if (hJust != 0 || vJust != 0) {
        dxfPair(out, 11, pos.x());
        dxfPair(out, 21, toDxfY(pos.y()));
        dxfPair(out, 31, 0.0);
    }
    dxfPair(out, 40, qMax(1.0e-6, height));
    const double wf = qBound(0.01, widthFactor, 100.0);
    if (std::abs(wf - 1.0) > 1.0e-9) dxfPair(out, 41, wf);
    dxfPair(out, 1, dxfText(text));
    dxfPair(out, 50, rotationDeg);
    if (std::abs(obliqueAngleDeg) > 1.0e-9) dxfPair(out, 51, qBound(-85.0, obliqueAngleDeg, 85.0));
    if (!textStyleName.trimmed().isEmpty()) dxfPair(out, 7, textStyleName.trimmed());
    if (hJust != 0) dxfPair(out, 72, hJust);
    if (vJust != 0) dxfPair(out, 73, vJust);
}

void writeDxfHatchEntity(QTextStream& out, const CadHatch& hatch)
{
    QVector<QVector<QPointF>> loops = hatch.loops();
    if (loops.isEmpty() && hatch.boundary().size() >= 3) loops.append(hatch.boundary());

    QVector<QVector<QPointF>> validLoops;
    for (const QVector<QPointF>& loop : loops) {
        if (loop.size() >= 3) validLoops.append(loop);
    }
    if (validLoops.isEmpty()) return;

    QString pattern = hatch.pattern().trimmed().isEmpty() ? QStringLiteral("ANSI31") : hatch.pattern().trimmed().toUpper();
    pattern.replace('-', '_');
    pattern.replace(' ', '_');
    const bool solid = pattern == QStringLiteral("SOLID")
        || pattern == QStringLiteral("PLEIN")
        || pattern == QStringLiteral("REMPLISSAGE")
        || pattern.startsWith(QStringLiteral("SOLID_"));
    const QString dxfPattern = solid ? QStringLiteral("SOLID") : pattern;

    dxfPair(out, 0, QStringLiteral("HATCH"));
    writeDxfEntityCommon(out, hatch);
    dxfPair(out, 100, QStringLiteral("AcDbEntity"));
    dxfPair(out, 100, QStringLiteral("AcDbHatch"));
    dxfPair(out, 10, 0.0);
    dxfPair(out, 20, 0.0);
    dxfPair(out, 30, 0.0);
    dxfPair(out, 210, 0.0);
    dxfPair(out, 220, 0.0);
    dxfPair(out, 230, 1.0);
    dxfPair(out, 2, dxfPattern);
    dxfPair(out, 70, solid ? 1 : 0);
    dxfPair(out, 71, 0);
    dxfPair(out, 91, int(validLoops.size()));

    for (const QVector<QPointF>& loop : validLoops) {
        dxfPair(out, 92, 2);   // polyline boundary path
        dxfPair(out, 72, 0);   // no bulge
        dxfPair(out, 73, 1);   // closed
        dxfPair(out, 93, int(loop.size()));
        for (const QPointF& p : loop) {
            dxfPair(out, 10, p.x());
            dxfPair(out, 20, toDxfY(p.y()));
        }
        dxfPair(out, 97, 0);
    }

    dxfPair(out, 75, 0);
    dxfPair(out, 76, 1);
    dxfPair(out, 52, hatch.angleDeg());
    dxfPair(out, 41, qMax(0.01, hatch.hatchScale()));
    dxfPair(out, 77, 0);
    dxfPair(out, 78, 0);
}

QVector<QPointF> rectAsPoints(const QRectF& r)
{
    const QRectF n = r.normalized();
    QVector<QPointF> pts;
    pts << n.topLeft() << n.topRight() << n.bottomRight() << n.bottomLeft();
    return pts;
}

QVector<QPointF> polygonAsPoints(const CadPolygon& polygon)
{
    QVector<QPointF> pts;
    const int sides = qMax(3, polygon.sides());
    pts.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const double a = qDegreesToRadians(polygon.rotationDeg() + 360.0 * double(i) / double(sides));
        pts.append(QPointF(polygon.center().x() + polygon.radius() * std::cos(a),
                           polygon.center().y() + polygon.radius() * std::sin(a)));
    }
    return pts;
}

QVector<QPointF> ellipseApproxPoints(const QRectF& rect, int segments = 72)
{
    QVector<QPointF> pts;
    const QRectF r = rect.normalized();
    const QPointF c = r.center();
    const double rx = r.width() * 0.5;
    const double ry = r.height() * 0.5;
    if (rx <= 1.0e-12 || ry <= 1.0e-12) return pts;
    pts.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        const double a = 2.0 * kCadPi * double(i) / double(segments);
        pts.append(QPointF(c.x() + rx * std::cos(a), c.y() + ry * std::sin(a)));
    }
    return pts;
}

void writeDxfEntity(QTextStream& out, const CadEntity& entity)
{
    switch (entity.type()) {
    case CadEntity::Type::Line: {
        const auto& e = static_cast<const CadLine&>(entity);
        writeDxfLineEntity(out, e.start(), e.end(), entity);
        break;
    }
    case CadEntity::Type::Circle: {
        const auto& e = static_cast<const CadCircle&>(entity);
        if (e.radius() <= 0.0) break;
        dxfPair(out, 0, QStringLiteral("CIRCLE"));
        writeDxfEntityCommon(out, entity);
        dxfPair(out, 10, e.center().x());
        dxfPair(out, 20, toDxfY(e.center().y()));
        dxfPair(out, 30, 0.0);
        dxfPair(out, 40, e.radius());
        break;
    }
    case CadEntity::Type::Arc: {
        const auto& e = static_cast<const CadArc&>(entity);
        if (e.radius() <= 0.0) break;
        dxfPair(out, 0, QStringLiteral("ARC"));
        writeDxfEntityCommon(out, entity);
        dxfPair(out, 10, e.center().x());
        dxfPair(out, 20, toDxfY(e.center().y()));
        dxfPair(out, 30, 0.0);
        dxfPair(out, 40, e.radius());
        const double start = e.startAngleDeg();
        dxfPair(out, 50, start);
        dxfPair(out, 51, start + e.spanAngleDeg());
        break;
    }
    case CadEntity::Type::Rectangle: {
        const auto& e = static_cast<const CadRectangle&>(entity);
        writeDxfLwPolyline(out, rectAsPoints(e.rect()), true, entity);
        break;
    }
    case CadEntity::Type::Polyline: {
        const auto& e = static_cast<const CadPolyline&>(entity);
        writeDxfLwPolyline(out, e.points(), e.closed(), entity);
        break;
    }
    case CadEntity::Type::Ellipse: {
        const auto& e = static_cast<const CadEllipse&>(entity);
        writeDxfLwPolyline(out, ellipseApproxPoints(e.rect()), true, entity);
        break;
    }
    case CadEntity::Type::Polygon: {
        const auto& e = static_cast<const CadPolygon&>(entity);
        writeDxfLwPolyline(out, polygonAsPoints(e), true, entity);
        break;
    }
    case CadEntity::Type::Hatch: {
        const auto& e = static_cast<const CadHatch&>(entity);
        writeDxfHatchEntity(out, e);
        break;
    }
    case CadEntity::Type::Text: {
        const auto& e = static_cast<const CadText&>(entity);
        writeDxfTextEntity(out, e.position(), e.text(), e.height(), e.rotationDeg(), entity,
                           e.widthFactor(), e.horizontalJustification(), e.verticalJustification(),
                           e.obliqueAngleDeg(), e.fontName());
        break;
    }
    case CadEntity::Type::LinearDimension: {
        const auto& e = static_cast<const CadLinearDimension&>(entity);
        writeDxfLineEntity(out, e.first(), e.second(), entity);
        writeDxfLineEntity(out, e.first(), e.dimensionPoint(), entity);
        writeDxfLineEntity(out, e.second(), e.dimensionPoint(), entity);
        writeDxfTextEntity(out, e.dimensionPoint(), QString::number(e.measuredLength(), 'f', 2), e.textHeight(), 0.0, entity);
        break;
    }
    case CadEntity::Type::Leader: {
        const auto& e = static_cast<const CadLeader&>(entity);
        writeDxfLineEntity(out, e.arrowPoint(), e.textPoint(), entity);
        writeDxfTextEntity(out, e.textPoint(), e.text(), e.textHeight(), 0.0, entity);
        break;
    }
    case CadEntity::Type::BlockReference: {
        const auto& e = static_cast<const CadBlockReference&>(entity);
        dxfPair(out, 0, QStringLiteral("INSERT"));
        writeDxfEntityCommon(out, entity);
        dxfPair(out, 2, e.blockName().trimmed().isEmpty() ? QStringLiteral("DWGVIEW_BLOCK") : e.blockName().trimmed());
        dxfPair(out, 10, e.insertionPoint().x());
        dxfPair(out, 20, toDxfY(e.insertionPoint().y()));
        dxfPair(out, 30, 0.0);
        dxfPair(out, 41, e.scaleFactor());
        dxfPair(out, 42, e.scaleFactor());
        dxfPair(out, 50, e.rotationDeg());
        break;
    }
    }

}

void writeDxfBlockDefinition(QTextStream& out, const QString& name, const QJsonObject& blockDefinition)
{
    const QString blockName = name.trimmed().isEmpty() ? QStringLiteral("DWGVIEW_BLOCK") : name.trimmed();
    const QRectF baseRect = rectFromJson(blockDefinition.value("basePoint"));
    const QPointF basePoint = baseRect.topLeft();

    dxfPair(out, 0, QStringLiteral("BLOCK"));
    dxfPair(out, 8, QStringLiteral("0"));
    dxfPair(out, 2, blockName);
    dxfPair(out, 70, 0);
    dxfPair(out, 10, basePoint.x());
    dxfPair(out, 20, toDxfY(basePoint.y()));
    dxfPair(out, 30, 0.0);
    dxfPair(out, 3, blockName);
    dxfPair(out, 1, QString());

    const QJsonArray entities = blockDefinition.value("entities").toArray();
    for (const QJsonValue& value : entities) {
        std::unique_ptr<CadEntity> child = CadEntity::fromJson(value.toObject());
        if (!child) continue;
        // DWGViewer stores block definition entities in drawing coordinates and
        // keeps a base point separately. DXF BLOCK content is written in local
        // coordinates, so subtract the block base point before exporting.
        child->translate(QPointF(-basePoint.x(), -basePoint.y()));
        writeDxfEntity(out, *child);
    }

    dxfPair(out, 0, QStringLiteral("ENDBLK"));
    dxfPair(out, 8, QStringLiteral("0"));
}

void writeDxfBlocksSection(QTextStream& out, const QMap<QString, QJsonObject>& blocks)
{
    dxfPair(out, 0, QStringLiteral("SECTION"));
    dxfPair(out, 2, QStringLiteral("BLOCKS"));

    // Standard model/paper block records improve compatibility with AutoCAD,
    // QCAD and LibreCAD even when the drawing has no user-defined blocks.
    QJsonObject modelSpace;
    modelSpace["name"] = QStringLiteral("*Model_Space");
    modelSpace["basePoint"] = rectToJson(QRectF(QPointF(0.0, 0.0), QSizeF(0.0, 0.0)));
    modelSpace["entities"] = QJsonArray();
    writeDxfBlockDefinition(out, QStringLiteral("*Model_Space"), modelSpace);

    QJsonObject paperSpace;
    paperSpace["name"] = QStringLiteral("*Paper_Space");
    paperSpace["basePoint"] = rectToJson(QRectF(QPointF(0.0, 0.0), QSizeF(0.0, 0.0)));
    paperSpace["entities"] = QJsonArray();
    writeDxfBlockDefinition(out, QStringLiteral("*Paper_Space"), paperSpace);

    for (auto it = blocks.constBegin(); it != blocks.constEnd(); ++it) {
        const QString name = it.key().trimmed();
        if (name.isEmpty() || name.startsWith(QLatin1Char('*'))) continue;
        writeDxfBlockDefinition(out, name, it.value());
    }

    dxfPair(out, 0, QStringLiteral("ENDSEC"));
}




double medianHatchSegmentLength(QVector<double> values)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !qIsFinite(v) || v <= 1.0e-7; }), values.end());
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

bool lodHatchLoopCanBeClosedSafely(const QVector<QPointF>& loop)
{
    if (loop.size() < 3) return false;
    QVector<double> segments;
    segments.reserve(loop.size());
    double maxGap = 0.0;
    for (int i = 0; i < loop.size(); ++i) {
        const QPointF& a = loop.at(i);
        const QPointF& b = loop.at((i + 1) % loop.size());
        if (!qIsFinite(a.x()) || !qIsFinite(a.y()) || !qIsFinite(b.x()) || !qIsFinite(b.y())) return false;
        const double d = QLineF(a, b).length();
        segments.append(d);
        maxGap = std::max(maxGap, d);
    }
    const double typical = medianHatchSegmentLength(segments);
    if (typical <= 1.0e-9) return false;
    const double closingGap = QLineF(loop.last(), loop.first()).length();
    if (maxGap > std::max(250.0, typical * 22.0)) return false;
    if (closingGap > std::max(120.0, typical * 12.0)) return false;
    return true;
}

QPainterPath hatchPathFromLoops(const QVector<QVector<QPointF>>& loops, const QVector<QPointF>& boundary)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    auto addLoop = [&path](const QVector<QPointF>& pts) {
        if (!lodHatchLoopCanBeClosedSafely(pts)) return;
        path.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i) path.lineTo(pts.at(i));
        path.closeSubpath();
    };
    if (!loops.isEmpty()) {
        for (const QVector<QPointF>& loop : loops) addLoop(loop);
    } else {
        addLoop(boundary);
    }
    return path;
}


QPainterPath sampledPathForNavigation(const QPainterPath& source, int elementCount)
{
    QPainterPath out;
    const int n = source.elementCount();
    if (n <= 0) return out;

    // Le but n'est pas de changer la géométrie finale: ce chemin est seulement
    // utilisé à très faible zoom / pendant les vues globales pour éviter de
    // redessiner des milliers d'éléments invisibles. On conserve des segments
    // représentatifs dans le même emplacement CAD, au lieu de remplacer le plan
    // par de faux rectangles de tuiles.
    int stride = 1;
    if (elementCount > 2500 || n > 12000) stride = 24;
    else if (elementCount > 1200 || n > 6000) stride = 16;
    else if (elementCount > 600 || n > 3000) stride = 10;
    else if (elementCount > 250 || n > 1200) stride = 6;
    else if (elementCount > 120 || n > 600) stride = 4;
    if (stride <= 1) return source;

    int segmentIndex = 0;
    QPointF lastMove;
    QPointF prev;
    bool havePrev = false;

    for (int i = 0; i < n; ++i) {
        const QPainterPath::Element e = source.elementAt(i);
        const QPointF p(e.x, e.y);
        if (e.isMoveTo()) {
            lastMove = p;
            prev = p;
            havePrev = true;
            continue;
        }
        if (!havePrev) {
            prev = p;
            havePrev = true;
            continue;
        }

        // Les courbes QPainterPath sont déjà aplaties par Qt au rendu. Pour le
        // niveau overview, on relie quelques points de contrôle; c'est beaucoup
        // plus léger et suffisant quand la forme fait quelques pixels.
        if ((segmentIndex % stride) == 0) {
            out.moveTo(prev);
            out.lineTo(p);
        }
        ++segmentIndex;
        prev = p;
    }

    if (out.isEmpty()) return source;
    return out;
}

class FastCadBatchItem final : public QGraphicsItem
{
public:
    FastCadBatchItem(const QVector<QLineF>& lines, const QPen& pen, const QString& layerName)
        : m_lines(lines), m_pen(pen), m_layerName(layerName)
    {
        QRectF b;
        for (const QLineF& l : m_lines) {
            if (l.isNull()) continue;
            const QRectF lb(l.p1(), l.p2());
            b = b.isNull() ? lb.normalized() : b.united(lb.normalized());
        }
        m_bounds = b.isValid() && !b.isNull() ? b.adjusted(-2.0, -2.0, 2.0, 2.0) : QRectF(-1.0, -1.0, 2.0, 2.0);
        setData(0, layerName);
        setData(1, -1);
        setData(3, 0);
        setData(10, 1); // item de rendu groupe, non editable individuellement
        setFlag(QGraphicsItem::ItemIsSelectable, false);
    }

    QRectF boundingRect() const override { return m_bounds; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) override
    {
        if (m_lines.isEmpty()) return;
        if (option && !option->exposedRect.intersects(m_bounds)) return;

        QPen p = m_pen;
        p.setCosmetic(true);
        painter->setPen(p);
        painter->setBrush(Qt::NoBrush);

        // V26: drawLines(QLineF*) est nettement plus léger que QPainterPath
        // pour les DWG/DXF énormes. À très faible zoom, on échantillonne les
        // segments pour garder la navigation fluide sans remplacer le dessin
        // par des rectangles de tuiles.
        const double lod = option ? QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform()) : 1.0;
        int stride = 1;
        const int n = m_lines.size();
        if (lod < 0.0015 && n > 1500) stride = 20;
        else if (lod < 0.003 && n > 1500) stride = 12;
        else if (lod < 0.008 && n > 2500) stride = 8;
        else if (lod < 0.020 && n > 5000) stride = 4;
        else if (lod < 0.050 && n > 10000) stride = 2;

        if (stride <= 1) {
            painter->drawLines(m_lines.constData(), m_lines.size());
            return;
        }

        QVector<QLineF> sampled;
        sampled.reserve((n / stride) + 1);
        for (int i = 0; i < n; i += stride) sampled.append(m_lines.at(i));
        painter->drawLines(sampled.constData(), sampled.size());
    }

private:
    QVector<QLineF> m_lines;
    QPen m_pen;
    QString m_layerName;
    QRectF m_bounds;
};

class LodCadTextItem final : public QGraphicsItem
{
public:
    LodCadTextItem(const CadText* text, const CadLayer& layer)
        : m_text(text ? text->text() : QStringLiteral("Text")),
          m_height(text ? qMax(1.0e-6, text->height()) : 1.0),
          m_color(CadRenderStyle::deepCadColor(text ? text->color() : layer.color)),
          m_rotationDeg(text ? text->rotationDeg() : 0.0),
          m_widthFactor(text ? qBound(0.01, text->widthFactor(), 100.0) : 1.0)
    {
        const QPointF pos = text ? text->position() : QPointF();
        const int lineCount = qMax(1, static_cast<int>(m_text.count(QLatin1Char('\n')) + 1));
        int maxChars = 1;
        for (const QString& line : m_text.split(QLatin1Char('\n'))) {
            maxChars = qMax(maxChars, static_cast<int>(line.size()));
        }
        const double w = qMax(m_height * 0.75, maxChars * m_height * 0.62 * m_widthFactor);
        const double h = m_height * (1.25 * lineCount);
        m_bounds = QRectF(0.0, -m_height, w, h).adjusted(-m_height * 0.10, -m_height * 0.10, m_height * 0.10, m_height * 0.10);
        setPos(pos);
        setRotation(-m_rotationDeg);
        setData(0, layer.name);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemIsFocusable, true);
    }

    QRectF boundingRect() const override { return m_bounds; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) override
    {
        const double lod = option ? QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform()) : 1.0;
        const double pxHeight = m_height * lod;

        painter->save();
        QPen pen(m_color);
        pen.setCosmetic(true);
        pen.setWidthF(pxHeight < 5.0 ? 1.0 : 1.25);
        painter->setPen(pen);

        // Text illisible à ce zoom: proxy carré/rectangle au lieu de glyphes coûteux.
        if (pxHeight < 5.0) {
            painter->setBrush(Qt::NoBrush);
            const double s = qMax(m_height * 0.45, qMin(m_bounds.width(), m_height));
            painter->drawRect(QRectF(0.0, -s, s, s));
            painter->restore();
            return;
        }
        if (pxHeight < 10.0) {
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(m_bounds);
            painter->restore();
            return;
        }

        QFont font;
        font.setPointSizeF(qMax(1.0, m_height));
        painter->setFont(font);
        painter->setBrush(Qt::NoBrush);
        painter->drawText(QRectF(0.0, -m_height, m_bounds.width(), m_bounds.height()),
                          Qt::AlignLeft | Qt::AlignTop, m_text);
        painter->restore();
    }

private:
    QString m_text;
    double m_height = 1.0;
    QColor m_color;
    double m_rotationDeg = 0.0;
    double m_widthFactor = 1.0;
    QRectF m_bounds;
};


QString cadNormalizedHatchPatternName(const QString& pattern)
{
    QString p = pattern.trimmed().toUpper();
    p.replace('-', '_');
    p.replace(' ', '_');
    if (p.isEmpty()) p = QStringLiteral("ANSI31");
    return p;
}

bool cadHatchPatternIsSolid(const QString& pattern)
{
    const QString p = cadNormalizedHatchPatternName(pattern);
    return p == QStringLiteral("SOLID")
        || p == QStringLiteral("PLEIN")
        || p == QStringLiteral("REMPLISSAGE")
        || p.startsWith(QStringLiteral("SOLID_"))
        || p.startsWith(QStringLiteral("SOLID"));
}

int cadSolidHatchAlpha(const QString& pattern)
{
    const QString p = cadNormalizedHatchPatternName(pattern);
    if (p == QStringLiteral("SOLID_TRANSPARENT")) return 35;
    if (p == QStringLiteral("SOLID_LIGHT")) return 55;
    if (p == QStringLiteral("SOLID_MEDIUM")) return 128;
    if (p == QStringLiteral("SOLID_DARK")) return 215;
    if (p.startsWith(QStringLiteral("SOLID_"))) {
        bool ok = false;
        const int percent = p.mid(6).toInt(&ok);
        if (ok) return qBound(12, qRound(255.0 * double(percent) / 100.0), 242);
    }
    return 135;
}

void cadDrawHatchLine(QPainter& painter, double x1, double y1, double x2, double y2)
{
    painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
}

void cadDrawHatchDot(QPainter& painter, double x, double y, double r)
{
    painter.drawEllipse(QPointF(x, y), r, r);
}

QBrush cadMaterialHatchBrush(const QString& pattern, QColor color, double scale, double angleDeg, double viewLod = 1.0)
{
    const QString p = cadNormalizedHatchPatternName(pattern);

    if (cadHatchPatternIsSolid(p)) {
        color.setAlpha(cadSolidHatchAlpha(p));
        return QBrush(color, Qt::SolidPattern);
    }

    // Same native material hatch renderer as CadHatch::createGraphicsItem(), used
    // here for LOD rendering. Without this path, large DXF scenes displayed only
    // Qt's default diagonal hatch regardless of the selected pattern.
    const double safeScale = qBound(0.10, std::abs(scale), 250.0);
    const double lodFactor = qBound(0.55, std::sqrt(qMax(0.10, viewLod)), 2.0);
    const int base = qBound(20, int(std::round(36.0 * std::sqrt(qBound(0.25, safeScale, 16.0)) / lodFactor)), 180);
    const int tile = qMax(24, base + (base % 2));
    const double step = qMax(4.0, tile / 4.0);

    QPixmap pixmap(tile, tile);
    pixmap.fill(Qt::transparent);

    QColor stroke = color;
    stroke.setAlpha(qBound(70, stroke.alpha(), 230));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(stroke, qBound(1.0, safeScale * 0.45, 2.6));
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    auto drawDiagonal45 = [&]() {
        for (double x = -tile; x <= tile * 2.0; x += step) cadDrawHatchLine(painter, x, tile, x + tile, 0);
    };
    auto drawDiagonal135 = [&]() {
        for (double x = -tile; x <= tile * 2.0; x += step) cadDrawHatchLine(painter, x, 0, x + tile, tile);
    };
    auto drawHorizontal = [&]() {
        for (double y = step * 0.5; y < tile; y += step) cadDrawHatchLine(painter, 0, y, tile, y);
    };
    auto drawVertical = [&]() {
        for (double x = step * 0.5; x < tile; x += step) cadDrawHatchLine(painter, x, 0, x, tile);
    };
    auto drawDots = [&](double spacing, double radius) {
        painter.setBrush(stroke);
        for (double y = spacing * 0.55; y < tile; y += spacing) {
            for (double x = spacing * 0.55; x < tile; x += spacing) {
                const double offset = (int(y / spacing) % 2) ? spacing * 0.35 : 0.0;
                cadDrawHatchDot(painter, std::fmod(x + offset, double(tile)), y, radius);
            }
        }
        painter.setBrush(Qt::NoBrush);
    };

    if (p == QStringLiteral("ANSI31") || p == QStringLiteral("STEEL") || p == QStringLiteral("METAL")) {
        drawDiagonal45();
    } else if (p == QStringLiteral("ANSI32") || p == QStringLiteral("VERTICAL")) {
        drawVertical();
    } else if (p == QStringLiteral("ANSI33") || p == QStringLiteral("HORIZONTAL")) {
        drawHorizontal();
    } else if (p == QStringLiteral("ANSI34") || p == QStringLiteral("CROSS") || p == QStringLiteral("CROSSHATCH")) {
        drawHorizontal(); drawVertical();
    } else if (p == QStringLiteral("ANSI35") || p == QStringLiteral("DIAGONAL_CROSS")) {
        drawDiagonal45(); drawDiagonal135();
    } else if (p == QStringLiteral("ANSI36") || p == QStringLiteral("ZIGZAG")) {
        for (double y = step; y < tile + step; y += step * 1.25) {
            QPainterPath wave;
            wave.moveTo(0, y);
            for (double x = 0; x <= tile; x += step * 0.5)
                wave.lineTo(x, y + ((int(x / (step * 0.5)) % 2) ? -step * 0.35 : step * 0.35));
            painter.drawPath(wave);
        }
    } else if (p == QStringLiteral("ANSI37") || p == QStringLiteral("GRID")) {
        drawHorizontal(); drawVertical();
        pen.setWidthF(qBound(0.8, safeScale * 0.25, 1.8));
        painter.setPen(pen);
        drawDiagonal45();
    } else if (p == QStringLiteral("ANSI38") || p == QStringLiteral("NET")) {
        drawDiagonal45(); drawDiagonal135();
        pen.setWidthF(qBound(0.8, safeScale * 0.25, 1.7));
        painter.setPen(pen);
        drawHorizontal();
    } else if (p == QStringLiteral("CONCRETE") || p == QStringLiteral("BETON") || p == QStringLiteral("AR_CONC")) {
        drawDots(step * 0.75, qBound(0.65, safeScale * 0.28, 1.7));
        for (int i = 0; i < 7; ++i) {
            const double x = std::fmod(7.0 + i * step * 1.37, double(tile));
            const double y = std::fmod(11.0 + i * step * 0.91, double(tile));
            cadDrawHatchLine(painter, x - step * 0.25, y, x + step * 0.35, y + step * 0.18);
        }
    } else if (p == QStringLiteral("SAND") || p == QStringLiteral("SABLE") || p == QStringLiteral("DOTS")) {
        drawDots(step * 0.55, qBound(0.45, safeScale * 0.20, 1.25));
    } else if (p == QStringLiteral("GRAVEL") || p == QStringLiteral("GRAVIER")) {
        drawDots(step * 0.90, qBound(0.8, safeScale * 0.32, 2.0));
        for (int i = 0; i < 5; ++i) {
            const double x = std::fmod(5.0 + i * step * 1.55, double(tile));
            const double y = std::fmod(3.0 + i * step * 1.10, double(tile));
            painter.drawRect(QRectF(x, y, step * 0.35, step * 0.25));
        }
    } else if (p == QStringLiteral("WOOD") || p == QStringLiteral("BOIS") || p == QStringLiteral("AR_WOOD")) {
        for (double y = step * 0.35; y < tile; y += step * 0.65) {
            QPainterPath grain;
            grain.moveTo(0, y);
            grain.cubicTo(tile * 0.25, y - step * 0.35, tile * 0.55, y + step * 0.35, tile, y);
            painter.drawPath(grain);
        }
        painter.drawEllipse(QRectF(tile * 0.25, tile * 0.25, step * 0.9, step * 0.45));
        painter.drawEllipse(QRectF(tile * 0.62, tile * 0.58, step * 0.7, step * 0.36));
    } else if (p == QStringLiteral("BRICK") || p == QStringLiteral("BRIQUE") || p == QStringLiteral("MASONRY")) {
        const double h = step;
        const double w = step * 2.2;
        for (double y = 0; y <= tile; y += h) cadDrawHatchLine(painter, 0, y, tile, y);
        for (int row = 0; row < int(tile / h) + 2; ++row) {
            const double y0 = row * h;
            const double offset = (row % 2) ? w * 0.5 : 0.0;
            for (double x = offset; x <= tile; x += w) cadDrawHatchLine(painter, x, y0, x, y0 + h);
        }
    } else if (p == QStringLiteral("TILE") || p == QStringLiteral("CARRELAGE")) {
        drawHorizontal(); drawVertical();
    } else if (p == QStringLiteral("EARTH") || p == QStringLiteral("TERRE") || p == QStringLiteral("GROUND")) {
        drawHorizontal();
        for (double x = 0; x < tile; x += step) cadDrawHatchLine(painter, x, step * 1.2, x + step * 0.6, step * 1.8);
    } else if (p == QStringLiteral("INSULATION") || p == QStringLiteral("ISOLATION")) {
        for (double x = -step; x < tile + step; x += step) {
            QPainterPath arc;
            arc.moveTo(x, tile * 0.5);
            arc.cubicTo(x + step * 0.25, 0, x + step * 0.75, tile, x + step, tile * 0.5);
            painter.drawPath(arc);
        }
    } else if (p == QStringLiteral("GLASS") || p == QStringLiteral("VERRE")) {
        drawDiagonal45();
        pen.setWidthF(qBound(0.8, safeScale * 0.20, 1.5));
        painter.setPen(pen);
        for (double y = step; y < tile; y += step * 1.1) cadDrawHatchLine(painter, 0, y, tile, y - step * 0.4);
    } else if (p == QStringLiteral("WATER") || p == QStringLiteral("EAU")) {
        for (double y = step * 0.6; y < tile; y += step * 0.85) {
            QPainterPath wave;
            wave.moveTo(0, y);
            wave.cubicTo(tile * 0.20, y - step * 0.4, tile * 0.30, y + step * 0.4, tile * 0.50, y);
            wave.cubicTo(tile * 0.70, y - step * 0.4, tile * 0.80, y + step * 0.4, tile, y);
            painter.drawPath(wave);
        }
    } else if (p == QStringLiteral("GRASS") || p == QStringLiteral("HERBE")) {
        for (double x = step * 0.3; x < tile; x += step * 0.7) {
            cadDrawHatchLine(painter, x, tile, x - step * 0.2, tile - step * 0.7);
            cadDrawHatchLine(painter, x, tile, x + step * 0.2, tile - step * 0.6);
        }
    } else if (p == QStringLiteral("DENSE") || p == QStringLiteral("DENSE4")) {
        painter.fillRect(QRect(0, 0, tile, tile), QBrush(color, Qt::Dense4Pattern));
    } else {
        drawDiagonal45();
    }

    painter.end();
    QBrush brush(pixmap);
    QTransform transform;
    if (!qFuzzyIsNull(angleDeg)) transform.rotate(angleDeg);
    brush.setTransform(transform);
    return brush;
}

class LodCadHatchItem final : public QGraphicsItem
{
public:
    LodCadHatchItem(const CadHatch* hatch, const CadLayer& layer)
        : m_path(hatchPathFromLoops(hatch ? hatch->loops() : QVector<QVector<QPointF>>(),
                                    hatch ? hatch->boundary() : QVector<QPointF>())),
          m_color(CadRenderStyle::deepCadColor(hatch ? hatch->color() : layer.color)),
          m_pattern(hatch ? cadNormalizedHatchPatternName(hatch->pattern()) : QStringLiteral("ANSI31")),
          m_scale(hatch ? hatch->hatchScale() : 1.0),
          m_angleDeg(hatch ? hatch->angleDeg() : 45.0)
    {
        m_bounds = m_path.controlPointRect().adjusted(-2.0, -2.0, 2.0, 2.0);
        setData(0, layer.name);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemIsFocusable, true);
    }

    QRectF boundingRect() const override { return m_bounds; }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) override
    {
        if (m_path.isEmpty()) return;
        const double lod = option ? QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform()) : 1.0;
        const double pxW = m_bounds.width() * lod;
        const double pxH = m_bounds.height() * lod;
        const double pxMax = qMax(pxW, pxH);

        QPen pen(m_color);
        pen.setCosmetic(true);
        pen.setWidthF(1.1);
        painter->setPen(pen);

        // Trop petit ou pendant zoom éloigné: pas de remplissage, seulement contour léger.
        if (pxMax < 26.0 || lod < 0.015) {
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(m_path);
            return;
        }

        QColor fill = m_color;
        fill.setAlpha(pxMax < 80.0 ? 70 : 135);
        painter->setBrush(cadMaterialHatchBrush(m_pattern, fill, m_scale, m_angleDeg, lod));
        painter->drawPath(m_path);
    }

private:
    QPainterPath m_path;
    QColor m_color;
    QString m_pattern;
    double m_scale = 1.0;
    double m_angleDeg = 45.0;
    QRectF m_bounds;
};

QPen fastPenForEntity(const CadEntity* e)
{
    QPen pen(CadRenderStyle::deepCadColor(e ? e->color() : QColor(Qt::white)));
    pen.setCosmetic(true);
    const double lw = e ? e->lineWeight() : 0.0;
    pen.setWidthF(lw > 0.0 ? qBound(1.1, lw, 5.0) : 1.55);

    // In batch/huge-file mode we intentionally draw solid geometry.
    // Qt cosmetic dash patterns do not respect DWG LTSCALE/PSLTSCALE and can make
    // continuous-looking CAD geometry appear as dotted points after import.
    pen.setStyle(Qt::SolidLine);
    return pen;
}

QString batchKeyForEntity(const CadEntity* e)
{
    const QColor c = e->color();
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(e->layer())
        .arg(c.rgba())
        .arg(e->lineWeight(), 0, 'f', 3)
        .arg(e->lineType())
        .arg(static_cast<int>(e->type()))
        .arg(e->lineWeight() <= 0.0 ? 0 : 1);
}

QRectF approximateEntityBounds(const CadEntity* e)
{
    if (!e) return QRectF();
    switch (e->type()) {
    case CadEntity::Type::Line: {
        const auto* l = dynamic_cast<const CadLine*>(e);
        return l ? QRectF(l->start(), l->end()).normalized() : QRectF();
    }
    case CadEntity::Type::Circle: {
        const auto* c = dynamic_cast<const CadCircle*>(e);
        if (!c) return QRectF();
        const double r = c->radius();
        return QRectF(c->center().x() - r, c->center().y() - r, 2.0 * r, 2.0 * r);
    }
    case CadEntity::Type::Rectangle: {
        const auto* r = dynamic_cast<const CadRectangle*>(e);
        return r ? r->rect().normalized() : QRectF();
    }
    case CadEntity::Type::Polyline: {
        const auto* pl = dynamic_cast<const CadPolyline*>(e);
        if (!pl || pl->points().isEmpty()) return QRectF();
        QRectF b(pl->points().first(), QSizeF(0, 0));
        for (const QPointF& pt : pl->points()) b = b.united(QRectF(pt, QSizeF(0, 0)));
        return b.normalized();
    }
    case CadEntity::Type::Arc: {
        const auto* a = dynamic_cast<const CadArc*>(e);
        if (!a) return QRectF();
        const double r = a->radius();
        return QRectF(a->center().x() - r, a->center().y() - r, 2.0 * r, 2.0 * r);
    }
    case CadEntity::Type::Ellipse: {
        const auto* el = dynamic_cast<const CadEllipse*>(e);
        return el ? el->rect().normalized() : QRectF();
    }
    case CadEntity::Type::Polygon: {
        const auto* pg = dynamic_cast<const CadPolygon*>(e);
        if (!pg) return QRectF();
        const double r = pg->radius();
        return QRectF(pg->center().x() - r, pg->center().y() - r, 2.0 * r, 2.0 * r);
    }
    default:
        return QRectF();
    }
}

QString tileKeyForEntity(const CadEntity* e, const QRectF& limits, double tileSize)
{
    if (!e || tileSize <= 1e-9 || !limits.isValid()) return batchKeyForEntity(e);

    const QRectF b = approximateEntityBounds(e);
    const QPointF c = b.isValid() && !b.isNull() ? b.center() : limits.center();
    const int tx = static_cast<int>(std::floor((c.x() - limits.left()) / tileSize));
    const int ty = static_cast<int>(std::floor((c.y() - limits.top()) / tileSize));
    return batchKeyForEntity(e) + QStringLiteral("|tile:%1:%2").arg(tx).arg(ty);
}

double recommendedTileSize(const QRectF& limits, int entityCount)
{
    if (!limits.isValid() || limits.isNull()) return 1000.0;
    const double maxDim = std::max(limits.width(), limits.height());
    if (!std::isfinite(maxDim) || maxDim <= 1.0) return 1000.0;

    // V21: les DWG avec blocs exploses peuvent depasser le million
    // d'entites. Des tuiles plus fines evitent les QPainterPath geants
    // et rendent le pan/zoom beaucoup plus stable.
    int divisions = 8;
    // Performance rollback: la version récente avait augmenté fortement le
    // nombre de tuiles. Sur QGraphicsScene/BspTreeIndex, trop de petites tuiles
    // crée beaucoup trop de QGraphicsItem et ralentit zoom/pan. On revient aux
    // valeurs de l'ancienne version qui était plus fluide sur les gros DWG/DXF.
    if (entityCount > 1500000) divisions = 96;
    else if (entityCount > 1000000) divisions = 80;
    else if (entityCount > 500000) divisions = 64;
    else if (entityCount > 300000) divisions = 48;
    else if (entityCount > 150000) divisions = 32;
    else if (entityCount > 50000) divisions = 20;
    return std::max(maxDim / static_cast<double>(divisions), 25.0);
}

void appendArcSegments(QVector<QLineF>& lines, const QPointF& center, double rx, double ry, double startDeg, double spanDeg, int segmentHint)
{
    if (rx <= 0.0 || ry <= 0.0 || !std::isfinite(rx) || !std::isfinite(ry)) return;
    const double spanAbs = std::abs(spanDeg);
    int segs = std::max(4, static_cast<int>(std::ceil(spanAbs / 15.0)));
    segs = std::max(segs, segmentHint);
    segs = std::min(segs, 96);

    QPointF prev;
    bool havePrev = false;
    for (int i = 0; i <= segs; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segs);
        const double a = qDegreesToRadians(startDeg + spanDeg * t);
        const QPointF p(center.x() + std::cos(a) * rx, center.y() + std::sin(a) * ry);
        if (havePrev) lines.append(QLineF(prev, p));
        prev = p;
        havePrev = true;
    }
}

bool appendEntityToBatchLines(const CadEntity* e, QVector<QLineF>& lines)
{
    if (!e) return false;
    switch (e->type()) {
    case CadEntity::Type::Line: {
        const auto* l = dynamic_cast<const CadLine*>(e);
        if (!l) return false;
        lines.append(QLineF(l->start(), l->end()));
        return true;
    }
    case CadEntity::Type::Circle: {
        const auto* c = dynamic_cast<const CadCircle*>(e);
        if (!c || c->radius() <= 0.0) return false;
        appendArcSegments(lines, c->center(), c->radius(), c->radius(), 0.0, 360.0, 32);
        return true;
    }
    case CadEntity::Type::Rectangle: {
        const auto* r = dynamic_cast<const CadRectangle*>(e);
        if (!r) return false;
        const QRectF rr = r->rect().normalized();
        const QPointF a = rr.topLeft();
        const QPointF b = rr.topRight();
        const QPointF c = rr.bottomRight();
        const QPointF d = rr.bottomLeft();
        lines.append(QLineF(a, b));
        lines.append(QLineF(b, c));
        lines.append(QLineF(c, d));
        lines.append(QLineF(d, a));
        return true;
    }
    case CadEntity::Type::Polyline: {
        const auto* pl = dynamic_cast<const CadPolyline*>(e);
        if (!pl || pl->points().size() < 2) return false;
        const QVector<QPointF>& pts = pl->points();
        for (int i = 1; i < pts.size(); ++i) lines.append(QLineF(pts.at(i - 1), pts.at(i)));
        if (pl->closed() && pts.size() > 2) lines.append(QLineF(pts.last(), pts.first()));
        return true;
    }
    case CadEntity::Type::Arc: {
        const auto* a = dynamic_cast<const CadArc*>(e);
        if (!a || a->radius() <= 0.0) return false;
        appendArcSegments(lines, a->center(), a->radius(), a->radius(), a->startAngleDeg(), a->spanAngleDeg(), 8);
        return true;
    }
    case CadEntity::Type::Ellipse: {
        const auto* el = dynamic_cast<const CadEllipse*>(e);
        if (!el) return false;
        const QRectF rr = el->rect().normalized();
        appendArcSegments(lines, rr.center(), rr.width() * 0.5, rr.height() * 0.5, 0.0, 360.0, 32);
        return true;
    }
    case CadEntity::Type::Polygon: {
        const auto* pg = dynamic_cast<const CadPolygon*>(e);
        if (!pg || pg->sides() < 3 || pg->radius() <= 0.0) return false;
        QVector<QPointF> pts;
        pts.reserve(pg->sides());
        const double start = qDegreesToRadians(pg->rotationDeg());
        for (int i = 0; i < pg->sides(); ++i) {
            const double a = start + 2.0 * kCadPi * static_cast<double>(i) / static_cast<double>(pg->sides());
            pts.append(pg->center() + QPointF(std::cos(a) * pg->radius(), std::sin(a) * pg->radius()));
        }
        for (int i = 1; i < pts.size(); ++i) lines.append(QLineF(pts.at(i - 1), pts.at(i)));
        if (pts.size() > 2) lines.append(QLineF(pts.last(), pts.first()));
        return true;
    }
    default:
        return false;
    }
}
bool isBatchableEntityType(CadEntity::Type t)
{
    return t == CadEntity::Type::Line ||
           t == CadEntity::Type::Circle ||
           t == CadEntity::Type::Rectangle ||
           t == CadEntity::Type::Polyline ||
           t == CadEntity::Type::Arc ||
           t == CadEntity::Type::Ellipse ||
           t == CadEntity::Type::Polygon;
}

QRectF rectFromJson(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 4) return QRectF(0.0, 0.0, 420.0, 297.0);
    return QRectF(a.at(0).toDouble(), a.at(1).toDouble(),
                  a.at(2).toDouble(), a.at(3).toDouble()).normalized();
}
}

CadDocument::CadDocument()
{
    clear();
    m_modified = false;
}

void CadDocument::clear()
{
    m_entities.clear();
    m_blockDefinitions.clear();
    m_layers.clear();

    CadLayer base;
    base.name = "0";
    base.color = Qt::white;
    base.visible = true;
    m_layers.insert(base.name, base);

    m_currentLayerName = base.name;
    m_unit = Unit::Millimeter;
    m_limits = QRectF(0.0, 0.0, 420.0, 297.0);
    m_modified = true;
}

void CadDocument::setUnit(Unit unit)
{
    m_unit = unit;
    m_modified = true;
}

QString CadDocument::unitName() const
{
    return unitName(m_unit);
}

QString CadDocument::unitName(Unit unit)
{
    switch (unit) {
    case Unit::Millimeter: return "mm";
    case Unit::Centimeter: return "cm";
    case Unit::Meter:      return "m";
    case Unit::Unitless:   return "unitless";
    }
    return "mm";
}

CadDocument::Unit CadDocument::unitFromName(const QString& name)
{
    if (name == "cm") return Unit::Centimeter;
    if (name == "m") return Unit::Meter;
    if (name == "unitless") return Unit::Unitless;
    return Unit::Millimeter;
}

void CadDocument::setLimits(const QRectF& limits)
{
    m_limits = limits.normalized();
    m_modified = true;
}

void CadDocument::setCurrentLayerName(const QString& name)
{
    ensureLayer(name);
    m_currentLayerName = name;
    m_modified = true;
}

void CadDocument::ensureLayer(const QString& name)
{
    const QString cleanName = name.trimmed().isEmpty() ? QStringLiteral("0") : name.trimmed();
    if (m_layers.contains(cleanName)) return;

    CadLayer layer;
    layer.name = cleanName;
    layer.color = Qt::white;
    layer.visible = true;
    m_layers.insert(cleanName, layer);
    m_modified = true;
}

CadLayer CadDocument::currentLayer() const
{
    return m_layers.value(m_currentLayerName, m_layers.value("0"));
}

bool CadDocument::createLayer(const QString& name)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || m_layers.contains(cleanName)) return false;

    CadLayer layer;
    layer.name = cleanName;
    layer.color = Qt::white;
    layer.visible = true;
    layer.locked = false;
    layer.lineWeight = 0.0;
    layer.lineType = "Continuous";
    m_layers.insert(cleanName, layer);
    m_modified = true;
    return true;
}

bool CadDocument::renameLayer(const QString& oldName, const QString& newName)
{
    const QString cleanOld = oldName.trimmed();
    const QString cleanNew = newName.trimmed();
    if (cleanOld.isEmpty() || cleanNew.isEmpty()) return false;
    if (cleanOld == "0" || cleanOld == cleanNew) return false;
    if (!m_layers.contains(cleanOld) || m_layers.contains(cleanNew)) return false;

    CadLayer layer = m_layers.take(cleanOld);
    layer.name = cleanNew;
    m_layers.insert(cleanNew, layer);

    for (auto& entity : m_entities) {
        if (entity && entity->layer() == cleanOld) entity->setLayer(cleanNew);
    }
    if (m_currentLayerName == cleanOld) m_currentLayerName = cleanNew;
    m_modified = true;
    return true;
}

bool CadDocument::deleteLayer(const QString& name, QString* error)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || cleanName == "0") {
        if (error) *error = "Layer 0 cannot be deleted.";
        return false;
    }
    if (!m_layers.contains(cleanName)) {
        if (error) *error = "Layer not found.";
        return false;
    }

    for (auto& entity : m_entities) {
        if (entity && entity->layer() == cleanName) entity->setLayer("0");
    }
    m_layers.remove(cleanName);
    if (m_currentLayerName == cleanName) m_currentLayerName = "0";
    ensureLayer("0");
    m_modified = true;
    return true;
}

void CadDocument::setLayerVisible(const QString& name, bool visible)
{
    ensureLayer(name);
    m_layers[name].visible = visible;
    m_modified = true;
}

void CadDocument::setLayerLocked(const QString& name, bool locked)
{
    ensureLayer(name);
    m_layers[name].locked = locked;
    m_modified = true;
}

void CadDocument::setLayerColor(const QString& name, const QColor& color, bool applyToExistingEntities)
{
    if (!color.isValid()) return;
    ensureLayer(name);
    m_layers[name].color = color;
    if (applyToExistingEntities) {
        for (auto& entity : m_entities) {
            if (entity && entity->layer() == name) entity->setColor(color);
        }
    }
    m_modified = true;
}

void CadDocument::setLayerLineWeight(const QString& name, double lineWeight, bool applyToExistingEntities)
{
    ensureLayer(name);
    m_layers[name].lineWeight = qMax(0.0, lineWeight);
    if (applyToExistingEntities) {
        for (auto& entity : m_entities) {
            if (entity && entity->layer() == name) entity->setLineWeight(m_layers[name].lineWeight);
        }
    }
    m_modified = true;
}

void CadDocument::setLayerLineType(const QString& name, const QString& lineType, bool applyToExistingEntities)
{
    ensureLayer(name);
    const QString cleanLineType = lineType.trimmed().isEmpty() ? QStringLiteral("Continuous") : lineType.trimmed();
    m_layers[name].lineType = cleanLineType;
    if (applyToExistingEntities) {
        for (auto& entity : m_entities) {
            if (entity && entity->layer() == name) entity->setLineType(cleanLineType);
        }
    }
    m_modified = true;
}

void CadDocument::moveEntitiesToLayer(const QVector<int>& indices, const QString& layerName)
{
    ensureLayer(layerName);
    const CadLayer layer = m_layers.value(layerName, m_layers.value("0"));
    for (int index : indices) {
        CadEntity* entity = entityAt(index);
        if (!entity) continue;
        entity->setLayer(layer.name);
        entity->setColor(layer.color);
        entity->setLineWeight(layer.lineWeight);
        entity->setLineType(layer.lineType);
        m_modified = true;
    }
}

int CadDocument::entityCount() const
{
    return static_cast<int>(m_entities.size());
}

CadEntity* CadDocument::entityAt(int index)
{
    if (index < 0 || index >= entityCount()) return nullptr;
    return m_entities[static_cast<size_t>(index)].get();
}

const CadEntity* CadDocument::entityAt(int index) const
{
    if (index < 0 || index >= entityCount()) return nullptr;
    return m_entities[static_cast<size_t>(index)].get();
}

void CadDocument::reserveEntities(size_t count)
{
    if (count > m_entities.capacity()) m_entities.reserve(count);
}

void CadDocument::addEntity(std::unique_ptr<CadEntity> entity)
{
    if (!entity) return;

    ensureLayer(entity->layer());
    m_entities.push_back(std::move(entity));
    m_modified = true;
}

void CadDocument::removeEntities(const QVector<int>& indices)
{
    QVector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    int last = -1;
    for (int index : sorted) {
        if (index == last) continue;
        last = index;
        if (index < 0 || index >= entityCount()) continue;
        m_entities.erase(m_entities.begin() + index);
        m_modified = true;
    }
}

void CadDocument::duplicateEntities(const QVector<int>& indices, const QPointF& offset)
{
    std::vector<std::unique_ptr<CadEntity>> copies;
    for (int index : indices) {
        const CadEntity* entity = entityAt(index);
        if (!entity) continue;
        auto copy = entity->clone();
        if (!copy) continue;
        copy->translate(offset);
        copies.push_back(std::move(copy));
    }
    for (auto& copy : copies) {
        ensureLayer(copy->layer());
        m_entities.push_back(std::move(copy));
        m_modified = true;
    }
}

void CadDocument::translateEntities(const QVector<int>& indices, const QPointF& delta)
{
    for (int index : indices) {
        if (CadEntity* entity = entityAt(index)) {
            entity->translate(delta);
            m_modified = true;
        }
    }
}

void CadDocument::rotateEntities(const QVector<int>& indices, const QPointF& center, double angleDeg)
{
    for (int index : indices) {
        if (CadEntity* entity = entityAt(index)) {
            entity->rotate(center, angleDeg);
            m_modified = true;
        }
    }
}

void CadDocument::scaleEntities(const QVector<int>& indices, const QPointF& center, double factor)
{
    for (int index : indices) {
        if (CadEntity* entity = entityAt(index)) {
            entity->scale(center, factor);
            m_modified = true;
        }
    }
}

void CadDocument::mirrorEntities(const QVector<int>& indices, const QPointF& axisA, const QPointF& axisB)
{
    for (int index : indices) {
        if (CadEntity* entity = entityAt(index)) {
            entity->mirror(axisA, axisB);
            m_modified = true;
        }
    }
}


QStringList CadDocument::blockNames() const
{
    QStringList names = m_blockDefinitions.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

int CadDocument::blockReferenceCount(const QString& name) const
{
    const QString cleanName = name.trimmed();
    int count = 0;
    for (const auto& entity : m_entities) {
        const auto* ref = dynamic_cast<const CadBlockReference*>(entity.get());
        if (!ref) continue;
        if (cleanName.isEmpty() || ref->blockName().compare(cleanName, Qt::CaseInsensitive) == 0) ++count;
    }
    return count;
}

int CadDocument::blockDefinitionEntityCount(const QString& name) const
{
    const QString cleanName = name.trimmed();
    if (!m_blockDefinitions.contains(cleanName)) return 0;
    return m_blockDefinitions.value(cleanName).value("entities").toArray().size();
}

QString CadDocument::blockDefinitionSummary(const QString& name) const
{
    const QString cleanName = name.trimmed();
    if (!m_blockDefinitions.contains(cleanName)) return QString();
    return QStringLiteral("%1: %2 object(s), %3 reference(s)")
        .arg(cleanName)
        .arg(blockDefinitionEntityCount(cleanName))
        .arg(blockReferenceCount(cleanName));
}

bool CadDocument::createEmptyBlock(const QString& name, const QPointF& basePoint, QString* error)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) {
        if (error) *error = QStringLiteral("Block name is empty.");
        return false;
    }
    if (m_blockDefinitions.contains(cleanName)) {
        if (error) *error = QStringLiteral("A block with this name already exists.");
        return false;
    }

    QJsonObject blockDefinition;
    blockDefinition["name"] = cleanName;
    blockDefinition["basePoint"] = rectToJson(QRectF(basePoint, QSizeF(0.0, 0.0)));
    blockDefinition["entities"] = QJsonArray();
    m_blockDefinitions.insert(cleanName, blockDefinition);
    m_modified = true;
    return true;
}

bool CadDocument::upsertBlockDefinition(const QString& name, const QPointF& basePoint, const QJsonArray& entities, bool replaceExisting, QString* error)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) {
        if (error) *error = QStringLiteral("Block name is empty.");
        return false;
    }
    if (!replaceExisting && m_blockDefinitions.contains(cleanName)) {
        if (error) *error = QStringLiteral("A block with this name already exists.");
        return false;
    }

    QJsonObject blockDefinition;
    blockDefinition["name"] = cleanName;
    blockDefinition["basePoint"] = rectToJson(QRectF(basePoint, QSizeF(0.0, 0.0)));
    blockDefinition["entities"] = entities;
    m_blockDefinitions.insert(cleanName, blockDefinition);
    m_modified = true;
    return true;
}

bool CadDocument::createBlockFromEntities(const QString& name, const QVector<int>& indices, const QPointF& basePoint, QString* error)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) {
        if (error) *error = "Block name is empty.";
        return false;
    }
    if (m_blockDefinitions.contains(cleanName)) {
        if (error) *error = "A block with this name already exists.";
        return false;
    }
    if (indices.isEmpty()) {
        if (error) *error = "No object selected.";
        return false;
    }

    QJsonArray definition;
    QVector<int> valid;
    for (int index : indices) {
        const CadEntity* entity = entityAt(index);
        if (!entity) continue;
        definition.append(entity->toJson());
        valid.append(index);
    }
    if (definition.isEmpty()) {
        if (error) *error = "The selection does not contain any valid CAD object.";
        return false;
    }

    QJsonObject blockDefinition;
    blockDefinition["name"] = cleanName;
    blockDefinition["basePoint"] = rectToJson(QRectF(basePoint, QSizeF(0.0, 0.0)));
    blockDefinition["entities"] = definition;
    m_blockDefinitions.insert(cleanName, blockDefinition);
    removeEntities(valid);

    auto reference = std::make_unique<CadBlockReference>(cleanName, basePoint, basePoint, definition, 1.0, 0.0);
    const CadLayer layer = currentLayer();
    reference->setLayer(layer.name);
    reference->setColor(layer.color);
    reference->setLineWeight(layer.lineWeight);
    reference->setLineType(layer.lineType);
    m_entities.push_back(std::move(reference));
    m_modified = true;
    return true;
}

bool CadDocument::insertBlockReference(const QString& name, const QPointF& insertionPoint, double scaleFactor, double rotationDeg, QString* error)
{
    const QString cleanName = name.trimmed();
    if (!m_blockDefinitions.contains(cleanName)) {
        if (error) *error = "Block not found.";
        return false;
    }

    const QJsonObject blockDefinition = m_blockDefinitions.value(cleanName);
    const QRectF baseRect = rectFromJson(blockDefinition.value("basePoint"));
    const QPointF basePoint = baseRect.topLeft();
    auto reference = std::make_unique<CadBlockReference>(cleanName, basePoint, insertionPoint,
                                                        blockDefinition.value("entities").toArray(), scaleFactor, rotationDeg);
    const CadLayer layer = currentLayer();
    reference->setLayer(layer.name);
    reference->setColor(layer.color);
    reference->setLineWeight(layer.lineWeight);
    reference->setLineType(layer.lineType);
    m_entities.push_back(std::move(reference));
    m_modified = true;
    return true;
}

bool CadDocument::renameBlockDefinition(const QString& oldName, const QString& newName, QString* error)
{
    const QString oldClean = oldName.trimmed();
    const QString newClean = newName.trimmed();
    if (oldClean.isEmpty() || newClean.isEmpty()) {
        if (error) *error = QStringLiteral("Block name is empty.");
        return false;
    }
    if (!m_blockDefinitions.contains(oldClean)) {
        if (error) *error = QStringLiteral("Block not found.");
        return false;
    }
    if (m_blockDefinitions.contains(newClean)) {
        if (error) *error = QStringLiteral("A block with the new name already exists.");
        return false;
    }

    QJsonObject definition = m_blockDefinitions.take(oldClean);
    definition["name"] = newClean;
    m_blockDefinitions.insert(newClean, definition);

    const QJsonArray entities = definition.value("entities").toArray();
    const QRectF baseRect = rectFromJson(definition.value("basePoint"));
    const QPointF basePoint = baseRect.topLeft();

    for (auto& entity : m_entities) {
        auto* ref = dynamic_cast<CadBlockReference*>(entity.get());
        if (!ref || ref->blockName().compare(oldClean, Qt::CaseInsensitive) != 0) continue;

        auto replacement = std::make_unique<CadBlockReference>(newClean,
                                                              basePoint,
                                                              ref->insertionPoint(),
                                                              entities,
                                                              ref->scaleFactor(),
                                                              ref->rotationDeg());
        ref->copyStyleTo(*replacement);
        entity = std::move(replacement);
    }

    m_modified = true;
    return true;
}

bool CadDocument::duplicateBlockDefinition(const QString& sourceName, const QString& newName, QString* error)
{
    const QString sourceClean = sourceName.trimmed();
    const QString newClean = newName.trimmed();
    if (sourceClean.isEmpty() || newClean.isEmpty()) {
        if (error) *error = QStringLiteral("Block name is empty.");
        return false;
    }
    if (!m_blockDefinitions.contains(sourceClean)) {
        if (error) *error = QStringLiteral("Source block not found.");
        return false;
    }
    if (m_blockDefinitions.contains(newClean)) {
        if (error) *error = QStringLiteral("A block with the new name already exists.");
        return false;
    }

    QJsonObject definition = m_blockDefinitions.value(sourceClean);
    definition["name"] = newClean;
    m_blockDefinitions.insert(newClean, definition);
    m_modified = true;
    return true;
}

bool CadDocument::removeBlockDefinition(const QString& name, QString* error)
{
    const QString cleanName = name.trimmed();
    if (!m_blockDefinitions.contains(cleanName)) {
        if (error) *error = QStringLiteral("Block not found.");
        return false;
    }
    const int refs = blockReferenceCount(cleanName);
    if (refs > 0) {
        if (error) *error = QStringLiteral("Block is still referenced %1 time(s). Explode or delete its references first.").arg(refs);
        return false;
    }
    m_blockDefinitions.remove(cleanName);
    m_modified = true;
    return true;
}

int CadDocument::purgeUnusedBlockDefinitions(QStringList* purgedNames)
{
    QStringList purged;
    const QStringList names = blockNames();
    for (const QString& name : names) {
        if (blockReferenceCount(name) == 0) {
            m_blockDefinitions.remove(name);
            purged.append(name);
        }
    }
    if (purgedNames) *purgedNames = purged;
    if (!purged.isEmpty()) m_modified = true;
    return purged.size();
}

int CadDocument::explodeBlockReferences(const QVector<int>& indices, QString* error)
{
    if (indices.isEmpty()) {
        if (error) *error = QStringLiteral("No block reference selected.");
        return 0;
    }

    QVector<int> toRemove;
    std::vector<std::unique_ptr<CadEntity>> exploded;

    for (int index : indices) {
        const auto* ref = dynamic_cast<const CadBlockReference*>(entityAt(index));
        if (!ref) continue;

        const QJsonArray definition = ref->definitionEntities();
        if (definition.isEmpty()) {
            toRemove.append(index);
            continue;
        }

        for (const QJsonValue& value : definition) {
            std::unique_ptr<CadEntity> child = CadEntity::fromJson(value.toObject());
            if (!child) continue;

            // CadBlockReference::createGraphicsItem() renders each child in local
            // coordinates by subtracting the base point, then applying block scale,
            // rotation and insertion. Apply the same transform to make EXPLODE
            // produce visible world-space entities.
            child->translate(-ref->basePoint());
            if (std::abs(ref->scaleFactor() - 1.0) > 1.0e-12)
                child->scale(QPointF(0.0, 0.0), ref->scaleFactor());
            if (std::abs(ref->rotationDeg()) > 1.0e-12)
                child->rotate(QPointF(0.0, 0.0), -ref->rotationDeg());
            child->translate(ref->insertionPoint());
            exploded.push_back(std::move(child));
        }
        toRemove.append(index);
    }

    if (toRemove.isEmpty()) {
        if (error) *error = QStringLiteral("The selection does not contain a block reference.");
        return 0;
    }

    removeEntities(toRemove);
    const int count = static_cast<int>(exploded.size());
    for (auto& entity : exploded) m_entities.push_back(std::move(entity));
    m_modified = true;
    return count > 0 ? count : toRemove.size();
}

void CadDocument::renderToScene(QGraphicsScene* scene) const
{
    if (!scene) return;

    const QList<QGraphicsView*> views = scene->views();
    QVector<bool> oldViewUpdates;
    oldViewUpdates.reserve(views.size());
    for (QGraphicsView* view : views) {
        if (!view) continue;
        oldViewUpdates.append(view->viewport()->updatesEnabled());
        view->viewport()->setUpdatesEnabled(false);
    }

    const bool oldSceneSignalsBlocked = scene->signalsBlocked();
    scene->blockSignals(true);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    scene->clear();
    scene->setSceneRect(m_limits.adjusted(-20.0, -20.0, 20.0, 20.0));

    const int totalEntities = entityCount();
    const bool largeDocument = totalEntities > 10000;
    const bool proBatchMode = totalEntities > 50000;

    // Mode proche CAD pro pour fichiers enormes : les entites geometriques
    // simples sont regroupees par style en gros QPainterPath. Cela reduit
    // fortement le nombre de QGraphicsItem, donc moins de RAM et un pan/zoom
    // beaucoup plus fluide. Les annotations/blocs restent des items separes.
    QHash<QString, QVector<QLineF>> batchLines;
    QHash<QString, QPen> batchPens;
    QHash<QString, QString> batchLayers;
    QHash<QString, int> batchElementCounts;
    QHash<QString, int> batchChunkCounts;
    const double batchTileSize = recommendedTileSize(m_limits, totalEntities);
    // Performance rollback: les chunks trop petits créaient 10 à 20 fois plus
    // de QGraphicsItem batchés que l'ancienne version. Le coût de l'index BSP,
    // du paint dispatch et de la mémoire dépassait le gain de culling. On revient
    // aux tailles larges de la version stable.
    const int maxElementsPerBatchPath = totalEntities > 1000000 ? 2500 : (totalEntities > 300000 ? 4000 : 8000);
    if (proBatchMode) batchLines.reserve(qMin(totalEntities / 8, 120000));

    for (int i = 0; i < totalEntities; ++i) {
        const auto& entity = m_entities[static_cast<size_t>(i)];
        if (!entity) continue;
        const CadLayer layer = m_layers.value(entity->layer(), m_layers.value("0"));
        if (!layer.visible) continue;

        if (proBatchMode && isBatchableEntityType(entity->type())) {
            const QString baseKey = tileKeyForEntity(entity.get(), m_limits, batchTileSize);
            const int n = batchElementCounts.value(baseKey, 0);
            const int chunk = maxElementsPerBatchPath > 0 ? (n / maxElementsPerBatchPath) : 0;
            const QString key = baseKey + QStringLiteral("|chunk:%1").arg(chunk);
            if (appendEntityToBatchLines(entity.get(), batchLines[key])) {
                batchElementCounts.insert(baseKey, n + 1);
                batchChunkCounts.insert(key, batchChunkCounts.value(key, 0) + 1);
                if (!batchPens.contains(key)) batchPens.insert(key, fastPenForEntity(entity.get()));
                if (!batchLayers.contains(key)) batchLayers.insert(key, entity->layer());
                continue;
            }
        }

        QGraphicsItem* item = nullptr;
        if (largeDocument && entity->type() == CadEntity::Type::Text) {
            item = new LodCadTextItem(dynamic_cast<const CadText*>(entity.get()), layer);
        } else if (largeDocument && entity->type() == CadEntity::Type::Hatch) {
            item = new LodCadHatchItem(dynamic_cast<const CadHatch*>(entity.get()), layer);
        } else {
            item = entity->createGraphicsItem();
        }
        if (!item) continue;

        // Classification LOD utilisee par GraphicsView::updateLevelOfDetail().
        // Texts et cotations restent en classe 0 pour ne pas disparaitre
        // apres ouverture d'un gros DXF/DWG; seuls les details tres lourds
        // comme hachures et blocs sont masques lors des vues tres dezoomees.
        int detailClass = 0;
        switch (entity->type()) {
        case CadEntity::Type::Text: detailClass = 1; break;
        case CadEntity::Type::Hatch: detailClass = 2; break;
        case CadEntity::Type::LinearDimension:
        case CadEntity::Type::Leader: detailClass = 1; break;
        case CadEntity::Type::BlockReference: detailClass = 3; break;
        default: detailClass = 0; break;
        }

        item->setCacheMode(QGraphicsItem::NoCache);
        item->setData(1, i);
        item->setData(2, layer.locked);
        item->setData(3, detailClass);
        if (layer.locked) {
            item->setFlag(QGraphicsItem::ItemIsSelectable, false);
            item->setOpacity(0.65);
        }
        scene->addItem(item);

        if (largeDocument && (i % 2000) == 0) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    if (proBatchMode) {
        int addedBatches = 0;
        for (auto it = batchLines.constBegin(); it != batchLines.constEnd(); ++it) {
            if (it.value().isEmpty()) continue;
            auto* item = new FastCadBatchItem(it.value(), batchPens.value(it.key()), batchLayers.value(it.key()));
            scene->addItem(item);
            ++addedBatches;
            if ((addedBatches % 1000) == 0) {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
        }
    }

    scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    if (totalEntities > 1000000) scene->setBspTreeDepth(22);
    else if (totalEntities > 500000) scene->setBspTreeDepth(20);
    else if (totalEntities > 150000) scene->setBspTreeDepth(18);
    else if (totalEntities > 50000) scene->setBspTreeDepth(16);
    else if (totalEntities > 10000) scene->setBspTreeDepth(12);
    else scene->setBspTreeDepth(8);

    scene->blockSignals(oldSceneSignalsBlocked);
    for (int i = 0; i < views.size() && i < oldViewUpdates.size(); ++i) {
        if (views.at(i)) views.at(i)->viewport()->setUpdatesEnabled(oldViewUpdates.at(i));
    }
}

QJsonObject CadDocument::toJson() const
{
    QJsonObject root;
    root["format"] = "DWGViewerAdvancedCadDocument";
    root["version"] = 1;
    root["unit"] = unitName();
    root["limits"] = rectToJson(m_limits);
    root["currentLayer"] = m_currentLayerName;

    QJsonArray layers;
    for (const CadLayer& layer : m_layers) layers.append(layer.toJson());
    root["layers"] = layers;

    QJsonArray blocks;
    for (auto it = m_blockDefinitions.constBegin(); it != m_blockDefinitions.constEnd(); ++it) {
        QJsonObject block = it.value();
        block["name"] = it.key();
        blocks.append(block);
    }
    root["blocks"] = blocks;

    QJsonArray entities;
    for (const auto& entity : m_entities) {
        if (entity) entities.append(entity->toJson());
    }
    root["entities"] = entities;
    return root;
}

bool CadDocument::fromJson(const QJsonObject& obj, QString* error)
{
    if (obj.value("format").toString() != "DWGViewerAdvancedCadDocument") {
        if (error) *error = "Unrecognized CAD project format.";
        return false;
    }

    clear();
    m_entities.clear();
    m_blockDefinitions.clear();
    m_layers.clear();

    m_unit = unitFromName(obj.value("unit").toString("mm"));
    m_limits = rectFromJson(obj.value("limits"));

    const QJsonArray layers = obj.value("layers").toArray();
    for (const QJsonValue& v : layers) {
        const CadLayer layer = CadLayer::fromJson(v.toObject());
        if (!layer.name.trimmed().isEmpty()) m_layers.insert(layer.name, layer);
    }
    if (m_layers.isEmpty()) ensureLayer("0");

    m_currentLayerName = obj.value("currentLayer").toString("0");
    ensureLayer(m_currentLayerName);

    const QJsonArray blocks = obj.value("blocks").toArray();
    for (const QJsonValue& v : blocks) {
        const QJsonObject block = v.toObject();
        const QString name = block.value("name").toString().trimmed();
        if (!name.isEmpty()) m_blockDefinitions.insert(name, block);
    }

    const QJsonArray entities = obj.value("entities").toArray();
    for (const QJsonValue& v : entities) {
        auto entity = CadEntity::fromJson(v.toObject());
        if (entity) m_entities.push_back(std::move(entity));
    }

    m_modified = false;
    return true;
}

bool CadDocument::saveToFile(const QString& path, QString* error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }

    const QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool CadDocument::loadFromFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = parseError.errorString();
        return false;
    }

    return fromJson(doc.object(), error);
}

bool CadDocument::saveAsDxf(const QString& path, QString* error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Cannot open DXF for writing: %1").arg(file.errorString());
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    dxfPair(out, 0, QStringLiteral("SECTION"));
    dxfPair(out, 2, QStringLiteral("HEADER"));
    dxfPair(out, 9, QStringLiteral("$ACADVER"));
    dxfPair(out, 1, QStringLiteral("AC1015"));
    dxfPair(out, 9, QStringLiteral("$INSUNITS"));
    int insUnits = 0;
    switch (m_unit) {
    case Unit::Millimeter: insUnits = 4; break;
    case Unit::Centimeter: insUnits = 5; break;
    case Unit::Meter: insUnits = 6; break;
    case Unit::Unitless: default: insUnits = 0; break;
    }
    dxfPair(out, 70, insUnits);
    dxfPair(out, 0, QStringLiteral("ENDSEC"));

    dxfPair(out, 0, QStringLiteral("SECTION"));
    dxfPair(out, 2, QStringLiteral("TABLES"));

    dxfPair(out, 0, QStringLiteral("TABLE"));
    dxfPair(out, 2, QStringLiteral("LTYPE"));
    dxfPair(out, 70, 1);
    dxfPair(out, 0, QStringLiteral("LTYPE"));
    dxfPair(out, 2, QStringLiteral("Continuous"));
    dxfPair(out, 70, 0);
    dxfPair(out, 3, QStringLiteral("Solid line"));
    dxfPair(out, 72, 65);
    dxfPair(out, 73, 0);
    dxfPair(out, 40, 0.0);
    dxfPair(out, 0, QStringLiteral("ENDTAB"));

    dxfPair(out, 0, QStringLiteral("TABLE"));
    dxfPair(out, 2, QStringLiteral("LAYER"));
    dxfPair(out, 70, int(m_layers.size()));
    for (auto it = m_layers.constBegin(); it != m_layers.constEnd(); ++it) {
        const CadLayer& layer = it.value();
        dxfPair(out, 0, QStringLiteral("LAYER"));
        dxfPair(out, 2, safeDxfLayerName(layer.name));
        dxfPair(out, 70, layer.locked ? 4 : 0);
        dxfPair(out, 62, layer.visible ? 7 : -7);
        dxfPair(out, 420, rgbTrueColor(layer.color));
        dxfPair(out, 6, layer.lineType.trimmed().isEmpty() ? QStringLiteral("Continuous") : layer.lineType.trimmed());
        if (layer.lineWeight > 0.0)
            dxfPair(out, 370, qBound(0, int(std::round(layer.lineWeight * 100.0)), 211));
    }
    dxfPair(out, 0, QStringLiteral("ENDTAB"));
    dxfPair(out, 0, QStringLiteral("ENDSEC"));

    writeDxfBlocksSection(out, m_blockDefinitions);

    dxfPair(out, 0, QStringLiteral("SECTION"));
    dxfPair(out, 2, QStringLiteral("ENTITIES"));
    int exported = 0;
    for (const auto& entity : m_entities) {
        if (!entity) continue;
        writeDxfEntity(out, *entity);
        ++exported;
    }
    dxfPair(out, 0, QStringLiteral("ENDSEC"));
    dxfPair(out, 0, QStringLiteral("EOF"));

    if (out.status() != QTextStream::Ok) {
        if (error) *error = QStringLiteral("Native DXF stream write error.");
        return false;
    }

    if (error) *error = QStringLiteral("%1 entities exported with the DWGView native DXF writer.").arg(exported);
    return true;
}

