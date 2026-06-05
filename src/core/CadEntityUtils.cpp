#include "core/CadEntityUtils.h"
#include "geometry/GeometryKernel.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <limits>

namespace CadCore {
namespace {

static QRectF normalizedRectFromPoints(const QVector<QPointF>& points)
{
    if (points.isEmpty()) {
        return QRectF();
    }
    double minX = points.first().x();
    double minY = points.first().y();
    double maxX = minX;
    double maxY = minY;
    for (const QPointF& p : points) {
        minX = qMin(minX, p.x());
        minY = qMin(minY, p.y());
        maxX = qMax(maxX, p.x());
        maxY = qMax(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

static QVector<QPointF> textBox(const CadText& text)
{
    const double width = qMax(1.0, text.text().size() * text.height() * 0.6 * text.widthFactor());
    const double height = qMax(1.0, text.height());
    QVector<QPointF> pts;
    pts << text.position()
        << text.position() + QPointF(width, 0.0)
        << text.position() + QPointF(width, height)
        << text.position() + QPointF(0.0, height);
    if (!qFuzzyIsNull(text.rotationDeg())) {
        for (QPointF& p : pts) {
            p = CadGeometry::rotatePoint(p, text.position(), text.rotationDeg());
        }
    }
    return pts;
}

static void appendPolylineSegments(QVector<QPointF>& out, const QVector<QPointF>& points, bool closed)
{
    for (const QPointF& p : points) {
        out.push_back(p);
    }
    if (closed && !points.isEmpty()) {
        out.push_back(points.first());
    }
}

} // namespace

QString entityTypeName(CadEntity::Type type)
{
    switch (type) {
    case CadEntity::Type::Line: return QStringLiteral("Line");
    case CadEntity::Type::Circle: return QStringLiteral("Circle");
    case CadEntity::Type::Rectangle: return QStringLiteral("Rectangle");
    case CadEntity::Type::Polyline: return QStringLiteral("Polyline");
    case CadEntity::Type::Arc: return QStringLiteral("Arc");
    case CadEntity::Type::Ellipse: return QStringLiteral("Ellipse");
    case CadEntity::Type::Polygon: return QStringLiteral("Polygon");
    case CadEntity::Type::Text: return QStringLiteral("Text");
    case CadEntity::Type::LinearDimension: return QStringLiteral("LinearDimension");
    case CadEntity::Type::Leader: return QStringLiteral("Leader");
    case CadEntity::Type::Hatch: return QStringLiteral("Hatch");
    case CadEntity::Type::BlockReference: return QStringLiteral("BlockReference");
    }
    return QStringLiteral("Unknown");
}

QString entityTypeName(const CadEntity& entity)
{
    return entityTypeName(entity.type());
}

QVector<QPointF> representativePoints(const CadEntity& entity, int curveSegments)
{
    curveSegments = qBound(8, curveSegments, 720);
    QVector<QPointF> pts;

    if (const auto* line = dynamic_cast<const CadLine*>(&entity)) {
        pts << line->start() << line->end();
    } else if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
        for (int i = 0; i < curveSegments; ++i) {
            pts << CadGeometry::pointOnCadCircle(circle->center(), circle->radius(), 360.0 * i / curveSegments);
        }
    } else if (const auto* rect = dynamic_cast<const CadRectangle*>(&entity)) {
        pts = CadGeometry::rectangleCorners(rect->rect());
        if (!pts.isEmpty()) pts << pts.first();
    } else if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) {
        appendPolylineSegments(pts, poly->points(), poly->closed());
    } else if (const auto* arc = dynamic_cast<const CadArc*>(&entity)) {
        pts = CadGeometry::approximateArc(arc->center(), arc->radius(), arc->startAngleDeg(), arc->spanAngleDeg(),
                                          360.0 / curveSegments);
    } else if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
        pts = CadGeometry::approximateEllipse(ellipse->rect(), curveSegments);
        if (!pts.isEmpty()) pts << pts.first();
    } else if (const auto* polygon = dynamic_cast<const CadPolygon*>(&entity)) {
        pts = CadGeometry::regularPolygonPoints(polygon->center(), polygon->radius(), polygon->sides(), polygon->rotationDeg());
        if (!pts.isEmpty()) pts << pts.first();
    } else if (const auto* text = dynamic_cast<const CadText*>(&entity)) {
        pts = textBox(*text);
    } else if (const auto* dim = dynamic_cast<const CadLinearDimension*>(&entity)) {
        pts << dim->first() << dim->second() << dim->dimensionPoint();
    } else if (const auto* leader = dynamic_cast<const CadLeader*>(&entity)) {
        pts << leader->arrowPoint() << leader->textPoint();
    } else if (const auto* hatch = dynamic_cast<const CadHatch*>(&entity)) {
        if (!hatch->loops().isEmpty()) {
            for (const QVector<QPointF>& loop : hatch->loops()) {
                appendPolylineSegments(pts, loop, true);
            }
        } else {
            appendPolylineSegments(pts, hatch->boundary(), true);
        }
    } else if (const auto* block = dynamic_cast<const CadBlockReference*>(&entity)) {
        pts << block->insertionPoint() << block->basePoint();
    }

    return pts;
}

QRectF entityBoundingRect(const CadEntity& entity, int curveSegments)
{
    if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
        return CadGeometry::circleBoundingRect(circle->center(), circle->radius());
    }
    if (const auto* arc = dynamic_cast<const CadArc*>(&entity)) {
        return CadGeometry::arcBoundingRect(arc->center(), arc->radius(), arc->startAngleDeg(), arc->spanAngleDeg());
    }
    if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
        return ellipse->rect().normalized();
    }
    return normalizedRectFromPoints(representativePoints(entity, curveSegments));
}

double entityLength(const CadEntity& entity, int curveSegments)
{
    if (const auto* line = dynamic_cast<const CadLine*>(&entity)) {
        return CadGeometry::distance(line->start(), line->end());
    }
    if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
        return 2.0 * M_PI * qAbs(circle->radius());
    }
    if (const auto* arc = dynamic_cast<const CadArc*>(&entity)) {
        return qAbs(arc->radius() * qDegreesToRadians(arc->spanAngleDeg()));
    }
    if (const auto* dim = dynamic_cast<const CadLinearDimension*>(&entity)) {
        return dim->measuredLength();
    }
    const QVector<QPointF> pts = representativePoints(entity, curveSegments);
    return CadGeometry::polylineLength(pts, false);
}

double entityArea(const CadEntity& entity, int curveSegments)
{
    if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
        return M_PI * circle->radius() * circle->radius();
    }
    if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
        return M_PI * ellipse->rect().width() * 0.5 * ellipse->rect().height() * 0.5;
    }
    if (const auto* rect = dynamic_cast<const CadRectangle*>(&entity)) {
        return qAbs(rect->rect().width() * rect->rect().height());
    }
    if (const auto* polygon = dynamic_cast<const CadPolygon*>(&entity)) {
        const QVector<QPointF> pts = CadGeometry::regularPolygonPoints(polygon->center(), polygon->radius(), polygon->sides(), polygon->rotationDeg());
        return qAbs(CadGeometry::polygonSignedArea(pts));
    }
    if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) {
        return poly->closed() ? qAbs(CadGeometry::polygonSignedArea(poly->points())) : 0.0;
    }
    if (const auto* hatch = dynamic_cast<const CadHatch*>(&entity)) {
        double area = 0.0;
        if (!hatch->loops().isEmpty()) {
            for (const QVector<QPointF>& loop : hatch->loops()) area += qAbs(CadGeometry::polygonSignedArea(loop));
        } else {
            area = qAbs(CadGeometry::polygonSignedArea(hatch->boundary()));
        }
        return area;
    }
    Q_UNUSED(curveSegments)
    return 0.0;
}

EntityMetrics entityMetrics(const CadEntity& entity, int curveSegments)
{
    EntityMetrics metrics;
    metrics.typeName = entityTypeName(entity);
    metrics.bounds = entityBoundingRect(entity, curveSegments);
    metrics.length = entityLength(entity, curveSegments);
    metrics.area = entityArea(entity, curveSegments);
    const QVector<QPointF> pts = representativePoints(entity, curveSegments);
    metrics.vertexCount = pts.size();
    metrics.closed = metrics.area > 0.0;
    metrics.valid = !isEntityDegenerate(entity);
    return metrics;
}

QRectF documentBoundingRect(const std::vector<std::unique_ptr<CadEntity>>& entities, int curveSegments)
{
    QRectF result;
    bool first = true;
    for (const auto& entity : entities) {
        if (!entity) continue;
        const QRectF rect = entityBoundingRect(*entity, curveSegments);
        if (!rect.isValid() && rect.isNull()) continue;
        result = first ? rect : result.united(rect);
        first = false;
    }
    return result;
}

DrawingStatistics drawingStatistics(const std::vector<std::unique_ptr<CadEntity>>& entities, int curveSegments)
{
    DrawingStatistics stats;
    bool hasBounds = false;
    for (const auto& ptr : entities) {
        if (!ptr) continue;
        const CadEntity& entity = *ptr;
        ++stats.totalEntities;
        switch (entity.type()) {
        case CadEntity::Type::Line: ++stats.lineCount; break;
        case CadEntity::Type::Circle: ++stats.circleCount; break;
        case CadEntity::Type::Arc: ++stats.arcCount; break;
        case CadEntity::Type::Polyline: ++stats.polylineCount; break;
        case CadEntity::Type::Text: ++stats.textCount; break;
        case CadEntity::Type::LinearDimension: ++stats.dimensionCount; break;
        case CadEntity::Type::Hatch: ++stats.hatchCount; break;
        case CadEntity::Type::BlockReference: ++stats.blockReferenceCount; break;
        default: break;
        }
        const QRectF bounds = entityBoundingRect(entity, curveSegments);
        if (!bounds.isNull()) {
            stats.drawingBounds = hasBounds ? stats.drawingBounds.united(bounds) : bounds;
            hasBounds = true;
        }
        stats.totalCurveLength += entityLength(entity, curveSegments);
        stats.totalClosedArea += entityArea(entity, curveSegments);
    }
    return stats;
}

double distanceToEntity(const QPointF& point, const CadEntity& entity, QPointF* nearestPoint, int curveSegments)
{
    QVector<QPointF> pts = representativePoints(entity, curveSegments);
    if (pts.isEmpty()) {
        if (nearestPoint) *nearestPoint = QPointF();
        return std::numeric_limits<double>::infinity();
    }

    if (pts.size() == 1) {
        if (nearestPoint) *nearestPoint = pts.first();
        return CadGeometry::distance(point, pts.first());
    }

    double bestDistance = std::numeric_limits<double>::infinity();
    QPointF bestPoint = pts.first();
    for (int i = 0; i + 1 < pts.size(); ++i) {
        double t = 0.0;
        const QPointF candidate = CadGeometry::nearestPointOnSegment(point, pts[i], pts[i + 1], &t);
        const double d = CadGeometry::distance(point, candidate);
        if (d < bestDistance) {
            bestDistance = d;
            bestPoint = candidate;
        }
    }
    if (nearestPoint) *nearestPoint = bestPoint;
    return bestDistance;
}

bool isEntityDegenerate(const CadEntity& entity, double tolerance)
{
    if (const auto* line = dynamic_cast<const CadLine*>(&entity)) {
        return CadGeometry::distance(line->start(), line->end()) <= tolerance;
    }
    if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
        return circle->radius() <= tolerance;
    }
    if (const auto* arc = dynamic_cast<const CadArc*>(&entity)) {
        return arc->radius() <= tolerance || qAbs(arc->spanAngleDeg()) <= tolerance;
    }
    if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
        return qAbs(ellipse->rect().width()) <= tolerance || qAbs(ellipse->rect().height()) <= tolerance;
    }
    if (const auto* rect = dynamic_cast<const CadRectangle*>(&entity)) {
        return qAbs(rect->rect().width()) <= tolerance || qAbs(rect->rect().height()) <= tolerance;
    }
    if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) {
        return CadGeometry::polylineLength(poly->points(), poly->closed()) <= tolerance;
    }
    if (const auto* polygon = dynamic_cast<const CadPolygon*>(&entity)) {
        return polygon->sides() < 3 || polygon->radius() <= tolerance;
    }
    return false;
}

QJsonObject entitySummaryJson(const CadEntity& entity, int curveSegments)
{
    const EntityMetrics m = entityMetrics(entity, curveSegments);
    QJsonObject obj;
    obj["type"] = m.typeName;
    obj["layer"] = entity.layer();
    obj["color"] = entity.color().name(QColor::HexArgb);
    obj["lineType"] = entity.lineType();
    obj["lineWeight"] = entity.lineWeight();
    obj["curveLength"] = m.length;
    obj["area"] = m.area;
    obj["vertexCount"] = m.vertexCount;
    obj["valid"] = m.valid;
    QJsonObject bounds;
    bounds["x"] = m.bounds.x();
    bounds["y"] = m.bounds.y();
    bounds["width"] = m.bounds.width();
    bounds["height"] = m.bounds.height();
    obj["bounds"] = bounds;
    return obj;
}

} // namespace CadCore
