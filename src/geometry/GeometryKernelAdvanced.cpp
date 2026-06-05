#include "GeometryKernelAdvanced.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace CadGeometry {

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

void appendUnique(QVector<QPointF>& points, const QPointF& point, double tolerance)
{
    if (!isFinite(point)) return;
    for (const QPointF& existing : points) {
        if (distance(existing, point) <= tolerance) return;
    }
    points.append(point);
}

SnapCandidate makeSnap(SnapKind kind, const QPointF& point, const QPointF& cursor, int sourceIndex = -1)
{
    SnapCandidate snap;
    snap.valid = isFinite(point);
    snap.kind = kind;
    snap.point = point;
    snap.distance = distance(cursor, point);
    snap.sourceIndex = sourceIndex;
    return snap;
}

double safeAcos(double value)
{
    return std::acos(std::clamp(value, -1.0, 1.0));
}

QPointF lerp(const QPointF& a, const QPointF& b, double t)
{
    return a + (b - a) * t;
}

bool isInsideHalfPlane(const QPointF& point, const QPointF& edgeA, const QPointF& edgeB, bool clipClockwise, double tolerance)
{
    const double c = cross(edgeB - edgeA, point - edgeA);
    return clipClockwise ? c <= tolerance : c >= -tolerance;
}

QPointF lineIntersectionFallback(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d)
{
    const IntersectionResult hit = intersectInfiniteLines(a, b, c, d);
    return hit.points.isEmpty() ? b : hit.points.first();
}

QVector<QPointF> sutherlandHodgmanClip(const QVector<QPointF>& subject, const QVector<QPointF>& clip, double tolerance)
{
    if (subject.isEmpty() || clip.size() < 3) return {};
    QVector<QPointF> output = subject;
    const bool clipCw = isClockwise(clip);

    for (int i = 0; i < clip.size(); ++i) {
        const QPointF edgeA = clip.at(i);
        const QPointF edgeB = clip.at((i + 1) % clip.size());
        QVector<QPointF> input = output;
        output.clear();
        if (input.isEmpty()) break;

        QPointF previous = input.last();
        bool previousInside = isInsideHalfPlane(previous, edgeA, edgeB, clipCw, tolerance);
        for (const QPointF& current : input) {
            const bool currentInside = isInsideHalfPlane(current, edgeA, edgeB, clipCw, tolerance);
            if (currentInside) {
                if (!previousInside) {
                    appendUnique(output, lineIntersectionFallback(previous, current, edgeA, edgeB), tolerance);
                }
                appendUnique(output, current, tolerance);
            } else if (previousInside) {
                appendUnique(output, lineIntersectionFallback(previous, current, edgeA, edgeB), tolerance);
            }
            previous = current;
            previousInside = currentInside;
        }
    }
    return output;
}

QPointF catmullRomPoint(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3,
                        double t, double tension)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double s = tension;
    const QPointF m1 = (p2 - p0) * s;
    const QPointF m2 = (p3 - p1) * s;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    return p1 * h00 + m1 * h10 + p2 * h01 + m2 * h11;
}

int findKnotSpan(int controlPointCount, int degree, const QVector<double>& knots, double u)
{
    const int n = controlPointCount - 1;
    if (n < degree) return -1;
    if (u >= knots.at(n + 1)) return n;
    if (u <= knots.at(degree)) return degree;
    int low = degree;
    int high = n + 1;
    int mid = (low + high) / 2;
    while (u < knots.at(mid) || u >= knots.at(mid + 1)) {
        if (u < knots.at(mid)) high = mid;
        else low = mid;
        mid = (low + high) / 2;
    }
    return mid;
}

QVector<QPointF> segmentPortionInsideConvex(const QPointF& a, const QPointF& b,
                                            const QVector<QPointF>& clip, double tolerance)
{
    // Liang-Barsky style clipping against convex half-planes.
    if (clip.size() < 3) return {};
    QPointF p0 = a;
    QPointF p1 = b;
    QVector<QPointF> subject;
    subject.append(p0);
    subject.append(p1);
    QVector<QPointF> clipped = sutherlandHodgmanClip(subject, clip, tolerance);
    if (clipped.size() < 2) return {};

    // Sutherland-Hodgman can return duplicate endpoints for a segment; keep the two farthest points.
    QPointF first = clipped.first();
    QPointF second = clipped.first();
    double best = -1.0;
    for (const QPointF& x : clipped) {
        for (const QPointF& y : clipped) {
            const double d = distance(x, y);
            if (d > best) {
                best = d;
                first = x;
                second = y;
            }
        }
    }
    if (best <= tolerance) return {};
    return {first, second};
}

} // namespace

Transform2D identityTransform()
{
    return Transform2D{};
}

Transform2D translationTransform(double dx, double dy)
{
    Transform2D transform;
    transform.dx = dx;
    transform.dy = dy;
    return transform;
}

Transform2D rotationTransform(const QPointF& center, double angleDeg)
{
    const double a = qDegreesToRadians(angleDeg);
    const double c = std::cos(a);
    const double s = std::sin(a);
    Transform2D transform;
    transform.m11 = c;
    transform.m12 = -s;
    transform.m21 = s;
    transform.m22 = c;
    transform.dx = center.x() - (transform.m11 * center.x() + transform.m12 * center.y());
    transform.dy = center.y() - (transform.m21 * center.x() + transform.m22 * center.y());
    return transform;
}

Transform2D scaleTransform(const QPointF& center, double sx, double sy)
{
    Transform2D transform;
    transform.m11 = sx;
    transform.m22 = sy;
    transform.dx = center.x() - sx * center.x();
    transform.dy = center.y() - sy * center.y();
    return transform;
}

Transform2D mirrorTransform(const QPointF& axisA, const QPointF& axisB, double tolerance)
{
    if (isDegenerateSegment(axisA, axisB, tolerance)) return identityTransform();
    const QPointF u = normalized(axisB - axisA, tolerance);
    const double ux = u.x();
    const double uy = u.y();

    // Reflection matrix around an axis through the origin, then translated to axisA.
    Transform2D raw;
    raw.m11 = 2.0 * ux * ux - 1.0;
    raw.m12 = 2.0 * ux * uy;
    raw.m21 = 2.0 * ux * uy;
    raw.m22 = 2.0 * uy * uy - 1.0;
    raw.dx = axisA.x() - (raw.m11 * axisA.x() + raw.m12 * axisA.y());
    raw.dy = axisA.y() - (raw.m21 * axisA.x() + raw.m22 * axisA.y());
    return raw;
}

Transform2D multiplyTransforms(const Transform2D& first, const Transform2D& second)
{
    // Returns first(second(point)).
    Transform2D r;
    r.m11 = first.m11 * second.m11 + first.m12 * second.m21;
    r.m12 = first.m11 * second.m12 + first.m12 * second.m22;
    r.m21 = first.m21 * second.m11 + first.m22 * second.m21;
    r.m22 = first.m21 * second.m12 + first.m22 * second.m22;
    r.dx = first.m11 * second.dx + first.m12 * second.dy + first.dx;
    r.dy = first.m21 * second.dx + first.m22 * second.dy + first.dy;
    return r;
}

QPointF transformPoint(const QPointF& point, const Transform2D& transform)
{
    return QPointF(transform.m11 * point.x() + transform.m12 * point.y() + transform.dx,
                   transform.m21 * point.x() + transform.m22 * point.y() + transform.dy);
}

QVector<QPointF> transformPoints(const QVector<QPointF>& points, const Transform2D& transform)
{
    QVector<QPointF> result;
    result.reserve(points.size());
    for (const QPointF& point : points) result.append(transformPoint(point, transform));
    return result;
}

QVector<Transform2D> rectangularArrayTransforms(int rows, int columns, double rowSpacing, double columnSpacing)
{
    QVector<Transform2D> transforms;
    if (rows <= 0 || columns <= 0) return transforms;
    transforms.reserve(rows * columns);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            transforms.append(translationTransform(c * columnSpacing, r * rowSpacing));
        }
    }
    return transforms;
}

QVector<Transform2D> polarArrayTransforms(const QPointF& center, int count, double totalAngleDeg, bool rotateItems)
{
    QVector<Transform2D> transforms;
    if (count <= 0) return transforms;
    transforms.reserve(count);
    const double step = count == 1 ? 0.0 : totalAngleDeg / static_cast<double>(count);
    for (int i = 0; i < count; ++i) {
        const double angle = step * i;
        Transform2D rotation = rotationTransform(center, angle);
        if (!rotateItems) {
            const QPointF movedCenter = rotatePoint(center + QPointF(1.0, 0.0), center, angle) - QPointF(1.0, 0.0);
            rotation = translationTransform(movedCenter.x() - center.x(), movedCenter.y() - center.y());
        }
        transforms.append(rotation);
    }
    return transforms;
}

CircleSolution circleFromTwoPoints(const QPointF& a, const QPointF& b, double tolerance)
{
    CircleSolution result;
    const double d = distance(a, b);
    if (d <= tolerance) return result;
    result.valid = true;
    result.center = (a + b) * 0.5;
    result.radius = d * 0.5;
    return result;
}

CircleSolution circleFromThreePoints(const QPointF& a, const QPointF& b, const QPointF& c, double tolerance)
{
    CircleSolution result;
    const double ax = a.x();
    const double ay = a.y();
    const double bx = b.x();
    const double by = b.y();
    const double cx = c.x();
    const double cy = c.y();
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) <= tolerance) return result;

    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    const double ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
    const double uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    result.center = QPointF(ux, uy);
    result.radius = distance(result.center, a);
    result.valid = result.radius > tolerance && isFinite(result.center);
    return result;
}

CircleSolution circleFromCenterRadius(const QPointF& center, double radius, double tolerance)
{
    CircleSolution result;
    if (radius <= tolerance || !isFinite(center)) return result;
    result.valid = true;
    result.center = center;
    result.radius = radius;
    return result;
}

CircleSolution circleFromCenterDiameterPoint(const QPointF& center, const QPointF& diameterPoint, double tolerance)
{
    return circleFromCenterRadius(center, distance(center, diameterPoint), tolerance);
}

ArcSolution arcFromThreePoints(const QPointF& start, const QPointF& pointOnArc, const QPointF& end, double tolerance)
{
    ArcSolution result;
    const CircleSolution circle = circleFromThreePoints(start, pointOnArc, end, tolerance);
    if (!circle.valid) return result;

    const double startAngle = cadAngleDeg(circle.center, start);
    const double endAngle = cadAngleDeg(circle.center, end);
    double span = ccwSpanDeg(startAngle, endAngle);
    if (!pointOnCadArc(circle.center, pointOnArc, startAngle, span)) {
        span = -ccwSpanDeg(endAngle, startAngle);
    }

    result.valid = true;
    result.center = circle.center;
    result.radius = circle.radius;
    result.startAngleDeg = startAngle;
    result.spanAngleDeg = span;
    return result;
}

ArcSolution arcFromCenterStartEnd(const QPointF& center, const QPointF& start, const QPointF& end,
                                  bool clockwise, double tolerance)
{
    ArcSolution result;
    const double radius = distance(center, start);
    if (radius <= tolerance || std::abs(distance(center, end) - radius) > std::max(tolerance, radius * 1.0e-7)) return result;
    const double startAngle = cadAngleDeg(center, start);
    const double endAngle = cadAngleDeg(center, end);
    result.valid = true;
    result.center = center;
    result.radius = radius;
    result.startAngleDeg = startAngle;
    result.spanAngleDeg = clockwise ? -ccwSpanDeg(endAngle, startAngle) : ccwSpanDeg(startAngle, endAngle);
    return result;
}

QPointF linePointAtDistance(const QPointF& start, const QPointF& end, double distanceFromStart, double tolerance)
{
    const QPointF dir = normalized(end - start, tolerance);
    if (lengthSquared(dir) <= tolerance * tolerance) return start;
    return start + dir * distanceFromStart;
}

QVector<QPointF> parallelLineThroughPoint(const QPointF& lineA, const QPointF& lineB, const QPointF& throughPoint,
                                          double tolerance)
{
    QVector<QPointF> result;
    const QPointF direction = lineB - lineA;
    if (lengthSquared(direction) <= tolerance * tolerance) return result;
    result.append(throughPoint);
    result.append(throughPoint + direction);
    return result;
}

QVector<QPointF> perpendicularLineThroughPoint(const QPointF& lineA, const QPointF& lineB, const QPointF& throughPoint,
                                               double lengthHint, double tolerance)
{
    QVector<QPointF> result;
    const QPointF n = perpendicularLeft(lineB - lineA, tolerance);
    if (lengthSquared(n) <= tolerance * tolerance) return result;
    const double half = std::max(lengthHint, tolerance) * 0.5;
    result.append(throughPoint - n * half);
    result.append(throughPoint + n * half);
    return result;
}

QPointF angleBisectorDirection(const QPointF& vertex, const QPointF& pointA, const QPointF& pointB,
                               bool external, double tolerance)
{
    const QPointF a = normalized(pointA - vertex, tolerance);
    const QPointF b = normalized(pointB - vertex, tolerance);
    if (lengthSquared(a) <= tolerance * tolerance) return b;
    if (lengthSquared(b) <= tolerance * tolerance) return a;
    QPointF bisector = external ? a - b : a + b;
    if (lengthSquared(bisector) <= tolerance * tolerance) bisector = perpendicularLeft(a, tolerance);
    return normalized(bisector, tolerance);
}

BulgeArc bulgeToArc(const QPointF& start, const QPointF& end, double bulge, double tolerance)
{
    BulgeArc result;
    const double chord = distance(start, end);
    if (chord <= tolerance || std::abs(bulge) <= tolerance) return result;

    const double thetaRad = 4.0 * std::atan(bulge);
    const double absTheta = std::abs(thetaRad);
    const double radius = chord / (2.0 * std::sin(absTheta * 0.5));
    const double halfChord = chord * 0.5;
    const double h2 = std::max(0.0, radius * radius - halfChord * halfChord);
    const double h = std::sqrt(h2);
    const QPointF midpoint = (start + end) * 0.5;
    const QPointF u = normalized(end - start, tolerance);
    const QPointF n = perpendicularLeft(u, tolerance);
    const QPointF c1 = midpoint + n * h;
    const QPointF c2 = midpoint - n * h;

    const double wantedDeg = qRadiansToDegrees(absTheta);
    auto scoreCenter = [&](const QPointF& center) {
        const double sa = cadAngleDeg(center, start);
        const double ea = cadAngleDeg(center, end);
        const double candidateSpan = bulge >= 0.0 ? ccwSpanDeg(sa, ea) : ccwSpanDeg(ea, sa);
        return std::abs(candidateSpan - wantedDeg);
    };
    const QPointF center = scoreCenter(c1) <= scoreCenter(c2) ? c1 : c2;

    result.valid = true;
    result.center = center;
    result.radius = radius;
    result.startAngleDeg = cadAngleDeg(center, start);
    result.spanAngleDeg = bulge >= 0.0 ? wantedDeg : -wantedDeg;
    return result;
}

double bulgeFromIncludedAngle(double includedAngleDeg)
{
    return std::tan(qDegreesToRadians(includedAngleDeg) * 0.25);
}

QVector<QPointF> approximateBulgedPolyline(const QVector<QPointF>& points, const QVector<double>& bulges,
                                           bool closed, double maxAngleStepDeg, double tolerance)
{
    QVector<QPointF> result;
    if (points.isEmpty()) return result;
    if (points.size() == 1) return points;

    result.append(points.first());
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const QPointF a = points.at(i);
        const QPointF b = points.at((i + 1) % points.size());
        const double bulge = i < bulges.size() ? bulges.at(i) : 0.0;
        if (std::abs(bulge) <= tolerance) {
            if (!closed || i + 1 < points.size()) result.append(b);
            continue;
        }
        const BulgeArc arc = bulgeToArc(a, b, bulge, tolerance);
        if (!arc.valid) {
            if (!closed || i + 1 < points.size()) result.append(b);
            continue;
        }
        QVector<QPointF> arcPoints = approximateArc(arc.center, arc.radius, arc.startAngleDeg, arc.spanAngleDeg, maxAngleStepDeg);
        for (int j = 1; j < arcPoints.size(); ++j) {
            if (closed && i == segmentCount - 1 && j == arcPoints.size() - 1) continue;
            result.append(arcPoints.at(j));
        }
    }
    return simplifyPolyline(result, closed, tolerance, tolerance);
}

QPointF quadraticBezierPoint(const QPointF& p0, const QPointF& p1, const QPointF& p2, double t)
{
    const double u = 1.0 - t;
    return p0 * (u * u) + p1 * (2.0 * u * t) + p2 * (t * t);
}

QPointF cubicBezierPoint(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, double t)
{
    const double u = 1.0 - t;
    const double u2 = u * u;
    const double t2 = t * t;
    return p0 * (u2 * u) + p1 * (3.0 * u2 * t) + p2 * (3.0 * u * t2) + p3 * (t2 * t);
}

QPointF cubicBezierDerivative(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, double t)
{
    const double u = 1.0 - t;
    return (p1 - p0) * (3.0 * u * u) + (p2 - p1) * (6.0 * u * t) + (p3 - p2) * (3.0 * t * t);
}

QVector<QPointF> approximateCubicBezier(const QPointF& p0, const QPointF& p1,
                                        const QPointF& p2, const QPointF& p3, int segments)
{
    QVector<QPointF> result;
    const int count = std::max(1, segments);
    result.reserve(count + 1);
    for (int i = 0; i <= count; ++i) {
        result.append(cubicBezierPoint(p0, p1, p2, p3, static_cast<double>(i) / static_cast<double>(count)));
    }
    return result;
}

QVector<double> createClampedUniformKnotVector(int controlPointCount, int degree)
{
    QVector<double> knots;
    if (controlPointCount <= degree || degree < 1) return knots;
    const int knotCount = controlPointCount + degree + 1;
    knots.reserve(knotCount);
    const int interior = controlPointCount - degree - 1;
    for (int i = 0; i < knotCount; ++i) {
        if (i <= degree) knots.append(0.0);
        else if (i >= controlPointCount) knots.append(1.0);
        else knots.append(static_cast<double>(i - degree) / static_cast<double>(interior + 1));
    }
    return knots;
}

QPointF evaluateBSpline(const QVector<QPointF>& controlPoints, int degree,
                        const QVector<double>& knots, double u, double tolerance)
{
    if (controlPoints.size() <= degree || degree < 1 || knots.size() != controlPoints.size() + degree + 1) return QPointF();
    const double low = knots.at(degree);
    const double high = knots.at(controlPoints.size());
    if (high <= low + tolerance) return controlPoints.first();
    u = std::clamp(u, low, high);
    const int span = findKnotSpan(controlPoints.size(), degree, knots, u);
    if (span < degree) return QPointF();

    QVector<QPointF> d;
    d.reserve(degree + 1);
    for (int j = 0; j <= degree; ++j) d.append(controlPoints.at(span - degree + j));

    for (int r = 1; r <= degree; ++r) {
        for (int j = degree; j >= r; --j) {
            const int knotIndex = span - degree + j;
            const double denom = knots.at(knotIndex + degree + 1 - r) - knots.at(knotIndex);
            const double alpha = std::abs(denom) <= tolerance ? 0.0 : (u - knots.at(knotIndex)) / denom;
            d[j] = d.at(j - 1) * (1.0 - alpha) + d.at(j) * alpha;
        }
    }
    return d.at(degree);
}

QPointF evaluateNurbs(const QVector<QPointF>& controlPoints, const QVector<double>& weights, int degree,
                      const QVector<double>& knots, double u, double tolerance)
{
    if (controlPoints.size() != weights.size() || controlPoints.isEmpty()) return QPointF();
    QVector<QPointF> weighted;
    weighted.reserve(controlPoints.size());
    QVector<QPointF> weightPoints;
    weightPoints.reserve(controlPoints.size());
    for (int i = 0; i < controlPoints.size(); ++i) {
        const double w = std::max(weights.at(i), tolerance);
        weighted.append(controlPoints.at(i) * w);
        weightPoints.append(QPointF(w, 0.0));
    }
    const QPointF numerator = evaluateBSpline(weighted, degree, knots, u, tolerance);
    const QPointF denominatorPoint = evaluateBSpline(weightPoints, degree, knots, u, tolerance);
    const double denominator = denominatorPoint.x();
    if (std::abs(denominator) <= tolerance) return QPointF();
    return numerator / denominator;
}

CurveSampleResult approximateNurbs(const QVector<QPointF>& controlPoints, const QVector<double>& weights,
                                   int degree, const QVector<double>& knots, int samples, double tolerance)
{
    CurveSampleResult result;
    if (controlPoints.size() != weights.size() || controlPoints.size() <= degree || knots.size() != controlPoints.size() + degree + 1) return result;
    const int count = std::max(2, samples);
    const double low = knots.at(degree);
    const double high = knots.at(controlPoints.size());
    result.points.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(count - 1);
        const double u = low + (high - low) * t;
        result.points.append(evaluateNurbs(controlPoints, weights, degree, knots, u, tolerance));
    }
    result.length = polylineLength(result.points, false);
    result.bounds = boundingRect(result.points);
    result.valid = !result.points.isEmpty();
    return result;
}

QVector<QPointF> approximateCatmullRomSpline(const QVector<QPointF>& fitPoints, bool closed,
                                             int samplesPerSegment, double tension)
{
    QVector<QPointF> result;
    if (fitPoints.size() < 2) return fitPoints;
    const int samples = std::max(1, samplesPerSegment);
    const int segmentCount = closed ? fitPoints.size() : fitPoints.size() - 1;
    result.reserve(segmentCount * samples + 1);
    for (int i = 0; i < segmentCount; ++i) {
        const QPointF p0 = fitPoints.at((i - 1 + fitPoints.size()) % fitPoints.size());
        const QPointF p1 = fitPoints.at(i);
        const QPointF p2 = fitPoints.at((i + 1) % fitPoints.size());
        const QPointF p3 = fitPoints.at((i + 2) % fitPoints.size());
        const QPointF safeP0 = (!closed && i == 0) ? p1 : p0;
        const QPointF safeP3 = (!closed && i + 2 >= fitPoints.size()) ? p2 : p3;
        if (i == 0) result.append(p1);
        for (int s = 1; s <= samples; ++s) {
            if (closed && i == segmentCount - 1 && s == samples) continue;
            result.append(catmullRomPoint(safeP0, p1, p2, safeP3, static_cast<double>(s) / static_cast<double>(samples), tension));
        }
    }
    return result;
}

PolylineStation polylineStationAtPoint(const QVector<QPointF>& points, bool closed, const QPointF& point,
                                       double tolerance)
{
    PolylineStation station;
    if (points.size() < 2) return station;
    double cumulative = 0.0;
    double bestDistance = std::numeric_limits<double>::max();
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const QPointF a = points.at(i);
        const QPointF b = points.at((i + 1) % points.size());
        double t = 0.0;
        const QPointF nearest = nearestPointOnSegment(point, a, b, &t);
        const double d = distance(point, nearest);
        if (d < bestDistance) {
            bestDistance = d;
            station.valid = true;
            station.segmentIndex = i;
            station.parameter = t;
            station.distanceAlong = cumulative + distance(a, b) * t;
            station.point = nearest;
        }
        cumulative += distance(a, b);
    }
    if (bestDistance > tolerance && tolerance > 0.0) return station;
    return station;
}

PolylineStation polylineStationAtDistance(const QVector<QPointF>& points, bool closed, double distanceAlong,
                                          double tolerance)
{
    PolylineStation station;
    if (points.size() < 2) return station;
    const double total = polylineLength(points, closed);
    if (total <= tolerance) return station;
    double target = closed ? std::fmod(distanceAlong, total) : std::clamp(distanceAlong, 0.0, total);
    if (target < 0.0) target += total;
    double cumulative = 0.0;
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const QPointF a = points.at(i);
        const QPointF b = points.at((i + 1) % points.size());
        const double len = distance(a, b);
        if (len <= tolerance) continue;
        if (target <= cumulative + len || i == segmentCount - 1) {
            const double t = std::clamp((target - cumulative) / len, 0.0, 1.0);
            station.valid = true;
            station.segmentIndex = i;
            station.parameter = t;
            station.distanceAlong = cumulative + len * t;
            station.point = lerp(a, b, t);
            return station;
        }
        cumulative += len;
    }
    return station;
}

QVector<QPointF> resamplePolylineBySpacing(const QVector<QPointF>& points, bool closed, double spacing, double tolerance)
{
    QVector<QPointF> result;
    if (points.size() < 2 || spacing <= tolerance) return points;
    const double total = polylineLength(points, closed);
    if (total <= tolerance) return points;
    const int count = std::max(1, static_cast<int>(std::floor(total / spacing)));
    result.reserve(count + 1);
    for (int i = 0; i <= count; ++i) {
        const double d = std::min(total, spacing * i);
        PolylineStation station = polylineStationAtDistance(points, closed, d, tolerance);
        if (station.valid) result.append(station.point);
    }
    if (!closed && (result.isEmpty() || distance(result.last(), points.last()) > tolerance)) result.append(points.last());
    return result;
}

QVector<QPointF> removeDuplicateAndCollinearVertices(const QVector<QPointF>& points, bool closed,
                                                     double distanceTolerance, double collinearTolerance)
{
    if (points.size() < 3) return points;
    QVector<QPointF> filtered;
    filtered.reserve(points.size());
    for (const QPointF& point : points) {
        if (filtered.isEmpty() || distance(filtered.last(), point) > distanceTolerance) filtered.append(point);
    }
    if (closed && filtered.size() > 1 && distance(filtered.first(), filtered.last()) <= distanceTolerance) filtered.removeLast();
    if (filtered.size() < 3) return filtered;

    bool changed = true;
    while (changed && filtered.size() >= 3) {
        changed = false;
        QVector<QPointF> next;
        next.reserve(filtered.size());
        const int n = filtered.size();
        for (int i = 0; i < n; ++i) {
            if (!closed && (i == 0 || i == n - 1)) {
                next.append(filtered.at(i));
                continue;
            }
            const QPointF prev = filtered.at((i - 1 + n) % n);
            const QPointF curr = filtered.at(i);
            const QPointF following = filtered.at((i + 1) % n);
            if (distance(prev, curr) <= distanceTolerance || distance(curr, following) <= distanceTolerance) {
                changed = true;
                continue;
            }
            if (areCollinear(prev, curr, following, collinearTolerance)) {
                changed = true;
                continue;
            }
            next.append(curr);
        }
        filtered = next;
    }
    return filtered;
}

QVector<QPointF> weldPolylineGaps(const QVector<QPointF>& points, double gapTolerance)
{
    if (points.size() < 2) return points;
    QVector<QPointF> result = points;
    for (int i = 0; i + 1 < result.size(); ++i) {
        if (distance(result.at(i), result.at(i + 1)) <= gapTolerance) result[i + 1] = result.at(i);
    }
    return result;
}

QVector<QPointF> closePolylineIfNear(const QVector<QPointF>& points, double gapTolerance)
{
    QVector<QPointF> result = points;
    if (result.size() > 2 && distance(result.first(), result.last()) <= gapTolerance) result.last() = result.first();
    return result;
}

QVector<QPointF> reversePolyline(const QVector<QPointF>& points)
{
    QVector<QPointF> result = points;
    std::reverse(result.begin(), result.end());
    return result;
}

IntersectionResult findPolylineSelfIntersections(const QVector<QPointF>& points, bool closed, double tolerance)
{
    IntersectionResult result;
    if (points.size() < 4) return result;
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const QPointF a0 = points.at(i);
        const QPointF a1 = points.at((i + 1) % points.size());
        for (int j = i + 1; j < segmentCount; ++j) {
            const bool adjacent = std::abs(i - j) == 1 || (closed && i == 0 && j == segmentCount - 1);
            if (adjacent) continue;
            const QPointF b0 = points.at(j);
            const QPointF b1 = points.at((j + 1) % points.size());
            const IntersectionResult hit = intersectLineSegments(a0, a1, b0, b1, tolerance);
            for (const QPointF& point : hit.points) appendUnique(result.points, point, tolerance);
            if (hit.kind == IntersectionKind::Overlap && result.kind == IntersectionKind::None) result.kind = IntersectionKind::Overlap;
        }
    }
    if (result.kind != IntersectionKind::Overlap) {
        if (result.points.size() == 1) result.kind = IntersectionKind::One;
        else if (result.points.size() >= 2) result.kind = IntersectionKind::Two;
    }
    return result;
}

TrimSegmentResult extendSegmentToBoundary(const QPointF& segmentA, const QPointF& segmentB,
                                          const QVector<QPointF>& boundaryPolyline, bool boundaryClosed,
                                          bool extendStartSide, double tolerance)
{
    TrimSegmentResult result;
    if (isDegenerateSegment(segmentA, segmentB, tolerance) || boundaryPolyline.size() < 2) return result;
    const QPointF origin = extendStartSide ? segmentA : segmentB;
    const QPointF direction = extendStartSide ? segmentA - segmentB : segmentB - segmentA;
    QVector<QPointF> hits;
    const int segmentCount = boundaryClosed ? boundaryPolyline.size() : boundaryPolyline.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const IntersectionResult hit = intersectInfiniteLines(origin, origin + direction,
                                                              boundaryPolyline.at(i), boundaryPolyline.at((i + 1) % boundaryPolyline.size()),
                                                              tolerance);
        for (const QPointF& point : hit.points) {
            if (pointOnSegment(point, boundaryPolyline.at(i), boundaryPolyline.at((i + 1) % boundaryPolyline.size()), tolerance)
                && pointOnRay(point, origin, direction, tolerance)) appendUnique(hits, point, tolerance);
        }
    }
    QPointF chosen;
    if (!chooseNearestPointOnRay(origin, direction, hits, chosen, nullptr, tolerance)) return result;
    result.valid = true;
    result.start = extendStartSide ? chosen : segmentA;
    result.end = extendStartSide ? segmentB : chosen;
    return result;
}

ChamferResult lineLineChamfer(const QPointF& lineA0, const QPointF& lineA1,
                              const QPointF& lineB0, const QPointF& lineB1,
                              double distanceA, double distanceB, double tolerance)
{
    ChamferResult result;
    if (distanceA < 0.0 || distanceB < 0.0) return result;
    const IntersectionResult hit = intersectInfiniteLines(lineA0, lineA1, lineB0, lineB1, tolerance);
    if (hit.points.isEmpty()) return result;
    const QPointF intersection = hit.points.first();
    const QPointF dirA = normalized((distance(intersection, lineA0) > distance(intersection, lineA1) ? lineA0 : lineA1) - intersection, tolerance);
    const QPointF dirB = normalized((distance(intersection, lineB0) > distance(intersection, lineB1) ? lineB0 : lineB1) - intersection, tolerance);
    if (lengthSquared(dirA) <= tolerance * tolerance || lengthSquared(dirB) <= tolerance * tolerance) return result;
    result.valid = true;
    result.trimA = intersection + dirA * distanceA;
    result.trimB = intersection + dirB * distanceB;
    result.chamferStart = result.trimA;
    result.chamferEnd = result.trimB;
    return result;
}

QVector<QPointF> breakSegmentAtPoint(const QPointF& segmentA, const QPointF& segmentB,
                                     const QPointF& breakPoint, double gap, double tolerance)
{
    QVector<QPointF> result;
    if (!pointOnSegment(breakPoint, segmentA, segmentB, tolerance)) return result;
    const QPointF dir = normalized(segmentB - segmentA, tolerance);
    if (lengthSquared(dir) <= tolerance * tolerance) return result;
    const double halfGap = std::max(0.0, gap) * 0.5;
    const QPointF left = breakPoint - dir * halfGap;
    const QPointF right = breakPoint + dir * halfGap;
    result.append(segmentA);
    result.append(left);
    result.append(right);
    result.append(segmentB);
    return result;
}

QVector<QPointF> convexHull(QVector<QPointF> points, double tolerance)
{
    if (points.size() < 3) return points;
    std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) {
        if (a.x() == b.x()) return a.y() < b.y();
        return a.x() < b.x();
    });
    QVector<QPointF> unique;
    for (const QPointF& point : points) appendUnique(unique, point, tolerance);
    if (unique.size() < 3) return unique;

    QVector<QPointF> lower;
    for (const QPointF& point : unique) {
        while (lower.size() >= 2 && cross(lower.last() - lower.at(lower.size() - 2), point - lower.last()) <= tolerance) lower.removeLast();
        lower.append(point);
    }
    QVector<QPointF> upper;
    for (int i = unique.size() - 1; i >= 0; --i) {
        const QPointF point = unique.at(i);
        while (upper.size() >= 2 && cross(upper.last() - upper.at(upper.size() - 2), point - upper.last()) <= tolerance) upper.removeLast();
        upper.append(point);
    }
    lower.removeLast();
    upper.removeLast();
    lower += upper;
    return lower;
}

RegionResult intersectConvexPolygons(const QVector<QPointF>& subject, const QVector<QPointF>& convexClip, double tolerance)
{
    RegionResult result;
    if (subject.size() < 3 || convexClip.size() < 3) return result;
    result.boundary = sutherlandHodgmanClip(subject, convexClip, tolerance);
    result.area = std::abs(polygonSignedArea(result.boundary));
    result.valid = result.boundary.size() >= 3 && result.area > tolerance;
    return result;
}

QVector<QPointF> clipPolylineToConvexPolygon(const QVector<QPointF>& polyline, const QVector<QPointF>& convexClip,
                                             double tolerance)
{
    QVector<QPointF> result;
    if (polyline.size() < 2 || convexClip.size() < 3) return result;
    for (int i = 0; i + 1 < polyline.size(); ++i) {
        const QVector<QPointF> clipped = segmentPortionInsideConvex(polyline.at(i), polyline.at(i + 1), convexClip, tolerance);
        for (const QPointF& point : clipped) appendUnique(result, point, tolerance);
    }
    return result;
}

QVector<HatchSegment> hatchPolygonWithParallelLines(const QVector<QPointF>& polygon, double angleDeg,
                                                    double spacing, double phase, double tolerance)
{
    QVector<HatchSegment> segments;
    if (polygon.size() < 3 || spacing <= tolerance) return segments;
    const double a = qDegreesToRadians(angleDeg);
    const QPointF dir(std::cos(a), std::sin(a));
    const QPointF normal(-dir.y(), dir.x());

    double minProjection = std::numeric_limits<double>::max();
    double maxProjection = -std::numeric_limits<double>::max();
    for (const QPointF& point : polygon) {
        const double projection = dot(point, normal);
        minProjection = std::min(minProjection, projection);
        maxProjection = std::max(maxProjection, projection);
    }

    const double start = std::floor((minProjection - phase) / spacing) * spacing + phase;
    for (double offset = start; offset <= maxProjection + tolerance; offset += spacing) {
        const QPointF linePoint = normal * offset;
        QVector<QPointF> hits;
        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF a0 = polygon.at(i);
            const QPointF a1 = polygon.at((i + 1) % polygon.size());
            const IntersectionResult hit = intersectInfiniteLines(linePoint, linePoint + dir, a0, a1, tolerance);
            for (const QPointF& point : hit.points) {
                if (pointOnSegment(point, a0, a1, tolerance)) appendUnique(hits, point, tolerance);
            }
        }
        std::sort(hits.begin(), hits.end(), [&](const QPointF& lhs, const QPointF& rhs) {
            return dot(lhs, dir) < dot(rhs, dir);
        });
        for (int i = 0; i + 1 < hits.size(); i += 2) {
            if (distance(hits.at(i), hits.at(i + 1)) > tolerance) segments.append({hits.at(i), hits.at(i + 1)});
        }
    }
    return segments;
}

QVector<SnapCandidate> endpointSnaps(const QVector<QPointF>& points, const QPointF& cursor)
{
    QVector<SnapCandidate> snaps;
    for (int i = 0; i < points.size(); ++i) snaps.append(makeSnap(SnapKind::Endpoint, points.at(i), cursor, i));
    return snaps;
}

QVector<SnapCandidate> midpointSnaps(const QVector<QPointF>& points, bool closed, const QPointF& cursor)
{
    QVector<SnapCandidate> snaps;
    if (points.size() < 2) return snaps;
    const int segmentCount = closed ? points.size() : points.size() - 1;
    for (int i = 0; i < segmentCount; ++i) {
        snaps.append(makeSnap(SnapKind::Midpoint, (points.at(i) + points.at((i + 1) % points.size())) * 0.5, cursor, i));
    }
    return snaps;
}

QVector<SnapCandidate> circleSnaps(const QPointF& center, double radius, const QPointF& cursor,
                                   bool includeQuadrants, double tolerance)
{
    QVector<SnapCandidate> snaps;
    if (radius <= tolerance) return snaps;
    snaps.append(makeSnap(SnapKind::Center, center, cursor));
    if (includeQuadrants) {
        for (double angle : {0.0, 90.0, 180.0, 270.0}) snaps.append(makeSnap(SnapKind::Quadrant, pointOnCadCircle(center, radius, angle), cursor));
    }
    snaps.append(makeSnap(SnapKind::Nearest, closestPointOnCircle(cursor, center, radius, tolerance), cursor));
    return snaps;
}

SnapCandidate perpendicularSnap(const QPointF& cursor, const QPointF& lineA, const QPointF& lineB,
                                int sourceIndex, double tolerance)
{
    if (isDegenerateSegment(lineA, lineB, tolerance)) return SnapCandidate{};
    return makeSnap(SnapKind::Perpendicular, projectPointOnInfiniteLine(cursor, lineA, lineB), cursor, sourceIndex);
}

QVector<SnapCandidate> tangentSnapsFromCursorToCircle(const QPointF& cursor, const QPointF& center, double radius,
                                                      int sourceIndex, double tolerance)
{
    QVector<SnapCandidate> snaps;
    const CircleTangentResult tangents = tangentPointsFromPointToCircle(cursor, center, radius, tolerance);
    for (const QPointF& point : tangents.points) snaps.append(makeSnap(SnapKind::Tangent, point, cursor, sourceIndex));
    return snaps;
}

SnapCandidate nearestSnapOnPolyline(const QPointF& cursor, const QVector<QPointF>& points, bool closed,
                                    int sourceIndex, double tolerance)
{
    const ClosestPointResult closest = closestPointOnPolyline(cursor, points, closed, tolerance);
    if (!closest.valid) return SnapCandidate{};
    return makeSnap(SnapKind::Nearest, closest.point, cursor, sourceIndex);
}

SnapCandidate chooseBestSnap(const QVector<SnapCandidate>& candidates, double maxDistance, bool* found)
{
    SnapCandidate best;
    bool hasBest = false;
    for (const SnapCandidate& candidate : candidates) {
        if (!candidate.valid || candidate.distance > maxDistance) continue;
        if (!hasBest || candidate.distance < best.distance) {
            hasBest = true;
            best = candidate;
        }
    }
    if (found) *found = hasBest;
    return best;
}

DimensionLineGeometry makeAlignedDimensionGeometry(const QPointF& a, const QPointF& b, double offset,
                                                   double extensionOvershoot, double tolerance)
{
    DimensionLineGeometry result;
    if (isDegenerateSegment(a, b, tolerance)) return result;
    const QPointF dir = normalized(b - a, tolerance);
    const QPointF n = perpendicularLeft(dir, tolerance);
    result.valid = true;
    result.measurement = distance(a, b);
    result.dimensionA = a + n * offset;
    result.dimensionB = b + n * offset;
    result.extensionA0 = a;
    result.extensionA1 = result.dimensionA + n * extensionOvershoot;
    result.extensionB0 = b;
    result.extensionB1 = result.dimensionB + n * extensionOvershoot;
    result.textPosition = (result.dimensionA + result.dimensionB) * 0.5 + n * extensionOvershoot;
    return result;
}

DimensionLineGeometry makeLinearDimensionGeometry(const QPointF& a, const QPointF& b, double offset,
                                                  bool horizontal, double extensionOvershoot, double tolerance)
{
    QPointF projectedB = horizontal ? QPointF(b.x(), a.y()) : QPointF(a.x(), b.y());
    DimensionLineGeometry result = makeAlignedDimensionGeometry(a, projectedB, offset, extensionOvershoot, tolerance);
    if (!result.valid) return result;
    result.measurement = horizontal ? std::abs(b.x() - a.x()) : std::abs(b.y() - a.y());
    result.extensionB0 = b;
    return result;
}

AngularDimensionGeometry makeAngularDimensionGeometry(const QPointF& center, const QPointF& armA,
                                                      const QPointF& armB, double radius,
                                                      bool clockwise, double tolerance)
{
    AngularDimensionGeometry result;
    if (radius <= tolerance || distance(center, armA) <= tolerance || distance(center, armB) <= tolerance) return result;
    const double a0 = cadAngleDeg(center, armA);
    const double a1 = cadAngleDeg(center, armB);
    const double span = clockwise ? -ccwSpanDeg(a1, a0) : ccwSpanDeg(a0, a1);
    result.valid = true;
    result.center = center;
    result.startAngleDeg = a0;
    result.spanAngleDeg = span;
    result.measurementDeg = std::abs(span);
    result.arcStart = pointOnCadCircle(center, radius, a0);
    result.arcEnd = pointOnCadCircle(center, radius, a0 + span);
    result.textPosition = pointOnCadCircle(center, radius + 10.0, a0 + span * 0.5);
    return result;
}

DimensionLineGeometry makeRadiusDimensionGeometry(const QPointF& center, const QPointF& pointOnCircle,
                                                  double textOffset, double tolerance)
{
    DimensionLineGeometry result;
    const double radius = distance(center, pointOnCircle);
    if (radius <= tolerance) return result;
    const QPointF dir = normalized(pointOnCircle - center, tolerance);
    result.valid = true;
    result.measurement = radius;
    result.dimensionA = center;
    result.dimensionB = pointOnCircle;
    result.extensionA0 = center;
    result.extensionA1 = pointOnCircle;
    result.extensionB0 = pointOnCircle;
    result.extensionB1 = pointOnCircle + dir * textOffset;
    result.textPosition = result.extensionB1;
    return result;
}

} // namespace CadGeometry
