#include "GeometryKernel.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace CadGeometry {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

void appendUniquePoint(QVector<QPointF>& points, const QPointF& point, double tolerance)
{
    if (!isFinite(point)) return;
    for (const QPointF& existing : points) {
        if (distance(existing, point) <= tolerance) return;
    }
    points.append(point);
}

} // namespace

bool isFinite(double value)
{
    return std::isfinite(value);
}

bool isFinite(const QPointF& point)
{
    return isFinite(point.x()) && isFinite(point.y());
}

bool fuzzyEquals(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

bool fuzzyEquals(const QPointF& a, const QPointF& b, double tolerance)
{
    return distance(a, b) <= tolerance;
}

double dot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

double cross(const QPointF& a, const QPointF& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

double lengthSquared(const QPointF& v)
{
    return dot(v, v);
}

double length(const QPointF& v)
{
    return std::sqrt(lengthSquared(v));
}

double distance(const QPointF& a, const QPointF& b)
{
    return length(b - a);
}

bool isDegenerateSegment(const QPointF& a, const QPointF& b, double tolerance)
{
    return distance(a, b) <= tolerance;
}

QPointF normalized(const QPointF& v, double tolerance)
{
    const double len = length(v);
    if (len <= tolerance) return QPointF();
    return QPointF(v.x() / len, v.y() / len);
}

QPointF perpendicularLeft(const QPointF& v, double tolerance)
{
    const QPointF n = normalized(v, tolerance);
    if (lengthSquared(n) <= tolerance * tolerance) return QPointF();
    return QPointF(-n.y(), n.x());
}

QPointF pointOnLine(const QPointF& a, const QPointF& direction, double parameter)
{
    return QPointF(a.x() + direction.x() * parameter,
                   a.y() + direction.y() * parameter);
}

QPointF rotatePoint(const QPointF& point, const QPointF& center, double angleDeg)
{
    const double a = qDegreesToRadians(angleDeg);
    const double c = std::cos(a);
    const double s = std::sin(a);
    const QPointF v = point - center;
    return QPointF(center.x() + v.x() * c - v.y() * s,
                   center.y() + v.x() * s + v.y() * c);
}

QPointF mirrorPoint(const QPointF& point, const QPointF& axisA, const QPointF& axisB)
{
    double parameter = 0.0;
    const QPointF projection = projectPointOnInfiniteLine(point, axisA, axisB, &parameter);
    if (!isFinite(projection)) return point;
    return QPointF(2.0 * projection.x() - point.x(),
                   2.0 * projection.y() - point.y());
}

QPointF projectPointOnInfiniteLine(const QPointF& point, const QPointF& lineA, const QPointF& lineB, double* parameter)
{
    const QPointF direction = lineB - lineA;
    const double len2 = lengthSquared(direction);
    if (len2 <= kDefaultTolerance * kDefaultTolerance) {
        if (parameter) *parameter = 0.0;
        return lineA;
    }
    const double t = dot(point - lineA, direction) / len2;
    if (parameter) *parameter = t;
    return pointOnLine(lineA, direction, t);
}

QPointF nearestPointOnSegment(const QPointF& point, const QPointF& segmentA, const QPointF& segmentB, double* parameter)
{
    const QPointF direction = segmentB - segmentA;
    const double len2 = lengthSquared(direction);
    if (len2 <= kDefaultTolerance * kDefaultTolerance) {
        if (parameter) *parameter = 0.0;
        return segmentA;
    }
    const double t = clamp01(dot(point - segmentA, direction) / len2);
    if (parameter) *parameter = t;
    return pointOnLine(segmentA, direction, t);
}

double distancePointToSegment(const QPointF& point, const QPointF& segmentA, const QPointF& segmentB)
{
    return distance(point, nearestPointOnSegment(point, segmentA, segmentB));
}

double normalizeAngleDeg(double angleDeg)
{
    double value = std::fmod(angleDeg, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

double cadAngleDeg(const QPointF& center, const QPointF& point)
{
    // CAD/Qt arc convention used by CadArc: 0° at +X, positive counter-clockwise in drawing coordinates.
    return normalizeAngleDeg(qRadiansToDegrees(std::atan2(center.y() - point.y(), point.x() - center.x())));
}

double ccwSpanDeg(double startDeg, double endDeg)
{
    double span = normalizeAngleDeg(endDeg) - normalizeAngleDeg(startDeg);
    if (span < 0.0) span += 360.0;
    return span;
}

double signedShortestSpanDeg(double startDeg, double endDeg)
{
    const double ccw = ccwSpanDeg(startDeg, endDeg);
    return ccw <= 180.0 ? ccw : ccw - 360.0;
}

bool pointOnCadArc(const QPointF& center, const QPointF& point, double startAngleDeg, double spanAngleDeg,
                   double toleranceDeg)
{
    if (std::abs(spanAngleDeg) >= 360.0 - toleranceDeg) return true;
    const double angle = cadAngleDeg(center, point);
    if (spanAngleDeg >= 0.0) {
        return ccwSpanDeg(startAngleDeg, angle) <= spanAngleDeg + toleranceDeg;
    }
    return ccwSpanDeg(angle, startAngleDeg) <= std::abs(spanAngleDeg) + toleranceDeg;
}

QPointF pointOnCadCircle(const QPointF& center, double radius, double angleDeg)
{
    const double a = qDegreesToRadians(angleDeg);
    return QPointF(center.x() + radius * std::cos(a),
                   center.y() - radius * std::sin(a));
}

bool pointOnSegment(const QPointF& point, const QPointF& segmentA, const QPointF& segmentB, double tolerance)
{
    if (distancePointToSegment(point, segmentA, segmentB) > tolerance) return false;
    const QRectF box(segmentA, segmentB);
    return box.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point);
}

IntersectionResult intersectInfiniteLines(const QPointF& a, const QPointF& b,
                                          const QPointF& c, const QPointF& d,
                                          double tolerance)
{
    IntersectionResult result;
    const QPointF r = b - a;
    const QPointF s = d - c;
    if (lengthSquared(r) <= tolerance * tolerance || lengthSquared(s) <= tolerance * tolerance) return result;

    const double denominator = cross(r, s);
    const QPointF ca = c - a;
    if (std::abs(denominator) <= tolerance) {
        if (std::abs(cross(ca, r)) <= tolerance) result.kind = IntersectionKind::Overlap;
        return result;
    }

    const double t = cross(ca, s) / denominator;
    result.kind = IntersectionKind::One;
    result.points.append(pointOnLine(a, r, t));
    return result;
}

IntersectionResult intersectLineSegments(const QPointF& a, const QPointF& b,
                                         const QPointF& c, const QPointF& d,
                                         double tolerance)
{
    IntersectionResult result;
    const QPointF r = b - a;
    const QPointF s = d - c;
    if (lengthSquared(r) <= tolerance * tolerance || lengthSquared(s) <= tolerance * tolerance) return result;

    const double denominator = cross(r, s);
    const QPointF ca = c - a;

    if (std::abs(denominator) <= tolerance) {
        if (std::abs(cross(ca, r)) > tolerance) return result;

        const double r2 = lengthSquared(r);
        const double t0 = dot(c - a, r) / r2;
        const double t1 = dot(d - a, r) / r2;
        const double lo = std::max(0.0, std::min(t0, t1));
        const double hi = std::min(1.0, std::max(t0, t1));
        if (hi < lo - tolerance) return result;
        result.kind = fuzzyEquals(lo, hi, tolerance) ? IntersectionKind::One : IntersectionKind::Overlap;
        appendUniquePoint(result.points, pointOnLine(a, r, lo), tolerance);
        appendUniquePoint(result.points, pointOnLine(a, r, hi), tolerance);
        return result;
    }

    const double t = cross(ca, s) / denominator;
    const double u = cross(ca, r) / denominator;
    if (t < -tolerance || t > 1.0 + tolerance || u < -tolerance || u > 1.0 + tolerance) return result;

    result.kind = IntersectionKind::One;
    result.points.append(pointOnLine(a, r, t));
    return result;
}

IntersectionResult intersectInfiniteLineCircle(const QPointF& lineA, const QPointF& lineB,
                                               const QPointF& center, double radius,
                                               double tolerance)
{
    IntersectionResult result;
    if (radius < 0.0) return result;

    const QPointF d = lineB - lineA;
    const double aa = lengthSquared(d);
    if (aa <= tolerance * tolerance) return result;

    const QPointF f = lineA - center;
    const double bb = 2.0 * dot(f, d);
    const double cc = dot(f, f) - radius * radius;
    const double disc = bb * bb - 4.0 * aa * cc;
    if (disc < -tolerance) return result;

    if (std::abs(disc) <= tolerance) {
        result.kind = IntersectionKind::Tangent;
        result.points.append(pointOnLine(lineA, d, -bb / (2.0 * aa)));
        return result;
    }

    const double root = std::sqrt(std::max(0.0, disc));
    result.kind = IntersectionKind::Two;
    result.points.append(pointOnLine(lineA, d, (-bb - root) / (2.0 * aa)));
    result.points.append(pointOnLine(lineA, d, (-bb + root) / (2.0 * aa)));
    return result;
}

IntersectionResult intersectSegmentCircle(const QPointF& segmentA, const QPointF& segmentB,
                                          const QPointF& center, double radius,
                                          double tolerance)
{
    IntersectionResult lineHits = intersectInfiniteLineCircle(segmentA, segmentB, center, radius, tolerance);
    IntersectionResult result;
    for (const QPointF& point : lineHits.points) {
        if (pointOnSegment(point, segmentA, segmentB, tolerance)) appendUniquePoint(result.points, point, tolerance);
    }
    if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectCircleCircle(const QPointF& centerA, double radiusA,
                                         const QPointF& centerB, double radiusB,
                                         double tolerance)
{
    IntersectionResult result;
    if (radiusA < 0.0 || radiusB < 0.0) return result;

    const double d = distance(centerA, centerB);
    if (d <= tolerance && std::abs(radiusA - radiusB) <= tolerance) {
        result.kind = IntersectionKind::Overlap;
        return result;
    }
    if (d > radiusA + radiusB + tolerance) return result;
    if (d < std::abs(radiusA - radiusB) - tolerance) return result;
    if (d <= tolerance) return result;

    const double a = (radiusA * radiusA - radiusB * radiusB + d * d) / (2.0 * d);
    const double h2 = radiusA * radiusA - a * a;
    if (h2 < -tolerance) return result;

    const QPointF dir((centerB.x() - centerA.x()) / d,
                      (centerB.y() - centerA.y()) / d);
    const QPointF base(centerA.x() + a * dir.x(), centerA.y() + a * dir.y());

    if (std::abs(h2) <= tolerance) {
        result.kind = IntersectionKind::Tangent;
        result.points.append(base);
        return result;
    }

    const double h = std::sqrt(std::max(0.0, h2));
    const QPointF perp(-dir.y(), dir.x());
    result.kind = IntersectionKind::Two;
    result.points.append(QPointF(base.x() + h * perp.x(), base.y() + h * perp.y()));
    result.points.append(QPointF(base.x() - h * perp.x(), base.y() - h * perp.y()));
    return result;
}

IntersectionResult intersectInfiniteLineEllipse(const QPointF& lineA, const QPointF& lineB,
                                                const QRectF& ellipseRect,
                                                double tolerance)
{
    IntersectionResult result;
    const QRectF rect = ellipseRect.normalized();
    const double rx = rect.width() * 0.5;
    const double ry = rect.height() * 0.5;
    if (rx <= tolerance || ry <= tolerance) return result;

    const QPointF center = rect.center();
    const QPointF d = lineB - lineA;
    if (lengthSquared(d) <= tolerance * tolerance) return result;

    const double ox = (lineA.x() - center.x()) / rx;
    const double oy = (lineA.y() - center.y()) / ry;
    const double dx = d.x() / rx;
    const double dy = d.y() / ry;

    const double aa = dx * dx + dy * dy;
    const double bb = 2.0 * (ox * dx + oy * dy);
    const double cc = ox * ox + oy * oy - 1.0;
    const double disc = bb * bb - 4.0 * aa * cc;
    if (disc < -tolerance) return result;

    if (std::abs(disc) <= tolerance) {
        result.kind = IntersectionKind::Tangent;
        result.points.append(pointOnLine(lineA, d, -bb / (2.0 * aa)));
        return result;
    }

    const double root = std::sqrt(std::max(0.0, disc));
    result.kind = IntersectionKind::Two;
    result.points.append(pointOnLine(lineA, d, (-bb - root) / (2.0 * aa)));
    result.points.append(pointOnLine(lineA, d, (-bb + root) / (2.0 * aa)));
    return result;
}

QVector<QPointF> rectangleCorners(const QRectF& rect)
{
    const QRectF r = rect.normalized();
    return {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
}

QVector<QPointF> regularPolygonPoints(const QPointF& center, double radius, int sides, double rotationDeg)
{
    QVector<QPointF> points;
    const int n = std::max(3, sides);
    if (radius <= kDefaultTolerance) return points;
    const double rotationRad = qDegreesToRadians(rotationDeg);
    points.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double a = rotationRad + 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
        points.append(QPointF(center.x() + radius * std::cos(a), center.y() + radius * std::sin(a)));
    }
    return points;
}

QRectF boundingRect(const QVector<QPointF>& points)
{
    if (points.isEmpty()) return QRectF();
    double minX = points.first().x();
    double minY = points.first().y();
    double maxX = minX;
    double maxY = minY;
    for (const QPointF& point : points) {
        minX = std::min(minX, point.x());
        minY = std::min(minY, point.y());
        maxX = std::max(maxX, point.x());
        maxY = std::max(maxY, point.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

double polygonSignedArea(const QVector<QPointF>& polygon)
{
    if (polygon.size() < 3) return 0.0;
    double area = 0.0;
    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF& a = polygon.at(i);
        const QPointF& b = polygon.at((i + 1) % polygon.size());
        area += a.x() * b.y() - b.x() * a.y();
    }
    return 0.5 * area;
}

bool isClockwise(const QVector<QPointF>& polygon)
{
    return polygonSignedArea(polygon) > 0.0;
}

bool pointInPolygon(const QPointF& point, const QVector<QPointF>& polygon)
{
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (int i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const QPointF& a = polygon.at(i);
        const QPointF& b = polygon.at(j);
        const bool intersects = ((a.y() > point.y()) != (b.y() > point.y()))
            && (point.x() < (b.x() - a.x()) * (point.y() - a.y()) / (b.y() - a.y() + std::numeric_limits<double>::epsilon()) + a.x());
        if (intersects) inside = !inside;
    }
    return inside;
}

QVector<QPointF> offsetSegment(const QPointF& a, const QPointF& b, double distanceValue, double tolerance)
{
    QVector<QPointF> result;
    if (isDegenerateSegment(a, b, tolerance)) return result;
    const QPointF normal = perpendicularLeft(b - a, tolerance);
    const QPointF delta = normal * distanceValue;
    result.append(a + delta);
    result.append(b + delta);
    return result;
}

QVector<QPointF> offsetPolyline(const QVector<QPointF>& points, bool closed, double distanceValue, double tolerance)
{
    QVector<QPointF> result;
    if (points.size() < 2 || std::abs(distanceValue) <= tolerance) return result;
    if (!closed && points.size() == 2) return offsetSegment(points.first(), points.last(), distanceValue, tolerance);

    const int n = points.size();
    const int outCount = closed ? n : n;
    result.reserve(outCount);

    const auto offsetLine = [&](int i0, int i1, QPointF& a, QPointF& b) -> bool {
        const QPointF p0 = points.at(i0);
        const QPointF p1 = points.at(i1);
        if (isDegenerateSegment(p0, p1, tolerance)) return false;
        const QPointF normal = perpendicularLeft(p1 - p0, tolerance);
        const QPointF delta = normal * distanceValue;
        a = p0 + delta;
        b = p1 + delta;
        return true;
    };

    if (!closed) {
        QPointF a0, b0;
        if (!offsetLine(0, 1, a0, b0)) return result;
        result.append(a0);
    }

    const int firstVertex = closed ? 0 : 1;
    const int lastVertex = closed ? n - 1 : n - 2;
    for (int i = firstVertex; i <= lastVertex; ++i) {
        const int prev = (i - 1 + n) % n;
        const int next = (i + 1) % n;
        QPointF p1a, p1b, p2a, p2b;
        if (!offsetLine(prev, i, p1a, p1b) || !offsetLine(i, next, p2a, p2b)) {
            continue;
        }

        const IntersectionResult hit = intersectInfiniteLines(p1a, p1b, p2a, p2b, tolerance);
        if (hit.hasPoint() && hit.kind != IntersectionKind::Overlap) {
            result.append(hit.firstPoint());
        } else {
            // Parallel consecutive segments: use the average of both shifted vertex positions.
            result.append((p1b + p2a) * 0.5);
        }
    }

    if (!closed) {
        QPointF aN, bN;
        if (offsetLine(n - 2, n - 1, aN, bN)) result.append(bN);
    }

    return result;
}

QPointF pointFromPivotAlongSegment(const QPointF& segmentA, const QPointF& segmentB,
                                   const QPointF& pivot, double distanceFromPivot,
                                   double tolerance)
{
    const QPointF far = distance(segmentA, pivot) > distance(segmentB, pivot) ? segmentA : segmentB;
    const QPointF direction = far - pivot;
    const double len = length(direction);
    if (len <= tolerance) return pivot;
    const double d = std::min(distanceFromPivot, len);
    return pivot + normalized(direction, tolerance) * d;
}

FilletResult lineLineFillet(const QPointF& lineA0, const QPointF& lineA1,
                            const QPointF& lineB0, const QPointF& lineB1,
                            const QPointF& intersection, double radius,
                            double tolerance)
{
    FilletResult result;
    if (radius <= tolerance) return result;

    const QPointF farA = distance(lineA0, intersection) > distance(lineA1, intersection) ? lineA0 : lineA1;
    const QPointF farB = distance(lineB0, intersection) > distance(lineB1, intersection) ? lineB0 : lineB1;
    const QPointF u = normalized(farA - intersection, tolerance);
    const QPointF v = normalized(farB - intersection, tolerance);
    if (lengthSquared(u) <= tolerance * tolerance || lengthSquared(v) <= tolerance * tolerance) return result;

    const double cosTheta = std::clamp(dot(u, v), -1.0, 1.0);
    const double theta = std::acos(cosTheta);
    if (theta <= tolerance || std::abs(kPi - theta) <= tolerance) return result;

    const double tangentDistance = radius / std::tan(theta * 0.5);
    const double lenA = distance(farA, intersection);
    const double lenB = distance(farB, intersection);
    if (tangentDistance > lenA + tolerance || tangentDistance > lenB + tolerance) return result;

    const QPointF bisector = normalized(u + v, tolerance);
    if (lengthSquared(bisector) <= tolerance * tolerance) return result;

    const double centerDistance = radius / std::sin(theta * 0.5);
    result.tangentA = intersection + u * tangentDistance;
    result.tangentB = intersection + v * tangentDistance;
    result.center = intersection + bisector * centerDistance;
    result.radius = radius;
    result.startAngleDeg = cadAngleDeg(result.center, result.tangentA);
    const double endAngle = cadAngleDeg(result.center, result.tangentB);
    result.spanAngleDeg = signedShortestSpanDeg(result.startAngleDeg, endAngle);
    result.valid = std::abs(result.spanAngleDeg) > kAngularToleranceDeg;
    return result;
}

double signedDistancePointToLine(const QPointF& point, const QPointF& lineA, const QPointF& lineB,
                                 double tolerance)
{
    const QPointF direction = lineB - lineA;
    const double len = length(direction);
    if (len <= tolerance) return 0.0;
    return cross(direction, point - lineA) / len;
}

int orientation(const QPointF& a, const QPointF& b, const QPointF& c, double tolerance)
{
    const double value = cross(b - a, c - a);
    if (std::abs(value) <= tolerance) return 0;
    return value > 0.0 ? 1 : -1;
}

bool areCollinear(const QPointF& a, const QPointF& b, const QPointF& c, double tolerance)
{
    return orientation(a, b, c, tolerance) == 0;
}

double rayParameter(const QPointF& point, const QPointF& rayOrigin, const QPointF& rayDirection,
                    double tolerance)
{
    const double len2 = lengthSquared(rayDirection);
    if (len2 <= tolerance * tolerance) return std::numeric_limits<double>::quiet_NaN();
    return dot(point - rayOrigin, rayDirection) / len2;
}

bool pointOnRay(const QPointF& point, const QPointF& rayOrigin, const QPointF& rayDirection,
                double tolerance)
{
    const double t = rayParameter(point, rayOrigin, rayDirection, tolerance);
    if (!std::isfinite(t) || t < -tolerance) return false;
    return distance(point, pointOnLine(rayOrigin, rayDirection, t)) <= tolerance;
}

QVector<QPointF> arcEndpoints(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg)
{
    if (radius < 0.0) return {};
    return {pointOnCadCircle(center, radius, startAngleDeg),
            pointOnCadCircle(center, radius, startAngleDeg + spanAngleDeg)};
}

QRectF circleBoundingRect(const QPointF& center, double radius)
{
    const double r = std::max(0.0, radius);
    return QRectF(center.x() - r, center.y() - r, 2.0 * r, 2.0 * r);
}

QRectF arcBoundingRect(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg,
                       double toleranceDeg)
{
    if (radius < 0.0) return QRectF();
    QVector<QPointF> points = arcEndpoints(center, radius, startAngleDeg, spanAngleDeg);
    for (double cardinal : {0.0, 90.0, 180.0, 270.0}) {
        if (pointOnCadArc(center, pointOnCadCircle(center, radius, cardinal), startAngleDeg, spanAngleDeg, toleranceDeg)) {
            appendUniquePoint(points, pointOnCadCircle(center, radius, cardinal), kDefaultTolerance);
        }
    }
    return boundingRect(points);
}

QRectF ellipseBoundingRect(const QRectF& ellipseRect)
{
    return ellipseRect.normalized();
}

QPointF pointOnEllipse(const QRectF& ellipseRect, double angleDeg)
{
    const QRectF r = ellipseRect.normalized();
    const QPointF c = r.center();
    const double rx = r.width() * 0.5;
    const double ry = r.height() * 0.5;
    const double a = qDegreesToRadians(angleDeg);
    return QPointF(c.x() + rx * std::cos(a), c.y() - ry * std::sin(a));
}

QVector<QPointF> approximateArc(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg,
                                double maxAngleStepDeg)
{
    QVector<QPointF> points;
    if (radius <= kDefaultTolerance || std::abs(spanAngleDeg) <= kAngularToleranceDeg) return points;
    const double step = std::max(0.5, std::abs(maxAngleStepDeg));
    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(spanAngleDeg) / step)));
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        points.append(pointOnCadCircle(center, radius, startAngleDeg + spanAngleDeg * t));
    }
    return points;
}

QVector<QPointF> approximateEllipse(const QRectF& ellipseRect, int minimumSegments)
{
    QVector<QPointF> points;
    const QRectF r = ellipseRect.normalized();
    const double rx = r.width() * 0.5;
    const double ry = r.height() * 0.5;
    if (rx <= kDefaultTolerance || ry <= kDefaultTolerance) return points;
    const int segments = std::max(12, minimumSegments);
    points.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        const double angle = 360.0 * static_cast<double>(i) / static_cast<double>(segments);
        points.append(pointOnEllipse(r, angle));
    }
    return points;
}

QVector<QPointF> approximateEllipseArc(const QRectF& ellipseRect, double startAngleDeg, double spanAngleDeg,
                                       double maxAngleStepDeg)
{
    QVector<QPointF> points;
    const QRectF r = ellipseRect.normalized();
    if (r.width() <= kDefaultTolerance || r.height() <= kDefaultTolerance || std::abs(spanAngleDeg) <= kAngularToleranceDeg) return points;
    const double step = std::max(0.5, std::abs(maxAngleStepDeg));
    const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(spanAngleDeg) / step)));
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        points.append(pointOnEllipse(r, startAngleDeg + spanAngleDeg * t));
    }
    return points;
}

bool pointOnCircle(const QPointF& point, const QPointF& center, double radius, double tolerance)
{
    if (radius < 0.0) return false;
    return std::abs(distance(point, center) - radius) <= tolerance;
}

bool pointOnEllipse(const QPointF& point, const QRectF& ellipseRect, double tolerance)
{
    const QRectF r = ellipseRect.normalized();
    const double rx = r.width() * 0.5;
    const double ry = r.height() * 0.5;
    if (rx <= tolerance || ry <= tolerance) return false;
    const QPointF c = r.center();
    const double nx = (point.x() - c.x()) / rx;
    const double ny = (point.y() - c.y()) / ry;
    return std::abs(nx * nx + ny * ny - 1.0) <= tolerance;
}

bool pointOnPolyline(const QPointF& point, const QVector<QPointF>& points, bool closed, double tolerance)
{
    if (points.size() < 2) return false;
    for (int i = 0; i + 1 < points.size(); ++i) {
        if (pointOnSegment(point, points.at(i), points.at(i + 1), tolerance)) return true;
    }
    return closed && points.size() > 2 && pointOnSegment(point, points.last(), points.first(), tolerance);
}

double polylineLength(const QVector<QPointF>& points, bool closed)
{
    if (points.size() < 2) return 0.0;
    double value = 0.0;
    for (int i = 0; i + 1 < points.size(); ++i) value += distance(points.at(i), points.at(i + 1));
    if (closed && points.size() > 2) value += distance(points.last(), points.first());
    return value;
}

QPointF polygonCentroid(const QVector<QPointF>& polygon, double tolerance)
{
    if (polygon.isEmpty()) return QPointF();
    if (polygon.size() < 3) return boundingRect(polygon).center();

    double signedArea2 = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF& a = polygon.at(i);
        const QPointF& b = polygon.at((i + 1) % polygon.size());
        const double crossValue = a.x() * b.y() - b.x() * a.y();
        signedArea2 += crossValue;
        cx += (a.x() + b.x()) * crossValue;
        cy += (a.y() + b.y()) * crossValue;
    }
    if (std::abs(signedArea2) <= tolerance) return boundingRect(polygon).center();
    return QPointF(cx / (3.0 * signedArea2), cy / (3.0 * signedArea2));
}

QVector<QPointF> simplifyPolyline(const QVector<QPointF>& points, bool closed,
                                  double distanceTolerance, double collinearTolerance)
{
    QVector<QPointF> compact;
    compact.reserve(points.size());
    for (const QPointF& point : points) {
        if (compact.isEmpty() || distance(compact.last(), point) > distanceTolerance) compact.append(point);
    }
    if (closed && compact.size() > 1 && distance(compact.first(), compact.last()) <= distanceTolerance) compact.removeLast();
    if (compact.size() < 3) return compact;

    bool changed = true;
    while (changed && compact.size() >= 3) {
        changed = false;
        QVector<QPointF> next;
        next.reserve(compact.size());
        for (int i = 0; i < compact.size(); ++i) {
            const int prev = i - 1;
            const int nextIndex = i + 1;
            if (!closed && (i == 0 || i == compact.size() - 1)) {
                next.append(compact.at(i));
                continue;
            }
            const QPointF& a = compact.at((prev + compact.size()) % compact.size());
            const QPointF& b = compact.at(i);
            const QPointF& c = compact.at(nextIndex % compact.size());
            if (distance(a, b) <= distanceTolerance || distance(b, c) <= distanceTolerance || areCollinear(a, b, c, collinearTolerance)) {
                changed = true;
                continue;
            }
            next.append(b);
        }
        if (next.size() < (closed ? 3 : 2)) break;
        compact = next;
    }
    return compact;
}

ClosestPointResult closestPointOnPolyline(const QPointF& point, const QVector<QPointF>& points, bool closed,
                                          double tolerance)
{
    ClosestPointResult result;
    if (points.size() < 2) return result;

    const auto evaluate = [&](const QPointF& a, const QPointF& b, int segmentIndex) {
        double parameter = 0.0;
        const QPointF candidate = nearestPointOnSegment(point, a, b, &parameter);
        const double d = distance(point, candidate);
        if (!result.valid || d < result.distance) {
            result.valid = true;
            result.point = candidate;
            result.segmentIndex = segmentIndex;
            result.parameter = parameter;
            result.distance = d;
        }
    };

    for (int i = 0; i + 1 < points.size(); ++i) evaluate(points.at(i), points.at(i + 1), i);
    if (closed && points.size() > 2) evaluate(points.last(), points.first(), points.size() - 1);
    if (result.distance <= tolerance) return result;
    return result;
}

IntersectionResult intersectSegmentEllipse(const QPointF& segmentA, const QPointF& segmentB,
                                           const QRectF& ellipseRect, double tolerance)
{
    const IntersectionResult lineHits = intersectInfiniteLineEllipse(segmentA, segmentB, ellipseRect, tolerance);
    IntersectionResult result;
    for (const QPointF& point : lineHits.points) {
        if (pointOnSegment(point, segmentA, segmentB, tolerance)) appendUniquePoint(result.points, point, tolerance);
    }
    if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectPolylineSegment(const QVector<QPointF>& points, bool closed,
                                            const QPointF& segmentA, const QPointF& segmentB,
                                            double tolerance)
{
    IntersectionResult result;
    if (points.size() < 2) return result;
    for (int i = 0; i + 1 < points.size(); ++i) {
        const IntersectionResult hit = intersectLineSegments(points.at(i), points.at(i + 1), segmentA, segmentB, tolerance);
        for (const QPointF& point : hit.points) appendUniquePoint(result.points, point, tolerance);
        if (hit.kind == IntersectionKind::Overlap && result.kind == IntersectionKind::None) result.kind = IntersectionKind::Overlap;
    }
    if (closed && points.size() > 2) {
        const IntersectionResult hit = intersectLineSegments(points.last(), points.first(), segmentA, segmentB, tolerance);
        for (const QPointF& point : hit.points) appendUniquePoint(result.points, point, tolerance);
        if (hit.kind == IntersectionKind::Overlap && result.kind == IntersectionKind::None) result.kind = IntersectionKind::Overlap;
    }
    if (result.kind != IntersectionKind::Overlap) {
        if (result.points.size() == 1) result.kind = IntersectionKind::One;
        else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    }
    return result;
}

IntersectionResult intersectPolylines(const QVector<QPointF>& first, bool firstClosed,
                                      const QVector<QPointF>& second, bool secondClosed,
                                      double tolerance)
{
    IntersectionResult result;
    if (first.size() < 2 || second.size() < 2) return result;
    const int firstSegmentCount = firstClosed ? first.size() : first.size() - 1;
    for (int i = 0; i < firstSegmentCount; ++i) {
        const QPointF a = first.at(i);
        const QPointF b = first.at((i + 1) % first.size());
        const IntersectionResult hit = intersectPolylineSegment(second, secondClosed, a, b, tolerance);
        for (const QPointF& point : hit.points) appendUniquePoint(result.points, point, tolerance);
        if (hit.kind == IntersectionKind::Overlap && result.kind == IntersectionKind::None) result.kind = IntersectionKind::Overlap;
    }
    if (result.kind != IntersectionKind::Overlap) {
        if (result.points.size() == 1) result.kind = IntersectionKind::One;
        else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    }
    return result;
}

IntersectionResult intersectCirclePolyline(const QPointF& center, double radius,
                                           const QVector<QPointF>& points, bool closed,
                                           double tolerance)
{
    IntersectionResult result;
    if (points.size() < 2 || radius < 0.0) return result;
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const IntersectionResult hit = intersectSegmentCircle(points.at(i), points.at((i + 1) % points.size()), center, radius, tolerance);
        for (const QPointF& point : hit.points) appendUniquePoint(result.points, point, tolerance);
    }
    if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectEllipsePolyline(const QRectF& ellipseRect,
                                            const QVector<QPointF>& points, bool closed,
                                            double tolerance)
{
    IntersectionResult result;
    if (points.size() < 2) return result;
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const IntersectionResult hit = intersectSegmentEllipse(points.at(i), points.at((i + 1) % points.size()), ellipseRect, tolerance);
        for (const QPointF& point : hit.points) appendUniquePoint(result.points, point, tolerance);
    }
    if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectCadArcSegment(const QPointF& center, double radius,
                                          double startAngleDeg, double spanAngleDeg,
                                          const QPointF& segmentA, const QPointF& segmentB,
                                          double tolerance)
{
    const IntersectionResult circleHits = intersectSegmentCircle(segmentA, segmentB, center, radius, tolerance);
    IntersectionResult result;
    for (const QPointF& point : circleHits.points) {
        if (pointOnCadArc(center, point, startAngleDeg, spanAngleDeg)) appendUniquePoint(result.points, point, tolerance);
    }
    if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectCadArcCircle(const QPointF& center, double radius,
                                         double startAngleDeg, double spanAngleDeg,
                                         const QPointF& circleCenter, double circleRadius,
                                         double tolerance)
{
    const IntersectionResult circleHits = intersectCircleCircle(center, radius, circleCenter, circleRadius, tolerance);
    IntersectionResult result;
    for (const QPointF& point : circleHits.points) {
        if (pointOnCadArc(center, point, startAngleDeg, spanAngleDeg)) appendUniquePoint(result.points, point, tolerance);
    }
    if (circleHits.kind == IntersectionKind::Overlap && std::abs(spanAngleDeg) >= 360.0 - kAngularToleranceDeg) result.kind = IntersectionKind::Overlap;
    else if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectCadArcs(const QPointF& centerA, double radiusA,
                                    double startAngleDegA, double spanAngleDegA,
                                    const QPointF& centerB, double radiusB,
                                    double startAngleDegB, double spanAngleDegB,
                                    double tolerance)
{
    const IntersectionResult circleHits = intersectCircleCircle(centerA, radiusA, centerB, radiusB, tolerance);
    IntersectionResult result;
    for (const QPointF& point : circleHits.points) {
        if (pointOnCadArc(centerA, point, startAngleDegA, spanAngleDegA)
            && pointOnCadArc(centerB, point, startAngleDegB, spanAngleDegB)) {
            appendUniquePoint(result.points, point, tolerance);
        }
    }
    if (circleHits.kind == IntersectionKind::Overlap) result.kind = result.points.isEmpty() ? IntersectionKind::Overlap : IntersectionKind::Two;
    else if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectCadArcPolyline(const QPointF& center, double radius,
                                           double startAngleDeg, double spanAngleDeg,
                                           const QVector<QPointF>& points, bool closed,
                                           double tolerance)
{
    IntersectionResult result;
    if (points.size() < 2) return result;
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const IntersectionResult hit = intersectCadArcSegment(center, radius, startAngleDeg, spanAngleDeg,
                                                              points.at(i), points.at((i + 1) % points.size()), tolerance);
        for (const QPointF& point : hit.points) appendUniquePoint(result.points, point, tolerance);
    }
    if (result.points.size() == 1) result.kind = IntersectionKind::One;
    else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    return result;
}

IntersectionResult intersectCadArcEllipse(const QPointF& center, double radius,
                                          double startAngleDeg, double spanAngleDeg,
                                          const QRectF& ellipseRect,
                                          double tolerance)
{
    // Analytical circle/ellipse intersection is quartic. For editor interaction, use a dense, tolerance-filtered ellipse polyline.
    const QVector<QPointF> ellipse = approximateEllipse(ellipseRect, 256);
    return intersectCadArcPolyline(center, radius, startAngleDeg, spanAngleDeg, ellipse, true, tolerance);
}

QPointF closestPointOnCircle(const QPointF& point, const QPointF& center, double radius, double tolerance)
{
    if (radius <= tolerance) return center;
    const QPointF direction = normalized(point - center, tolerance);
    if (lengthSquared(direction) <= tolerance * tolerance) return pointOnCadCircle(center, radius, 0.0);
    return center + direction * radius;
}

QPointF closestPointOnCadArc(const QPointF& point, const QPointF& center, double radius,
                             double startAngleDeg, double spanAngleDeg, double tolerance)
{
    if (radius <= tolerance) return center;
    const QPointF circlePoint = closestPointOnCircle(point, center, radius, tolerance);
    if (pointOnCadArc(center, circlePoint, startAngleDeg, spanAngleDeg)) return circlePoint;
    const QVector<QPointF> ends = arcEndpoints(center, radius, startAngleDeg, spanAngleDeg);
    if (ends.size() < 2) return center;
    return distance(point, ends.first()) <= distance(point, ends.last()) ? ends.first() : ends.last();
}

QPointF closestPointOnEllipse(const QPointF& point, const QRectF& ellipseRect, int samples, double tolerance)
{
    const QRectF r = ellipseRect.normalized();
    if (r.width() <= tolerance || r.height() <= tolerance) return r.center();
    const int count = std::max(32, samples);
    QPointF best = pointOnEllipse(r, 0.0);
    double bestDistance = distance(point, best);
    for (int i = 1; i < count; ++i) {
        const QPointF candidate = pointOnEllipse(r, 360.0 * static_cast<double>(i) / static_cast<double>(count));
        const double d = distance(point, candidate);
        if (d < bestDistance) {
            bestDistance = d;
            best = candidate;
        }
    }
    return best;
}

TrimSegmentResult trimSegmentAtPoint(const QPointF& segmentA, const QPointF& segmentB,
                                     const QPointF& trimPoint, bool trimStartSide,
                                     double tolerance)
{
    TrimSegmentResult result;
    if (isDegenerateSegment(segmentA, segmentB, tolerance)) return result;
    if (!pointOnSegment(trimPoint, segmentA, segmentB, tolerance)) return result;
    result.valid = true;
    result.start = trimStartSide ? trimPoint : segmentA;
    result.end = trimStartSide ? segmentB : trimPoint;
    result.valid = !isDegenerateSegment(result.start, result.end, tolerance);
    return result;
}

TrimSegmentResult extendSegmentToPoint(const QPointF& segmentA, const QPointF& segmentB,
                                       const QPointF& targetPoint, bool extendStartSide,
                                       double tolerance)
{
    TrimSegmentResult result;
    if (isDegenerateSegment(segmentA, segmentB, tolerance)) return result;
    if (distancePointToSegment(targetPoint, segmentA, segmentB) > tolerance
        && std::abs(signedDistancePointToLine(targetPoint, segmentA, segmentB, tolerance)) > tolerance) return result;

    const QPointF direction = segmentB - segmentA;
    const double t = rayParameter(targetPoint, segmentA, direction, tolerance);
    if (!std::isfinite(t)) return result;
    if (extendStartSide && t > tolerance) return result;
    if (!extendStartSide && t < 1.0 - tolerance) return result;

    result.valid = true;
    result.start = extendStartSide ? targetPoint : segmentA;
    result.end = extendStartSide ? segmentB : targetPoint;
    return !isDegenerateSegment(result.start, result.end, tolerance) ? result : TrimSegmentResult{};
}

bool chooseNearestPointOnRay(const QPointF& rayOrigin, const QPointF& rayDirection,
                             const QVector<QPointF>& candidates, QPointF& chosen,
                             double* chosenDistance, double tolerance)
{
    if (lengthSquared(rayDirection) <= tolerance * tolerance) return false;
    bool found = false;
    double bestDistance = std::numeric_limits<double>::max();
    for (const QPointF& candidate : candidates) {
        if (!pointOnRay(candidate, rayOrigin, rayDirection, tolerance)) continue;
        const double d = distance(rayOrigin, candidate);
        if (d <= tolerance) continue;
        if (!found || d < bestDistance) {
            found = true;
            bestDistance = d;
            chosen = candidate;
        }
    }
    if (found && chosenDistance) *chosenDistance = bestDistance;
    return found;
}

CircleTangentResult tangentPointsFromPointToCircle(const QPointF& externalPoint,
                                                   const QPointF& center, double radius,
                                                   double tolerance)
{
    CircleTangentResult result;
    if (radius <= tolerance) return result;
    const double d = distance(externalPoint, center);
    if (d < radius - tolerance) return result;
    if (std::abs(d - radius) <= tolerance) {
        result.valid = true;
        result.points.append(externalPoint);
        return result;
    }

    const double base = std::atan2(externalPoint.y() - center.y(), externalPoint.x() - center.x());
    const double offset = std::acos(radius / d);
    const double tangentRadius = radius;
    for (double angle : {base + offset, base - offset}) {
        result.points.append(QPointF(center.x() + tangentRadius * std::cos(angle),
                                     center.y() + tangentRadius * std::sin(angle)));
    }
    result.valid = !result.points.isEmpty();
    return result;
}

} // namespace CadGeometry
