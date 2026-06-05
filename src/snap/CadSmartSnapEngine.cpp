#include "snap/CadSmartSnapEngine.h"

#include "core/CadEntityUtils.h"
#include "geometry/GeometryKernel.h"

#include <QtMath>
#include <algorithm>
#include <limits>
#include <cmath>

namespace CadSnap {
namespace {

struct SegmentRef {
    QPointF a;
    QPointF b;
    int entityIndex = -1;
};

static void addCandidate(QVector<CadGeometry::SnapCandidate>& out,
                         CadGeometry::SnapKind kind, const QPointF& point,
                         const QPointF& cursor, int sourceIndex, double aperture)
{
    const double d = CadGeometry::distance(cursor, point);
    if (d <= aperture) {
        CadGeometry::SnapCandidate candidate;
        candidate.valid = true;
        candidate.kind = kind;
        candidate.point = point;
        candidate.distance = d;
        candidate.sourceIndex = sourceIndex;
        out << candidate;
    }
}

static bool isClosedLike(const CadEntity& entity)
{
    if (entity.type() == CadEntity::Type::Circle || entity.type() == CadEntity::Type::Ellipse ||
        entity.type() == CadEntity::Type::Rectangle || entity.type() == CadEntity::Type::Polygon ||
        entity.type() == CadEntity::Type::Hatch) {
        return true;
    }
    if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) return poly->closed();
    return false;
}

static QVector<SegmentRef> entitySegments(const CadEntity& entity, int entityIndex, int curveSegments)
{
    QVector<SegmentRef> segments;
    const QVector<QPointF> points = CadCore::representativePoints(entity, curveSegments);
    if (points.size() < 2) return segments;
    for (int i = 0; i + 1 < points.size(); ++i) {
        if (CadGeometry::distance(points[i], points[i + 1]) > CadGeometry::kDefaultTolerance) {
            segments << SegmentRef{points[i], points[i + 1], entityIndex};
        }
    }
    if (isClosedLike(entity) && CadGeometry::distance(points.last(), points.first()) > CadGeometry::kDefaultTolerance) {
        segments << SegmentRef{points.last(), points.first(), entityIndex};
    }
    return segments;
}

static int snapPriority(CadGeometry::SnapKind kind)
{
    switch (kind) {
    case CadGeometry::SnapKind::Intersection: return 0;
    case CadGeometry::SnapKind::Endpoint: return 1;
    case CadGeometry::SnapKind::Center: return 2;
    case CadGeometry::SnapKind::Midpoint: return 3;
    case CadGeometry::SnapKind::Quadrant: return 4;
    case CadGeometry::SnapKind::Perpendicular: return 5;
    case CadGeometry::SnapKind::Tangent: return 6;
    case CadGeometry::SnapKind::Extension: return 7;
    case CadGeometry::SnapKind::ApparentIntersection: return 8;
    case CadGeometry::SnapKind::Nearest: return 9;
    }
    return 99;
}

} // namespace

bool hasMode(SnapModeMask mask, SnapModeFlag flag)
{
    return (mask & static_cast<int>(flag)) != 0;
}

QVector<CadGeometry::SnapCandidate> collectEntitySnaps(const CadEntity& entity, int entityIndex,
                                                       const SmartSnapContext& context, int curveSegments)
{
    QVector<CadGeometry::SnapCandidate> out;
    const QPointF cursor = context.cursor;

    const QVector<SegmentRef> segments = entitySegments(entity, entityIndex, curveSegments);
    if (hasMode(context.modes, SnapEndpoint)) {
        if (const auto* line = dynamic_cast<const CadLine*>(&entity)) {
            addCandidate(out, CadGeometry::SnapKind::Endpoint, line->start(), cursor, entityIndex, context.aperture);
            addCandidate(out, CadGeometry::SnapKind::Endpoint, line->end(), cursor, entityIndex, context.aperture);
        } else if (const auto* poly = dynamic_cast<const CadPolyline*>(&entity)) {
            for (const QPointF& p : poly->points()) addCandidate(out, CadGeometry::SnapKind::Endpoint, p, cursor, entityIndex, context.aperture);
        } else {
            const QVector<QPointF> pts = CadCore::representativePoints(entity, curveSegments);
            if (!pts.isEmpty() && entity.type() == CadEntity::Type::Arc) {
                addCandidate(out, CadGeometry::SnapKind::Endpoint, pts.first(), cursor, entityIndex, context.aperture);
                addCandidate(out, CadGeometry::SnapKind::Endpoint, pts.last(), cursor, entityIndex, context.aperture);
            }
        }
    }

    if (hasMode(context.modes, SnapMidpoint)) {
        for (const SegmentRef& segment : segments) {
            addCandidate(out, CadGeometry::SnapKind::Midpoint,
                         QPointF((segment.a.x() + segment.b.x()) * 0.5, (segment.a.y() + segment.b.y()) * 0.5),
                         cursor, entityIndex, context.aperture);
        }
    }

    if (hasMode(context.modes, SnapCenter)) {
        if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
            addCandidate(out, CadGeometry::SnapKind::Center, circle->center(), cursor, entityIndex, context.aperture);
        } else if (const auto* arc = dynamic_cast<const CadArc*>(&entity)) {
            addCandidate(out, CadGeometry::SnapKind::Center, arc->center(), cursor, entityIndex, context.aperture);
        } else if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
            addCandidate(out, CadGeometry::SnapKind::Center, ellipse->rect().center(), cursor, entityIndex, context.aperture);
        } else if (const auto* polygon = dynamic_cast<const CadPolygon*>(&entity)) {
            addCandidate(out, CadGeometry::SnapKind::Center, polygon->center(), cursor, entityIndex, context.aperture);
        }
    }

    if (hasMode(context.modes, SnapQuadrant)) {
        if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
            for (double angle : {0.0, 90.0, 180.0, 270.0}) {
                addCandidate(out, CadGeometry::SnapKind::Quadrant,
                             CadGeometry::pointOnCadCircle(circle->center(), circle->radius(), angle),
                             cursor, entityIndex, context.aperture);
            }
        } else if (const auto* ellipse = dynamic_cast<const CadEllipse*>(&entity)) {
            const QRectF r = ellipse->rect().normalized();
            addCandidate(out, CadGeometry::SnapKind::Quadrant, QPointF(r.right(), r.center().y()), cursor, entityIndex, context.aperture);
            addCandidate(out, CadGeometry::SnapKind::Quadrant, QPointF(r.center().x(), r.top()), cursor, entityIndex, context.aperture);
            addCandidate(out, CadGeometry::SnapKind::Quadrant, QPointF(r.left(), r.center().y()), cursor, entityIndex, context.aperture);
            addCandidate(out, CadGeometry::SnapKind::Quadrant, QPointF(r.center().x(), r.bottom()), cursor, entityIndex, context.aperture);
        }
    }

    if (hasMode(context.modes, SnapPerpendicular)) {
        for (const SegmentRef& segment : segments) {
            const CadGeometry::SnapCandidate candidate = CadGeometry::perpendicularSnap(cursor, segment.a, segment.b, entityIndex);
            if (candidate.valid && candidate.distance <= context.aperture) out << candidate;
        }
    }

    if (hasMode(context.modes, SnapTangent)) {
        if (const auto* circle = dynamic_cast<const CadCircle*>(&entity)) {
            for (const CadGeometry::SnapCandidate& candidate : CadGeometry::tangentSnapsFromCursorToCircle(cursor, circle->center(), circle->radius(), entityIndex)) {
                if (candidate.valid && candidate.distance <= context.aperture) out << candidate;
            }
        }
    }

    if (hasMode(context.modes, SnapNearest)) {
        QPointF nearest;
        const double distance = CadCore::distanceToEntity(cursor, entity, &nearest, curveSegments);
        if (distance <= context.aperture) {
            CadGeometry::SnapCandidate candidate;
            candidate.valid = true;
            candidate.kind = CadGeometry::SnapKind::Nearest;
            candidate.point = nearest;
            candidate.distance = distance;
            candidate.sourceIndex = entityIndex;
            out << candidate;
        }
    }

    return out;
}

QVector<CadGeometry::SnapCandidate> collectIntersectionSnaps(const std::vector<std::unique_ptr<CadEntity>>& entities,
                                                             const SmartSnapContext& context, int curveSegments)
{
    QVector<CadGeometry::SnapCandidate> out;
    if (!hasMode(context.modes, SnapIntersection) && !hasMode(context.modes, SnapApparentIntersection)) return out;

    QVector<SegmentRef> segments;
    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        if (!entities[i]) continue;
        segments += entitySegments(*entities[i], i, curveSegments);
    }

    for (int i = 0; i < segments.size(); ++i) {
        for (int j = i + 1; j < segments.size(); ++j) {
            if (segments[i].entityIndex == segments[j].entityIndex) continue;
            const auto intersection = CadGeometry::intersectLineSegments(segments[i].a, segments[i].b, segments[j].a, segments[j].b, context.drawingTolerance);
            for (const QPointF& p : intersection.points) {
                addCandidate(out, CadGeometry::SnapKind::Intersection, p, context.cursor, segments[i].entityIndex, context.aperture);
            }
        }
    }
    return out;
}

QVector<CadGeometry::SnapCandidate> collectTrackingSnaps(const SmartSnapContext& context)
{
    QVector<CadGeometry::SnapCandidate> out;
    if (!hasMode(context.modes, SnapExtension)) return out;
    for (const QPointF& base : context.trackingPoints) {
        QPointF ortho = orthogonalTrackingPoint(base, context.cursor);
        addCandidate(out, CadGeometry::SnapKind::Extension, ortho, context.cursor, -1, context.aperture);
        QPointF polar = polarTrackingPoint(base, context.cursor, context.polarAnglesDeg);
        addCandidate(out, CadGeometry::SnapKind::Extension, polar, context.cursor, -1, context.aperture);
    }
    return out;
}

SmartSnapResult snap(const std::vector<std::unique_ptr<CadEntity>>& entities,
                     const SmartSnapContext& context, int curveSegments)
{
    SmartSnapResult result;
    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        if (!entities[i]) continue;
        result.candidates += collectEntitySnaps(*entities[i], i, context, curveSegments);
    }
    result.candidates += collectIntersectionSnaps(entities, context, curveSegments);
    result.candidates += collectTrackingSnaps(context);

    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) {
        const int pa = snapPriority(a.kind);
        const int pb = snapPriority(b.kind);
        if (pa != pb) return pa < pb;
        return a.distance < b.distance;
    });

    if (!result.candidates.isEmpty()) {
        result.found = true;
        result.candidate = result.candidates.first();
    }
    return result;
}

QPointF snapToGrid(const QPointF& point, double gridSpacing, const QPointF& origin)
{
    if (gridSpacing <= CadGeometry::kDefaultTolerance) return point;
    const double x = origin.x() + std::round((point.x() - origin.x()) / gridSpacing) * gridSpacing;
    const double y = origin.y() + std::round((point.y() - origin.y()) / gridSpacing) * gridSpacing;
    return QPointF(x, y);
}

QPointF orthogonalTrackingPoint(const QPointF& basePoint, const QPointF& cursor)
{
    const QPointF delta = cursor - basePoint;
    return qAbs(delta.x()) >= qAbs(delta.y()) ? QPointF(cursor.x(), basePoint.y()) : QPointF(basePoint.x(), cursor.y());
}

QPointF polarTrackingPoint(const QPointF& basePoint, const QPointF& cursor,
                           const QVector<double>& anglesDeg, double* chosenAngleDeg)
{
    if (anglesDeg.isEmpty()) {
        if (chosenAngleDeg) *chosenAngleDeg = 0.0;
        return cursor;
    }
    const QPointF delta = cursor - basePoint;
    const double distance = CadGeometry::length(delta);
    if (distance <= CadGeometry::kDefaultTolerance) return basePoint;
    const double cursorAngle = CadGeometry::normalizeAngleDeg(qRadiansToDegrees(std::atan2(delta.y(), delta.x())));

    double bestAngle = anglesDeg.first();
    double bestDiff = std::numeric_limits<double>::infinity();
    for (double angle : anglesDeg) {
        const double diff = qAbs(CadGeometry::signedShortestSpanDeg(cursorAngle, angle));
        if (diff < bestDiff) {
            bestDiff = diff;
            bestAngle = angle;
        }
    }
    if (chosenAngleDeg) *chosenAngleDeg = bestAngle;
    return basePoint + QPointF(std::cos(qDegreesToRadians(bestAngle)) * distance,
                               std::sin(qDegreesToRadians(bestAngle)) * distance);
}

} // namespace CadSnap
