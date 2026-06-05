#include "CadMeasurementEngine.h"

#include "core/CadEntityUtils.h"
#include "geometry/GeometryKernel.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <limits>

namespace CadMeasure {

namespace {
QJsonArray pointsToJson(const QVector<QPointF>& points)
{
    QJsonArray arr;
    for (const QPointF& p : points) {
        QJsonObject o;
        o["x"] = p.x();
        o["y"] = p.y();
        arr.append(o);
    }
    return arr;
}

QJsonObject rectToJson(const QRectF& r)
{
    QJsonObject o;
    o["x"] = r.x();
    o["y"] = r.y();
    o["width"] = r.width();
    o["height"] = r.height();
    return o;
}

QString kindName(MeasurementKind kind)
{
    switch (kind) {
    case MeasurementKind::Distance: return QStringLiteral("distance");
    case MeasurementKind::Angle: return QStringLiteral("angle");
    case MeasurementKind::Length: return QStringLiteral("length");
    case MeasurementKind::Area: return QStringLiteral("area");
    case MeasurementKind::Perimeter: return QStringLiteral("perimeter");
    case MeasurementKind::Radius: return QStringLiteral("radius");
    case MeasurementKind::Diameter: return QStringLiteral("diameter");
    case MeasurementKind::BoundingBox: return QStringLiteral("bounding_box");
    case MeasurementKind::Coordinate: return QStringLiteral("coordinate");
    default: return QStringLiteral("unknown");
    }
}
}

QJsonObject MeasurementResult::toJson() const
{
    QJsonObject obj;
    obj["kind"] = kindName(kind);
    obj["value"] = value;
    obj["secondaryValue"] = secondaryValue;
    obj["unit"] = unit;
    obj["label"] = label;
    obj["valid"] = valid;
    obj["referencePoints"] = pointsToJson(referencePoints);
    obj["bounds"] = rectToJson(bounds);
    return obj;
}

QJsonObject EntityMeasurement::toJson() const
{
    QJsonObject obj;
    obj["index"] = index;
    obj["entityType"] = entityType;
    obj["layer"] = layer;
    obj["length"] = length;
    obj["area"] = area;
    obj["bounds"] = rectToJson(bounds);
    obj["degenerate"] = degenerate;
    return obj;
}

QJsonObject DrawingMeasurementSummary::toJson() const
{
    QJsonObject obj;
    obj["entityCount"] = entityCount;
    obj["measuredCount"] = measuredCount;
    obj["totalLength"] = totalLength;
    obj["totalClosedArea"] = totalClosedArea;
    obj["extents"] = rectToJson(extents);
    QJsonArray arr;
    for (const EntityMeasurement& item : entities) arr.append(item.toJson());
    obj["entities"] = arr;
    return obj;
}

MeasurementResult CadMeasurementEngine::distance(const QPointF& a, const QPointF& b, const QString& unit)
{
    MeasurementResult r;
    r.kind = MeasurementKind::Distance;
    r.value = QLineF(a, b).length();
    r.unit = unit;
    r.referencePoints = {a, b};
    r.bounds = QRectF(a, b).normalized();
    r.label = formatDistance(r.value, unit);
    r.valid = true;
    return r;
}

MeasurementResult CadMeasurementEngine::angle(const QPointF& a, const QPointF& vertex, const QPointF& b, bool degrees)
{
    MeasurementResult r;
    r.kind = MeasurementKind::Angle;
    const QLineF l1(vertex, a);
    const QLineF l2(vertex, b);
    double value = l1.angleTo(l2);
    if (value > 180.0) value = 360.0 - value;
    r.value = degrees ? value : qDegreesToRadians(value);
    r.unit = degrees ? QStringLiteral("deg") : QStringLiteral("rad");
    r.referencePoints = {a, vertex, b};
    r.bounds = QRectF(a, b).united(QRectF(vertex, QSizeF(0.0, 0.0))).normalized();
    r.label = QStringLiteral("%1 %2").arg(formatNumber(r.value, 3), r.unit);
    r.valid = l1.length() > 1.0e-9 && l2.length() > 1.0e-9;
    return r;
}

MeasurementResult CadMeasurementEngine::coordinate(const QPointF& point, const QString& unit)
{
    MeasurementResult r;
    r.kind = MeasurementKind::Coordinate;
    r.value = point.x();
    r.secondaryValue = point.y();
    r.unit = unit;
    r.referencePoints = {point};
    r.bounds = QRectF(point, QSizeF(0.0, 0.0));
    r.label = QStringLiteral("X=%1, Y=%2 %3").arg(formatNumber(point.x()), formatNumber(point.y()), unit);
    r.valid = true;
    return r;
}

MeasurementResult CadMeasurementEngine::boundingBox(const QRectF& rect, const QString& unit)
{
    MeasurementResult r;
    r.kind = MeasurementKind::BoundingBox;
    r.value = rect.width();
    r.secondaryValue = rect.height();
    r.unit = unit;
    r.bounds = rect.normalized();
    r.label = QStringLiteral("%1 x %2 %3").arg(formatNumber(r.value), formatNumber(r.secondaryValue), unit);
    r.valid = rect.isValid();
    return r;
}

double CadMeasurementEngine::polylineLength(const QVector<QPointF>& points, bool closed)
{
    if (points.size() < 2) return 0.0;
    double total = 0.0;
    for (int i = 1; i < points.size(); ++i) total += QLineF(points[i - 1], points[i]).length();
    if (closed && points.size() > 2) total += QLineF(points.last(), points.first()).length();
    return total;
}

double CadMeasurementEngine::polygonArea(const QVector<QPointF>& points)
{
    if (points.size() < 3) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF& a = points[i];
        const QPointF& b = points[(i + 1) % points.size()];
        sum += a.x() * b.y() - b.x() * a.y();
    }
    return qAbs(sum) * 0.5;
}

QPointF CadMeasurementEngine::polygonCentroid(const QVector<QPointF>& points)
{
    if (points.isEmpty()) return QPointF();
    double area2 = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF& a = points[i];
        const QPointF& b = points[(i + 1) % points.size()];
        const double cross = a.x() * b.y() - b.x() * a.y();
        area2 += cross;
        cx += (a.x() + b.x()) * cross;
        cy += (a.y() + b.y()) * cross;
    }
    if (qAbs(area2) < 1.0e-12) {
        QPointF avg;
        for (const QPointF& p : points) avg += p;
        return avg / points.size();
    }
    return QPointF(cx / (3.0 * area2), cy / (3.0 * area2));
}

double CadMeasurementEngine::entityLength(const CadEntity& entity)
{
    return CadCore::entityLength(entity);
}

double CadMeasurementEngine::entityArea(const CadEntity& entity)
{
    return CadCore::entityArea(entity);
}

QRectF CadMeasurementEngine::entityBounds(const CadEntity& entity)
{
    return CadCore::entityBoundingRect(entity);
}

EntityMeasurement CadMeasurementEngine::measureEntity(const CadEntity& entity, int index)
{
    EntityMeasurement item;
    item.index = index;
    item.entityType = CadCore::entityTypeName(entity.type());
    item.layer = entity.layer();
    item.length = entityLength(entity);
    item.area = entityArea(entity);
    item.bounds = entityBounds(entity);
    item.degenerate = CadCore::isEntityDegenerate(entity);
    return item;
}

DrawingMeasurementSummary CadMeasurementEngine::summarizeDocument(const CadDocument& document, const QVector<int>& indices)
{
    DrawingMeasurementSummary summary;
    summary.entityCount = document.entityCount();
    const QVector<int> normalized = normalizeIndices(document, indices);
    bool haveExtents = false;
    for (int index : normalized) {
        const CadEntity* e = document.entityAt(index);
        if (!e) continue;
        EntityMeasurement item = measureEntity(*e, index);
        summary.totalLength += item.length;
        summary.totalClosedArea += item.area;
        summary.entities.push_back(item);
        if (!item.bounds.isNull()) {
            summary.extents = haveExtents ? summary.extents.united(item.bounds) : item.bounds;
            haveExtents = true;
        }
    }
    summary.measuredCount = summary.entities.size();
    return summary;
}

MeasurementResult CadMeasurementEngine::selectionLength(const CadDocument& document, const QVector<int>& indices, const QString& unit)
{
    MeasurementResult r;
    r.kind = MeasurementKind::Length;
    r.unit = unit;
    for (int index : normalizeIndices(document, indices)) {
        const CadEntity* e = document.entityAt(index);
        if (e) r.value += entityLength(*e);
    }
    r.label = formatDistance(r.value, unit);
    r.valid = true;
    return r;
}

MeasurementResult CadMeasurementEngine::selectionArea(const CadDocument& document, const QVector<int>& indices, const QString& unit)
{
    MeasurementResult r;
    r.kind = MeasurementKind::Area;
    r.unit = unit;
    for (int index : normalizeIndices(document, indices)) {
        const CadEntity* e = document.entityAt(index);
        if (e) r.value += entityArea(*e);
    }
    r.label = formatArea(r.value, unit);
    r.valid = true;
    return r;
}

int CadMeasurementEngine::nearestEntity(const CadDocument& document, const QPointF& point, double* distanceOut, const QVector<int>& candidateIndices)
{
    double bestDistance = std::numeric_limits<double>::max();
    int bestIndex = -1;
    for (int index : normalizeIndices(document, candidateIndices)) {
        const CadEntity* e = document.entityAt(index);
        if (!e) continue;
        const double d = CadCore::distanceToEntity(point, *e);
        if (d < bestDistance) {
            bestDistance = d;
            bestIndex = index;
        }
    }
    if (distanceOut) *distanceOut = bestDistance;
    return bestIndex;
}

QVector<int> CadMeasurementEngine::entitiesInsideRadius(const CadDocument& document, const QPointF& center, double radius)
{
    QVector<int> result;
    if (radius < 0.0) return result;
    const double r = radius;
    for (int i = 0; i < document.entityCount(); ++i) {
        const CadEntity* e = document.entityAt(i);
        if (e && CadCore::distanceToEntity(center, *e) <= r) {
            result.push_back(i);
        }
    }
    return result;
}

QString CadMeasurementEngine::formatNumber(double value, int precision, bool trimTrailingZeros)
{
    QString s = QString::number(value, 'f', precision);
    if (trimTrailingZeros && s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    if (s == "-0") s = "0";
    return s;
}

QString CadMeasurementEngine::formatDistance(double value, const QString& unit, int precision)
{
    return QStringLiteral("%1 %2").arg(formatNumber(value, precision), unit).trimmed();
}

QString CadMeasurementEngine::formatArea(double value, const QString& unit, int precision)
{
    QString suffix = unit;
    if (!suffix.isEmpty() && !suffix.endsWith(QChar(0x00B2))) suffix += QChar(0x00B2);
    return QStringLiteral("%1 %2").arg(formatNumber(value, precision), suffix).trimmed();
}

QVector<int> CadMeasurementEngine::normalizeIndices(const CadDocument& document, const QVector<int>& indices)
{
    QVector<int> result;
    if (indices.isEmpty()) {
        result.reserve(document.entityCount());
        for (int i = 0; i < document.entityCount(); ++i) result.push_back(i);
    } else {
        for (int index : indices) {
            if (index >= 0 && index < document.entityCount()) result.push_back(index);
        }
    }
    return result;
}

} // namespace CadMeasure
