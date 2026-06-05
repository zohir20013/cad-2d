#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace CadGeometry {

constexpr double kDefaultTolerance = 1.0e-9;
constexpr double kAngularToleranceDeg = 1.0e-7;

enum class IntersectionKind {
    None,
    Tangent,
    One,
    Two,
    Overlap
};

struct IntersectionResult {
    IntersectionKind kind = IntersectionKind::None;
    QVector<QPointF> points;

    bool hasPoint() const { return !points.isEmpty(); }
    QPointF firstPoint() const { return points.isEmpty() ? QPointF() : points.first(); }
};

struct FilletResult {
    bool valid = false;
    QPointF tangentA;
    QPointF tangentB;
    QPointF center;
    double radius = 0.0;
    double startAngleDeg = 0.0;
    double spanAngleDeg = 0.0;
};

bool isFinite(double value);
bool isFinite(const QPointF& point);
bool fuzzyEquals(double a, double b, double tolerance = kDefaultTolerance);
bool fuzzyEquals(const QPointF& a, const QPointF& b, double tolerance = kDefaultTolerance);

double dot(const QPointF& a, const QPointF& b);
double cross(const QPointF& a, const QPointF& b);
double lengthSquared(const QPointF& v);
double length(const QPointF& v);
double distance(const QPointF& a, const QPointF& b);
bool isDegenerateSegment(const QPointF& a, const QPointF& b, double tolerance = kDefaultTolerance);

QPointF normalized(const QPointF& v, double tolerance = kDefaultTolerance);
QPointF perpendicularLeft(const QPointF& v, double tolerance = kDefaultTolerance);
QPointF pointOnLine(const QPointF& a, const QPointF& direction, double parameter);
QPointF rotatePoint(const QPointF& point, const QPointF& center, double angleDeg);
QPointF mirrorPoint(const QPointF& point, const QPointF& axisA, const QPointF& axisB);
QPointF projectPointOnInfiniteLine(const QPointF& point, const QPointF& lineA, const QPointF& lineB, double* parameter = nullptr);
QPointF nearestPointOnSegment(const QPointF& point, const QPointF& segmentA, const QPointF& segmentB, double* parameter = nullptr);
double distancePointToSegment(const QPointF& point, const QPointF& segmentA, const QPointF& segmentB);

double normalizeAngleDeg(double angleDeg);
double cadAngleDeg(const QPointF& center, const QPointF& point);
double ccwSpanDeg(double startDeg, double endDeg);
double signedShortestSpanDeg(double startDeg, double endDeg);
bool pointOnCadArc(const QPointF& center, const QPointF& point, double startAngleDeg, double spanAngleDeg,
                   double toleranceDeg = kAngularToleranceDeg);
QPointF pointOnCadCircle(const QPointF& center, double radius, double angleDeg);

bool pointOnSegment(const QPointF& point, const QPointF& segmentA, const QPointF& segmentB,
                    double tolerance = kDefaultTolerance);

IntersectionResult intersectInfiniteLines(const QPointF& a, const QPointF& b,
                                          const QPointF& c, const QPointF& d,
                                          double tolerance = kDefaultTolerance);
IntersectionResult intersectLineSegments(const QPointF& a, const QPointF& b,
                                         const QPointF& c, const QPointF& d,
                                         double tolerance = kDefaultTolerance);
IntersectionResult intersectInfiniteLineCircle(const QPointF& lineA, const QPointF& lineB,
                                               const QPointF& center, double radius,
                                               double tolerance = kDefaultTolerance);
IntersectionResult intersectSegmentCircle(const QPointF& segmentA, const QPointF& segmentB,
                                          const QPointF& center, double radius,
                                          double tolerance = kDefaultTolerance);
IntersectionResult intersectCircleCircle(const QPointF& centerA, double radiusA,
                                         const QPointF& centerB, double radiusB,
                                         double tolerance = kDefaultTolerance);
IntersectionResult intersectInfiniteLineEllipse(const QPointF& lineA, const QPointF& lineB,
                                                const QRectF& ellipseRect,
                                                double tolerance = kDefaultTolerance);

QVector<QPointF> rectangleCorners(const QRectF& rect);
QVector<QPointF> regularPolygonPoints(const QPointF& center, double radius, int sides, double rotationDeg);
QRectF boundingRect(const QVector<QPointF>& points);
double polygonSignedArea(const QVector<QPointF>& polygon);
bool isClockwise(const QVector<QPointF>& polygon);
bool pointInPolygon(const QPointF& point, const QVector<QPointF>& polygon);

QVector<QPointF> offsetSegment(const QPointF& a, const QPointF& b, double distance,
                               double tolerance = kDefaultTolerance);
QVector<QPointF> offsetPolyline(const QVector<QPointF>& points, bool closed, double distance,
                                double tolerance = kDefaultTolerance);

QPointF pointFromPivotAlongSegment(const QPointF& segmentA, const QPointF& segmentB,
                                   const QPointF& pivot, double distanceFromPivot,
                                   double tolerance = kDefaultTolerance);
FilletResult lineLineFillet(const QPointF& lineA0, const QPointF& lineA1,
                            const QPointF& lineB0, const QPointF& lineB1,
                            const QPointF& intersection, double radius,
                            double tolerance = kDefaultTolerance);

struct ClosestPointResult {
    bool valid = false;
    QPointF point;
    int segmentIndex = -1;
    double parameter = 0.0;
    double distance = 0.0;
};

struct TrimSegmentResult {
    bool valid = false;
    QPointF start;
    QPointF end;
};

struct CircleTangentResult {
    bool valid = false;
    QVector<QPointF> points;
};

double signedDistancePointToLine(const QPointF& point, const QPointF& lineA, const QPointF& lineB,
                                 double tolerance = kDefaultTolerance);
int orientation(const QPointF& a, const QPointF& b, const QPointF& c,
                double tolerance = kDefaultTolerance);
bool areCollinear(const QPointF& a, const QPointF& b, const QPointF& c,
                  double tolerance = kDefaultTolerance);
bool pointOnRay(const QPointF& point, const QPointF& rayOrigin, const QPointF& rayDirection,
                double tolerance = kDefaultTolerance);
double rayParameter(const QPointF& point, const QPointF& rayOrigin, const QPointF& rayDirection,
                    double tolerance = kDefaultTolerance);

QVector<QPointF> arcEndpoints(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg);
QRectF circleBoundingRect(const QPointF& center, double radius);
QRectF arcBoundingRect(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg,
                       double toleranceDeg = kAngularToleranceDeg);
QRectF ellipseBoundingRect(const QRectF& ellipseRect);
QPointF pointOnEllipse(const QRectF& ellipseRect, double angleDeg);
QVector<QPointF> approximateArc(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg,
                                double maxAngleStepDeg = 5.0);
QVector<QPointF> approximateEllipse(const QRectF& ellipseRect, int minimumSegments = 96);
QVector<QPointF> approximateEllipseArc(const QRectF& ellipseRect, double startAngleDeg, double spanAngleDeg,
                                       double maxAngleStepDeg = 5.0);

bool pointOnCircle(const QPointF& point, const QPointF& center, double radius,
                   double tolerance = kDefaultTolerance);
bool pointOnEllipse(const QPointF& point, const QRectF& ellipseRect,
                    double tolerance = kDefaultTolerance);
bool pointOnPolyline(const QPointF& point, const QVector<QPointF>& points, bool closed,
                     double tolerance = kDefaultTolerance);

double polylineLength(const QVector<QPointF>& points, bool closed = false);
QPointF polygonCentroid(const QVector<QPointF>& polygon, double tolerance = kDefaultTolerance);
QVector<QPointF> simplifyPolyline(const QVector<QPointF>& points, bool closed,
                                  double distanceTolerance = kDefaultTolerance,
                                  double collinearTolerance = kDefaultTolerance);
ClosestPointResult closestPointOnPolyline(const QPointF& point, const QVector<QPointF>& points, bool closed,
                                          double tolerance = kDefaultTolerance);

IntersectionResult intersectSegmentEllipse(const QPointF& segmentA, const QPointF& segmentB,
                                           const QRectF& ellipseRect,
                                           double tolerance = kDefaultTolerance);
IntersectionResult intersectPolylineSegment(const QVector<QPointF>& points, bool closed,
                                            const QPointF& segmentA, const QPointF& segmentB,
                                            double tolerance = kDefaultTolerance);
IntersectionResult intersectPolylines(const QVector<QPointF>& first, bool firstClosed,
                                      const QVector<QPointF>& second, bool secondClosed,
                                      double tolerance = kDefaultTolerance);
IntersectionResult intersectCirclePolyline(const QPointF& center, double radius,
                                           const QVector<QPointF>& points, bool closed,
                                           double tolerance = kDefaultTolerance);
IntersectionResult intersectEllipsePolyline(const QRectF& ellipseRect,
                                            const QVector<QPointF>& points, bool closed,
                                            double tolerance = kDefaultTolerance);
IntersectionResult intersectCadArcSegment(const QPointF& center, double radius,
                                          double startAngleDeg, double spanAngleDeg,
                                          const QPointF& segmentA, const QPointF& segmentB,
                                          double tolerance = kDefaultTolerance);
IntersectionResult intersectCadArcCircle(const QPointF& center, double radius,
                                         double startAngleDeg, double spanAngleDeg,
                                         const QPointF& circleCenter, double circleRadius,
                                         double tolerance = kDefaultTolerance);
IntersectionResult intersectCadArcs(const QPointF& centerA, double radiusA,
                                    double startAngleDegA, double spanAngleDegA,
                                    const QPointF& centerB, double radiusB,
                                    double startAngleDegB, double spanAngleDegB,
                                    double tolerance = kDefaultTolerance);
IntersectionResult intersectCadArcPolyline(const QPointF& center, double radius,
                                           double startAngleDeg, double spanAngleDeg,
                                           const QVector<QPointF>& points, bool closed,
                                           double tolerance = kDefaultTolerance);
IntersectionResult intersectCadArcEllipse(const QPointF& center, double radius,
                                          double startAngleDeg, double spanAngleDeg,
                                          const QRectF& ellipseRect,
                                          double tolerance = kDefaultTolerance);

QPointF closestPointOnCircle(const QPointF& point, const QPointF& center, double radius,
                             double tolerance = kDefaultTolerance);
QPointF closestPointOnCadArc(const QPointF& point, const QPointF& center, double radius,
                             double startAngleDeg, double spanAngleDeg,
                             double tolerance = kDefaultTolerance);
QPointF closestPointOnEllipse(const QPointF& point, const QRectF& ellipseRect,
                              int samples = 144, double tolerance = kDefaultTolerance);

TrimSegmentResult trimSegmentAtPoint(const QPointF& segmentA, const QPointF& segmentB,
                                     const QPointF& trimPoint, bool trimStartSide,
                                     double tolerance = kDefaultTolerance);
TrimSegmentResult extendSegmentToPoint(const QPointF& segmentA, const QPointF& segmentB,
                                       const QPointF& targetPoint, bool extendStartSide,
                                       double tolerance = kDefaultTolerance);
bool chooseNearestPointOnRay(const QPointF& rayOrigin, const QPointF& rayDirection,
                             const QVector<QPointF>& candidates, QPointF& chosen,
                             double* chosenDistance = nullptr,
                             double tolerance = kDefaultTolerance);
CircleTangentResult tangentPointsFromPointToCircle(const QPointF& externalPoint,
                                                   const QPointF& center, double radius,
                                                   double tolerance = kDefaultTolerance);

} // namespace CadGeometry
