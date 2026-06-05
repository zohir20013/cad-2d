#pragma once

#include "GeometryKernel.h"

namespace CadGeometry {

// Additional CAD-grade geometry primitives and helpers. These functions are UI-independent
// and are intended to support AutoCAD-like 2D editing commands.

enum class SnapKind {
    Endpoint,
    Midpoint,
    Center,
    Quadrant,
    Intersection,
    Perpendicular,
    Tangent,
    Nearest,
    Extension,
    ApparentIntersection
};

struct SnapCandidate {
    bool valid = false;
    SnapKind kind = SnapKind::Nearest;
    QPointF point;
    double distance = 0.0;
    int sourceIndex = -1;
};

struct CircleSolution {
    bool valid = false;
    QPointF center;
    double radius = 0.0;
};

struct ArcSolution {
    bool valid = false;
    QPointF center;
    double radius = 0.0;
    double startAngleDeg = 0.0;
    double spanAngleDeg = 0.0;
};

struct BulgeArc {
    bool valid = false;
    QPointF center;
    double radius = 0.0;
    double startAngleDeg = 0.0;
    double spanAngleDeg = 0.0;
};

struct ChamferResult {
    bool valid = false;
    QPointF trimA;
    QPointF trimB;
    QPointF chamferStart;
    QPointF chamferEnd;
};

struct DimensionLineGeometry {
    bool valid = false;
    QPointF extensionA0;
    QPointF extensionA1;
    QPointF extensionB0;
    QPointF extensionB1;
    QPointF dimensionA;
    QPointF dimensionB;
    QPointF textPosition;
    double measurement = 0.0;
};

struct AngularDimensionGeometry {
    bool valid = false;
    QPointF center;
    QPointF arcStart;
    QPointF arcEnd;
    QPointF textPosition;
    double startAngleDeg = 0.0;
    double spanAngleDeg = 0.0;
    double measurementDeg = 0.0;
};

struct HatchSegment {
    QPointF start;
    QPointF end;
};

struct PolylineStation {
    bool valid = false;
    int segmentIndex = -1;
    double parameter = 0.0;
    double distanceAlong = 0.0;
    QPointF point;
};

struct RegionResult {
    bool valid = false;
    QVector<QPointF> boundary;
    double area = 0.0;
};

struct Transform2D {
    double m11 = 1.0;
    double m12 = 0.0;
    double m21 = 0.0;
    double m22 = 1.0;
    double dx = 0.0;
    double dy = 0.0;
};

struct CurveSampleResult {
    bool valid = false;
    QVector<QPointF> points;
    double length = 0.0;
    QRectF bounds;
};

// Affine transforms and array helpers.
Transform2D identityTransform();
Transform2D translationTransform(double dx, double dy);
Transform2D rotationTransform(const QPointF& center, double angleDeg);
Transform2D scaleTransform(const QPointF& center, double sx, double sy);
Transform2D mirrorTransform(const QPointF& axisA, const QPointF& axisB,
                            double tolerance = kDefaultTolerance);
Transform2D multiplyTransforms(const Transform2D& first, const Transform2D& second);
QPointF transformPoint(const QPointF& point, const Transform2D& transform);
QVector<QPointF> transformPoints(const QVector<QPointF>& points, const Transform2D& transform);
QVector<Transform2D> rectangularArrayTransforms(int rows, int columns, double rowSpacing, double columnSpacing);
QVector<Transform2D> polarArrayTransforms(const QPointF& center, int count, double totalAngleDeg,
                                          bool rotateItems = true);

// Construction helpers comparable to classic CAD commands.
CircleSolution circleFromTwoPoints(const QPointF& a, const QPointF& b,
                                   double tolerance = kDefaultTolerance);
CircleSolution circleFromThreePoints(const QPointF& a, const QPointF& b, const QPointF& c,
                                     double tolerance = kDefaultTolerance);
CircleSolution circleFromCenterRadius(const QPointF& center, double radius,
                                      double tolerance = kDefaultTolerance);
CircleSolution circleFromCenterDiameterPoint(const QPointF& center, const QPointF& diameterPoint,
                                             double tolerance = kDefaultTolerance);
ArcSolution arcFromThreePoints(const QPointF& start, const QPointF& pointOnArc, const QPointF& end,
                               double tolerance = kDefaultTolerance);
ArcSolution arcFromCenterStartEnd(const QPointF& center, const QPointF& start, const QPointF& end,
                                  bool clockwise = false, double tolerance = kDefaultTolerance);
QPointF linePointAtDistance(const QPointF& start, const QPointF& end, double distanceFromStart,
                            double tolerance = kDefaultTolerance);
QVector<QPointF> parallelLineThroughPoint(const QPointF& lineA, const QPointF& lineB, const QPointF& throughPoint,
                                          double tolerance = kDefaultTolerance);
QVector<QPointF> perpendicularLineThroughPoint(const QPointF& lineA, const QPointF& lineB, const QPointF& throughPoint,
                                               double lengthHint = 100.0, double tolerance = kDefaultTolerance);
QPointF angleBisectorDirection(const QPointF& vertex, const QPointF& pointA, const QPointF& pointB,
                               bool external = false, double tolerance = kDefaultTolerance);

// DXF/LWPOLYLINE bulge support.
BulgeArc bulgeToArc(const QPointF& start, const QPointF& end, double bulge,
                    double tolerance = kDefaultTolerance);
double bulgeFromIncludedAngle(double includedAngleDeg);
QVector<QPointF> approximateBulgedPolyline(const QVector<QPointF>& points, const QVector<double>& bulges,
                                           bool closed, double maxAngleStepDeg = 5.0,
                                           double tolerance = kDefaultTolerance);

// Bezier, B-spline, NURBS and spline approximation helpers.
QPointF quadraticBezierPoint(const QPointF& p0, const QPointF& p1, const QPointF& p2, double t);
QPointF cubicBezierPoint(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, double t);
QPointF cubicBezierDerivative(const QPointF& p0, const QPointF& p1, const QPointF& p2, const QPointF& p3, double t);
QVector<QPointF> approximateCubicBezier(const QPointF& p0, const QPointF& p1,
                                        const QPointF& p2, const QPointF& p3,
                                        int segments = 32);
QVector<double> createClampedUniformKnotVector(int controlPointCount, int degree);
QPointF evaluateBSpline(const QVector<QPointF>& controlPoints, int degree,
                        const QVector<double>& knots, double u,
                        double tolerance = kDefaultTolerance);
QPointF evaluateNurbs(const QVector<QPointF>& controlPoints, const QVector<double>& weights, int degree,
                      const QVector<double>& knots, double u,
                      double tolerance = kDefaultTolerance);
CurveSampleResult approximateNurbs(const QVector<QPointF>& controlPoints, const QVector<double>& weights,
                                   int degree, const QVector<double>& knots, int samples = 96,
                                   double tolerance = kDefaultTolerance);
QVector<QPointF> approximateCatmullRomSpline(const QVector<QPointF>& fitPoints, bool closed,
                                             int samplesPerSegment = 16,
                                             double tension = 0.5);

// Polyline parameterization, editing and healing.
PolylineStation polylineStationAtPoint(const QVector<QPointF>& points, bool closed, const QPointF& point,
                                       double tolerance = kDefaultTolerance);
PolylineStation polylineStationAtDistance(const QVector<QPointF>& points, bool closed, double distanceAlong,
                                          double tolerance = kDefaultTolerance);
QVector<QPointF> resamplePolylineBySpacing(const QVector<QPointF>& points, bool closed, double spacing,
                                           double tolerance = kDefaultTolerance);
QVector<QPointF> removeDuplicateAndCollinearVertices(const QVector<QPointF>& points, bool closed,
                                                     double distanceTolerance = 1.0e-7,
                                                     double collinearTolerance = 1.0e-9);
QVector<QPointF> weldPolylineGaps(const QVector<QPointF>& points, double gapTolerance = 1.0e-6);
QVector<QPointF> closePolylineIfNear(const QVector<QPointF>& points, double gapTolerance = 1.0e-6);
QVector<QPointF> reversePolyline(const QVector<QPointF>& points);
IntersectionResult findPolylineSelfIntersections(const QVector<QPointF>& points, bool closed,
                                                 double tolerance = kDefaultTolerance);

// Trim, extend, chamfer and fillet support.
TrimSegmentResult extendSegmentToBoundary(const QPointF& segmentA, const QPointF& segmentB,
                                          const QVector<QPointF>& boundaryPolyline, bool boundaryClosed,
                                          bool extendStartSide, double tolerance = kDefaultTolerance);
ChamferResult lineLineChamfer(const QPointF& lineA0, const QPointF& lineA1,
                              const QPointF& lineB0, const QPointF& lineB1,
                              double distanceA, double distanceB,
                              double tolerance = kDefaultTolerance);
QVector<QPointF> breakSegmentAtPoint(const QPointF& segmentA, const QPointF& segmentB,
                                     const QPointF& breakPoint, double gap = 0.0,
                                     double tolerance = kDefaultTolerance);

// Region, boundary and hatch helpers. Convex clipping is exact for convex clips; general
// polygon operations are approximation-ready foundations for CAD editing commands.
QVector<QPointF> convexHull(QVector<QPointF> points, double tolerance = kDefaultTolerance);
RegionResult intersectConvexPolygons(const QVector<QPointF>& subject, const QVector<QPointF>& convexClip,
                                     double tolerance = kDefaultTolerance);
QVector<QPointF> clipPolylineToConvexPolygon(const QVector<QPointF>& polyline, const QVector<QPointF>& convexClip,
                                             double tolerance = kDefaultTolerance);
QVector<HatchSegment> hatchPolygonWithParallelLines(const QVector<QPointF>& polygon, double angleDeg,
                                                    double spacing, double phase = 0.0,
                                                    double tolerance = kDefaultTolerance);

// Snapping candidates.
QVector<SnapCandidate> endpointSnaps(const QVector<QPointF>& points, const QPointF& cursor);
QVector<SnapCandidate> midpointSnaps(const QVector<QPointF>& points, bool closed, const QPointF& cursor);
QVector<SnapCandidate> circleSnaps(const QPointF& center, double radius, const QPointF& cursor,
                                   bool includeQuadrants = true, double tolerance = kDefaultTolerance);
SnapCandidate perpendicularSnap(const QPointF& cursor, const QPointF& lineA, const QPointF& lineB,
                                int sourceIndex = -1, double tolerance = kDefaultTolerance);
QVector<SnapCandidate> tangentSnapsFromCursorToCircle(const QPointF& cursor, const QPointF& center, double radius,
                                                      int sourceIndex = -1, double tolerance = kDefaultTolerance);
SnapCandidate nearestSnapOnPolyline(const QPointF& cursor, const QVector<QPointF>& points, bool closed,
                                    int sourceIndex = -1, double tolerance = kDefaultTolerance);
SnapCandidate chooseBestSnap(const QVector<SnapCandidate>& candidates, double maxDistance,
                             bool* found = nullptr);

// Dimension helpers.
DimensionLineGeometry makeAlignedDimensionGeometry(const QPointF& a, const QPointF& b, double offset,
                                                   double extensionOvershoot = 2.5,
                                                   double tolerance = kDefaultTolerance);
DimensionLineGeometry makeLinearDimensionGeometry(const QPointF& a, const QPointF& b, double offset,
                                                  bool horizontal, double extensionOvershoot = 2.5,
                                                  double tolerance = kDefaultTolerance);
AngularDimensionGeometry makeAngularDimensionGeometry(const QPointF& center, const QPointF& armA,
                                                      const QPointF& armB, double radius,
                                                      bool clockwise = false,
                                                      double tolerance = kDefaultTolerance);
DimensionLineGeometry makeRadiusDimensionGeometry(const QPointF& center, const QPointF& pointOnCircle,
                                                  double textOffset = 10.0,
                                                  double tolerance = kDefaultTolerance);

} // namespace CadGeometry
