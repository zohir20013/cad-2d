#include "modify/CadAdvancedModifyEngine.h"

#include "core/CadEntityUtils.h"
#include "geometry/GeometryKernel.h"
#include "geometry/GeometryKernelAdvanced.h"

#include <QtMath>
#include <QPair>
#include <algorithm>
#include <limits>
#include <cmath>

namespace CadModify {
namespace {

static void copyStyle(const CadEntity& source, CadEntity& target)
{
    source.copyStyleTo(target);
}

static bool pointInRect(const QPointF& p, const QRectF& rect)
{
    return rect.normalized().contains(p);
}

static QVector<QPointF> sortedStationsOnLine(const CadLine& line, const QVector<QPointF>& rawPoints)
{
    QVector<QPair<double, QPointF>> stations;
    for (const QPointF& p : rawPoints) {
        double t = 0.0;
        const QPointF nearest = CadGeometry::nearestPointOnSegment(p, line.start(), line.end(), &t);
        if (CadGeometry::distance(nearest, p) <= 1.0e-5 && t > 1.0e-8 && t < 1.0 - 1.0e-8) {
            bool duplicate = false;
            for (const auto& existing : stations) {
                if (qAbs(existing.first - t) <= 1.0e-8) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) stations.push_back(qMakePair(t, nearest));
        }
    }
    std::sort(stations.begin(), stations.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    QVector<QPointF> result;
    for (const auto& s : stations) result << s.second;
    return result;
}

static void appendLine(std::vector<std::unique_ptr<CadEntity>>& out, const CadEntity& source,
                       const QPointF& a, const QPointF& b)
{
    if (CadGeometry::distance(a, b) <= CadGeometry::kDefaultTolerance) return;
    auto line = std::make_unique<CadLine>(a, b);
    copyStyle(source, *line);
    out.push_back(std::move(line));
}

static QVector<CadLine> polylineSegments(const QVector<QPointF>& points, bool closed)
{
    QVector<CadLine> segments;
    if (points.size() < 2) return segments;
    for (int i = 0; i + 1 < points.size(); ++i) segments << CadLine(points[i], points[i + 1]);
    if (closed && points.size() > 2) segments << CadLine(points.last(), points.first());
    return segments;
}

static bool near(const QPointF& a, const QPointF& b, double tolerance)
{
    return CadGeometry::distance(a, b) <= tolerance;
}

static QPointF unitDirection(const QPointF& a, const QPointF& b)
{
    return CadGeometry::normalized(b - a);
}

} // namespace

QVector<QPointF> gripPoints(const CadEntity& entity, int curveSegments)
{
    QVector<QPointF> points;
    if (const auto* line = dynamic_cast<const CadLine*>(&entity)) {
        points << line->start() << QPointF((line->start().x() + line->end().x()) * 0.5,
                                           (line->start().y() + line->end().y()) * 0.5)
               << line->end();
    } else if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
        points << circle->center()
               << CadGeometry::pointOnCadCircle(circle->center(), circle->radius(), 0.0)
               << CadGeometry::pointOnCadCircle(circle->center(), circle->radius(), 90.0)
               << CadGeometry::pointOnCadCircle(circle->center(), circle->radius(), 180.0)
               << CadGeometry::pointOnCadCircle(circle->center(), circle->radius(), 270.0);
    } else if (const auto* arc = dynamic_cast<const CadArc*>(&entity)) {
        const QVector<QPointF> endpoints = CadGeometry::arcEndpoints(arc->center(), arc->radius(), arc->startAngleDeg(), arc->spanAngleDeg());
        points << arc->center();
        for (const QPointF& p : endpoints) points << p;
        points << CadGeometry::pointOnCadCircle(arc->center(), arc->radius(), arc->startAngleDeg() + arc->spanAngleDeg() * 0.5);
    } else if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) {
        points = poly->points();
    } else if (const auto* rect = dynamic_cast<const CadRectangle*>(&entity)) {
        points = CadGeometry::rectangleCorners(rect->rect());
        points << rect->rect().center();
    } else if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
        points << ellipse->rect().center()
               << ellipse->rect().center() + QPointF(ellipse->rect().width() * 0.5, 0.0)
               << ellipse->rect().center() + QPointF(0.0, ellipse->rect().height() * 0.5)
               << ellipse->rect().center() - QPointF(ellipse->rect().width() * 0.5, 0.0)
               << ellipse->rect().center() - QPointF(0.0, ellipse->rect().height() * 0.5);
    } else {
        points = CadCore::representativePoints(entity, curveSegments);
    }
    return points;
}

std::unique_ptr<CadEntity> stretchByCrossingWindow(const CadEntity& entity, const QRectF& crossingWindow,
                                                   const QPointF& delta, int curveSegments)
{
    if (const auto* line = dynamic_cast<const CadLine*>(&entity)) {
        QPointF a = line->start();
        QPointF b = line->end();
        if (pointInRect(a, crossingWindow)) a += delta;
        if (pointInRect(b, crossingWindow)) b += delta;
        auto out = std::make_unique<CadLine>(a, b);
        copyStyle(entity, *out);
        return out;
    }
    if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) {
        QVector<QPointF> pts = poly->points();
        for (QPointF& p : pts) {
            if (pointInRect(p, crossingWindow)) p += delta;
        }
        auto out = std::make_unique<CadPolyline>(pts, poly->closed());
        copyStyle(entity, *out);
        return out;
    }
    if (const auto* dim = dynamic_cast<const CadLinearDimension*>(&entity)) {
        QPointF first = dim->first();
        QPointF second = dim->second();
        if (pointInRect(first, crossingWindow)) first += delta;
        if (pointInRect(second, crossingWindow)) second += delta;
        auto out = std::make_unique<CadLinearDimension>(first, second, dim->dimensionPoint(), dim->textHeight());
        copyStyle(entity, *out);
        return out;
    }

    auto clone = entity.clone();
    if (CadCore::entityBoundingRect(entity, curveSegments).intersects(crossingWindow.normalized())) {
        clone->translate(delta);
    }
    return clone;
}

CadLine lengthenLineToTotal(const CadLine& line, double targetLength, LengthenAnchor anchor)
{
    const double current = CadGeometry::distance(line.start(), line.end());
    if (current <= CadGeometry::kDefaultTolerance || targetLength <= CadGeometry::kDefaultTolerance) return line;
    const QPointF dir = unitDirection(line.start(), line.end());
    QPointF a = line.start();
    QPointF b = line.end();
    switch (anchor) {
    case LengthenAnchor::Start:
        a = b - dir * targetLength;
        break;
    case LengthenAnchor::End:
        b = a + dir * targetLength;
        break;
    case LengthenAnchor::Center: {
        const QPointF mid((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
        a = mid - dir * (targetLength * 0.5);
        b = mid + dir * (targetLength * 0.5);
        break;
    }
    }
    CadLine out(a, b);
    line.copyStyleTo(out);
    return out;
}

CadLine lengthenLineByDelta(const CadLine& line, double deltaLength, LengthenAnchor anchor)
{
    return lengthenLineToTotal(line, CadGeometry::distance(line.start(), line.end()) + deltaLength, anchor);
}

BreakResult breakLineAtPoints(const CadLine& line, const QVector<QPointF>& breakPoints, double gap)
{
    BreakResult result;
    QVector<QPointF> stations = sortedStationsOnLine(line, breakPoints);
    if (stations.isEmpty()) {
        result.valid = true;
        result.pieces << line;
        return result;
    }

    QVector<QPointF> chain;
    chain << line.start();
    for (const QPointF& p : stations) chain << p;
    chain << line.end();

    const QPointF dir = unitDirection(line.start(), line.end());
    for (int i = 0; i + 1 < chain.size(); ++i) {
        QPointF a = chain[i];
        QPointF b = chain[i + 1];
        if (gap > 0.0) {
            if (i > 0) a += dir * (gap * 0.5);
            if (i + 1 < chain.size() - 1) b -= dir * (gap * 0.5);
        }
        if (CadGeometry::distance(a, b) > CadGeometry::kDefaultTolerance) {
            CadLine piece(a, b);
            line.copyStyleTo(piece);
            result.pieces << piece;
        }
    }
    result.valid = !result.pieces.isEmpty();
    return result;
}

DivideResult divideLineByCount(const CadLine& line, int divisions)
{
    DivideResult result;
    if (divisions <= 1) return result;
    const QPointF step = (line.end() - line.start()) / divisions;
    for (int i = 1; i < divisions; ++i) result.points << line.start() + step * i;
    result.valid = true;
    return result;
}

DivideResult measureLineBySpacing(const CadLine& line, double spacing, bool includeRemainderPoint)
{
    DivideResult result;
    const double len = CadGeometry::distance(line.start(), line.end());
    if (spacing <= CadGeometry::kDefaultTolerance || len <= CadGeometry::kDefaultTolerance) return result;
    const QPointF dir = unitDirection(line.start(), line.end());
    for (double d = spacing; d < len - CadGeometry::kDefaultTolerance; d += spacing) {
        result.points << line.start() + dir * d;
    }
    if (includeRemainderPoint && result.points.isEmpty()) {
        result.points << line.end();
    }
    result.valid = true;
    return result;
}

std::unique_ptr<CadPolyline> joinConnectedLinesToPolyline(const QVector<CadLine>& lines, double tolerance)
{
    if (lines.isEmpty()) return nullptr;

    QVector<bool> used(lines.size(), false);
    QVector<QPointF> chain;
    chain << lines.first().start() << lines.first().end();
    used[0] = true;

    bool advanced = true;
    while (advanced) {
        advanced = false;
        for (int i = 0; i < lines.size(); ++i) {
            if (used[i]) continue;
            const QPointF a = lines[i].start();
            const QPointF b = lines[i].end();
            if (near(chain.last(), a, tolerance)) {
                chain << b;
                used[i] = true;
                advanced = true;
            } else if (near(chain.last(), b, tolerance)) {
                chain << a;
                used[i] = true;
                advanced = true;
            } else if (near(chain.first(), b, tolerance)) {
                chain.prepend(a);
                used[i] = true;
                advanced = true;
            } else if (near(chain.first(), a, tolerance)) {
                chain.prepend(b);
                used[i] = true;
                advanced = true;
            }
        }
    }

    const bool closed = chain.size() > 2 && near(chain.first(), chain.last(), tolerance);
    if (closed) chain.removeLast();
    auto poly = std::make_unique<CadPolyline>(chain, closed);
    lines.first().copyStyleTo(*poly);
    return poly;
}

std::vector<std::unique_ptr<CadEntity>> explodeToPrimitives(const CadEntity& entity, int curveSegments)
{
    std::vector<std::unique_ptr<CadEntity>> out;
    curveSegments = qBound(8, curveSegments, 720);

    if (const auto* line = dynamic_cast<const CadLine*>(&entity)) {
        out.push_back(line->clone());
    } else if (const auto* rect = dynamic_cast<const CadRectangle*>(&entity)) {
        const QVector<QPointF> c = CadGeometry::rectangleCorners(rect->rect());
        for (int i = 0; i < c.size(); ++i) appendLine(out, entity, c[i], c[(i + 1) % c.size()]);
    } else if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) {
        for (const CadLine& segment : polylineSegments(poly->points(), poly->closed())) appendLine(out, entity, segment.start(), segment.end());
    } else if (const auto* polygon = dynamic_cast<const CadPolygon*>(&entity)) {
        const QVector<QPointF> pts = CadGeometry::regularPolygonPoints(polygon->center(), polygon->radius(), polygon->sides(), polygon->rotationDeg());
        for (int i = 0; i < pts.size(); ++i) appendLine(out, entity, pts[i], pts[(i + 1) % pts.size()]);
    } else if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
        auto poly = std::make_unique<CadPolyline>(CadGeometry::approximateEllipse(CadGeometry::circleBoundingRect(circle->center(), circle->radius()), curveSegments), true);
        copyStyle(entity, *poly);
        out.push_back(std::move(poly));
    } else if (const auto* arc = dynamic_cast<const CadArc*>(&entity)) {
        auto poly = std::make_unique<CadPolyline>(CadGeometry::approximateArc(arc->center(), arc->radius(), arc->startAngleDeg(), arc->spanAngleDeg(), 360.0 / curveSegments), false);
        copyStyle(entity, *poly);
        out.push_back(std::move(poly));
    } else if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
        auto poly = std::make_unique<CadPolyline>(CadGeometry::approximateEllipse(ellipse->rect(), curveSegments), true);
        copyStyle(entity, *poly);
        out.push_back(std::move(poly));
    } else if (const auto* hatch = dynamic_cast<const CadHatch*>(&entity)) {
        const auto loops = hatch->loops().isEmpty() ? QVector<QVector<QPointF>>{hatch->boundary()} : hatch->loops();
        for (const QVector<QPointF>& loop : loops) {
            auto poly = std::make_unique<CadPolyline>(loop, true);
            copyStyle(entity, *poly);
            out.push_back(std::move(poly));
        }
    } else if (const auto* leader = dynamic_cast<const CadLeader*>(&entity)) {
        appendLine(out, entity, leader->arrowPoint(), leader->textPoint());
        auto text = std::make_unique<CadText>(leader->textPoint(), leader->text(), leader->textHeight());
        copyStyle(entity, *text);
        out.push_back(std::move(text));
    } else {
        out.push_back(entity.clone());
    }
    return out;
}

std::unique_ptr<CadEntity> alignByTwoPoints(const CadEntity& entity,
                                            const QPointF& sourceA, const QPointF& sourceB,
                                            const QPointF& targetA, const QPointF& targetB,
                                            bool scaleToTarget)
{
    auto out = entity.clone();
    const double srcLen = CadGeometry::distance(sourceA, sourceB);
    const double dstLen = CadGeometry::distance(targetA, targetB);
    if (srcLen <= CadGeometry::kDefaultTolerance || dstLen <= CadGeometry::kDefaultTolerance) return out;

    out->translate(targetA - sourceA);
    const QPointF movedSourceB = sourceB + (targetA - sourceA);
    const double sourceAngle = qRadiansToDegrees(std::atan2(movedSourceB.y() - targetA.y(), movedSourceB.x() - targetA.x()));
    const double targetAngle = qRadiansToDegrees(std::atan2(targetB.y() - targetA.y(), targetB.x() - targetA.x()));
    out->rotate(targetA, targetAngle - sourceAngle);
    if (scaleToTarget) out->scale(targetA, dstLen / srcLen);
    return out;
}

std::vector<std::unique_ptr<CadEntity>> rectangularArray(const CadEntity& entity, int rows, int columns,
                                                     double rowSpacing, double columnSpacing)
{
    std::vector<std::unique_ptr<CadEntity>> out;
    rows = qMax(1, rows);
    columns = qMax(1, columns);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            auto clone = entity.clone();
            clone->translate(QPointF(c * columnSpacing, r * rowSpacing));
            out.push_back(std::move(clone));
        }
    }
    return out;
}

std::vector<std::unique_ptr<CadEntity>> polarArray(const CadEntity& entity, const QPointF& center,
                                               int count, double totalAngleDeg, bool rotateItems)
{
    std::vector<std::unique_ptr<CadEntity>> out;
    count = qMax(1, count);
    const double step = count <= 1 ? 0.0 : totalAngleDeg / count;
    for (int i = 0; i < count; ++i) {
        auto clone = entity.clone();
        const double angle = step * i;
        clone->rotate(center, angle);
        if (!rotateItems) {
            const QRectF bounds = CadCore::entityBoundingRect(*clone);
            clone->rotate(bounds.center(), -angle);
        }
        out.push_back(std::move(clone));
    }
    return out;
}

std::vector<std::unique_ptr<CadEntity>> copyAlongPolyline(const CadEntity& entity, const QVector<QPointF>& path,
                                                      bool closed, double spacing, bool alignToPath)
{
    std::vector<std::unique_ptr<CadEntity>> out;
    if (path.size() < 2 || spacing <= CadGeometry::kDefaultTolerance) return out;
    const double length = CadGeometry::polylineLength(path, closed);
    for (double distance = 0.0; distance <= length + CadGeometry::kDefaultTolerance; distance += spacing) {
        const auto station = CadGeometry::polylineStationAtDistance(path, closed, distance);
        if (!station.valid) continue;
        auto clone = entity.clone();
        const QRectF bounds = CadCore::entityBoundingRect(entity);
        clone->translate(station.point - bounds.center());
        if (alignToPath && station.segmentIndex >= 0) {
            const int next = (station.segmentIndex + 1) % path.size();
            const QPointF a = path[station.segmentIndex];
            const QPointF b = path[next];
            const double angle = qRadiansToDegrees(std::atan2(b.y() - a.y(), b.x() - a.x()));
            clone->rotate(station.point, angle);
        }
        out.push_back(std::move(clone));
    }
    return out;
}

ModifyReport joinSummary(const QVector<CadLine>& lines, double tolerance)
{
    ModifyReport report;
    auto joined = joinConnectedLinesToPolyline(lines, tolerance);
    report.success = static_cast<bool>(joined);
    report.createdCount = report.success ? 1 : 0;
    report.message = report.success
        ? QStringLiteral("Joined %1 line segments into a polyline.").arg(lines.size())
        : QStringLiteral("No connected line chain could be created.");
    if (joined) report.constructionPoints = joined->points();
    return report;
}

} // namespace CadModify
