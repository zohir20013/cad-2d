#pragma once

#include "geometry/GeometryKernelAdvanced.h"

#include <QPointF>
#include <QVector>

namespace CadGeometry {

// A lightweight 2D parametric constraint solver for CAD editing workflows.
// The solver is intentionally UI-independent. It works on points plus lightweight
// line/circle references, making it easy to connect to CadEntity objects later.

enum class ConstraintKind {
    FixedPoint,
    Coincident,
    Horizontal,
    Vertical,
    Collinear,
    Parallel,
    Perpendicular,
    EqualLength,
    Distance,
    Angle,
    PointOnLine,
    PointOnCircle,
    Concentric,
    EqualRadius,
    TangentLineCircle,
    Midpoint,
    Symmetric
};

enum class ConstraintIssueSeverity {
    Info,
    Warning,
    Error
};

struct ConstraintPoint {
    int id = -1;
    QPointF position;
    QPointF fixedPosition;
    bool locked = false;
    double weight = 1.0;
};

struct ConstraintLineRef {
    int start = -1;
    int end = -1;
};

struct ConstraintCircleRef {
    int center = -1;
    int radiusPoint = -1;
    double fallbackRadius = 0.0;
};

struct GeometricConstraint {
    ConstraintKind kind = ConstraintKind::Coincident;
    bool enabled = true;
    double strength = 1.0;
    double targetValue = 0.0;
    QPointF targetPoint;

    // Generic point slots. Their meaning depends on kind.
    // Examples: Coincident(p0,p1), Midpoint(p0 midpoint of p1-p2), Symmetric(p0,p1 around p2-p3).
    int p0 = -1;
    int p1 = -1;
    int p2 = -1;
    int p3 = -1;

    ConstraintLineRef lineA;
    ConstraintLineRef lineB;
    ConstraintCircleRef circleA;
    ConstraintCircleRef circleB;
};

struct ConstraintResidual {
    int constraintIndex = -1;
    ConstraintKind kind = ConstraintKind::Coincident;
    double error = 0.0;
};

struct ConstraintIssue {
    int constraintIndex = -1;
    ConstraintKind kind = ConstraintKind::Coincident;
    ConstraintIssueSeverity severity = ConstraintIssueSeverity::Info;
    double residual = 0.0;
};

struct ConstraintSolveOptions {
    int maxIterations = 120;
    double tolerance = 1.0e-6;
    double damping = 0.75;
    double maxStep = 1.0e4;
    bool stopWhenDiverging = true;
};

struct ConstraintSolveReport {
    bool converged = false;
    int iterations = 0;
    int enabledConstraintCount = 0;
    double initialMaxResidual = 0.0;
    double finalMaxResidual = 0.0;
    double lastIterationDelta = 0.0;
};

struct ConstraintGraphStats {
    int pointCount = 0;
    int lockedPointCount = 0;
    int lineCount = 0;
    int circleCount = 0;
    int enabledConstraintCount = 0;
    int estimatedScalarConstraints = 0;
    int estimatedDegreesOfFreedom = 0;
    bool probablyUnderConstrained = true;
    bool probablyOverConstrained = false;
};

class GeometricConstraintSystem {
public:
    int addPoint(const QPointF& position, bool locked = false);
    int addLine(int startPointId, int endPointId);
    int addCircle(int centerPointId, int radiusPointId, double fallbackRadius = 0.0);

    bool setPointPosition(int pointId, const QPointF& position);
    bool setPointLocked(int pointId, bool locked);
    bool setPointWeight(int pointId, double weight);
    bool setConstraintEnabled(int constraintIndex, bool enabled);
    bool clearConstraints();
    void clear();

    const QVector<ConstraintPoint>& points() const { return m_points; }
    const QVector<ConstraintLineRef>& lines() const { return m_lines; }
    const QVector<ConstraintCircleRef>& circles() const { return m_circles; }
    const QVector<GeometricConstraint>& constraints() const { return m_constraints; }

    QPointF pointPosition(int pointId) const;
    ConstraintLineRef line(int lineId) const;
    ConstraintCircleRef circle(int circleId) const;
    double circleRadius(const ConstraintCircleRef& circleRef) const;

    int addFixedPoint(int pointId, const QPointF& fixedPosition);
    int addCoincident(int pointA, int pointB);
    int addHorizontal(int lineId);
    int addVertical(int lineId);
    int addCollinear(int lineId, int pointId);
    int addParallel(int lineAId, int lineBId);
    int addPerpendicular(int lineAId, int lineBId);
    int addEqualLength(int lineAId, int lineBId);
    int addDistance(int pointA, int pointB, double distanceValue);
    int addAngle(int lineAId, int lineBId, double angleDeg);
    int addPointOnLine(int pointId, int lineId);
    int addPointOnCircle(int pointId, int circleId);
    int addConcentric(int circleAId, int circleBId);
    int addEqualRadius(int circleAId, int circleBId);
    int addTangentLineCircle(int lineId, int circleId);
    int addMidpoint(int midpointId, int endpointA, int endpointB);
    int addSymmetric(int pointA, int pointB, int axisLineId);

    QVector<ConstraintResidual> residuals() const;
    double maxResidual() const;
    ConstraintGraphStats graphStats() const;
    QVector<ConstraintIssue> validate(double warningTolerance = 1.0e-4,
                                      double errorTolerance = 1.0e-2) const;

    ConstraintSolveReport solve(const ConstraintSolveOptions& options = ConstraintSolveOptions());

private:
    QVector<ConstraintPoint> m_points;
    QVector<ConstraintLineRef> m_lines;
    QVector<ConstraintCircleRef> m_circles;
    QVector<GeometricConstraint> m_constraints;

    bool isValidPoint(int pointId) const;
    bool isValidLine(int lineId) const;
    bool isValidCircle(int circleId) const;
    bool isMovablePoint(int pointId) const;

    int addConstraint(const GeometricConstraint& constraint);
    double residual(const GeometricConstraint& constraint) const;
    double applyConstraint(const GeometricConstraint& constraint, const ConstraintSolveOptions& options);

    void movePoint(int pointId, const QPointF& delta, double strength, const ConstraintSolveOptions& options);
    void movePointToward(int pointId, const QPointF& target, double strength, const ConstraintSolveOptions& options);
    void movePairToward(int pointA, int pointB, const QPointF& targetA, const QPointF& targetB,
                        double strength, const ConstraintSolveOptions& options);

    QPointF lineStart(const ConstraintLineRef& lineRef) const;
    QPointF lineEnd(const ConstraintLineRef& lineRef) const;
    QPointF circleCenter(const ConstraintCircleRef& circleRef) const;
    QPointF circleRadiusPoint(const ConstraintCircleRef& circleRef) const;
};

int estimatedScalarConstraintCount(ConstraintKind kind);
const char* constraintKindName(ConstraintKind kind);

} // namespace CadGeometry
