#include "GeometricConstraintSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace CadGeometry {
namespace {

constexpr double kMinSolverLength = 1.0e-9;
constexpr double kPi = 3.141592653589793238462643383279502884;

bool validIndex(int index, int size)
{
    return index >= 0 && index < size;
}

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}


QPointF limitedVector(const QPointF& delta, double maxLength)
{
    const double len = length(delta);
    if (len <= maxLength || len <= kMinSolverLength) {
        return delta;
    }
    return delta * (maxLength / len);
}

QPointF midpoint(const QPointF& a, const QPointF& b)
{
    return (a + b) * 0.5;
}

QPointF safeDirection(const QPointF& a, const QPointF& b)
{
    const QPointF d = b - a;
    if (length(d) <= kMinSolverLength) {
        return QPointF(1.0, 0.0);
    }
    return normalized(d);
}

double signedAngleDegBetween(const QPointF& a, const QPointF& b)
{
    const double angle = std::atan2(cross(a, b), dot(a, b)) * 180.0 / kPi;
    return angle;
}

QPointF rotateVectorDeg(const QPointF& v, double angleDeg)
{
    const double a = angleDeg * kPi / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
}


} // namespace

int GeometricConstraintSystem::addPoint(const QPointF& position, bool locked)
{
    ConstraintPoint point;
    point.id = m_points.size();
    point.position = position;
    point.fixedPosition = position;
    point.locked = locked;
    m_points.append(point);
    return point.id;
}

int GeometricConstraintSystem::addLine(int startPointId, int endPointId)
{
    if (!isValidPoint(startPointId) || !isValidPoint(endPointId) || startPointId == endPointId) {
        return -1;
    }
    ConstraintLineRef lineRef;
    lineRef.start = startPointId;
    lineRef.end = endPointId;
    m_lines.append(lineRef);
    return m_lines.size() - 1;
}

int GeometricConstraintSystem::addCircle(int centerPointId, int radiusPointId, double fallbackRadius)
{
    if (!isValidPoint(centerPointId)) {
        return -1;
    }
    if (radiusPointId >= 0 && !isValidPoint(radiusPointId)) {
        return -1;
    }
    ConstraintCircleRef circleRef;
    circleRef.center = centerPointId;
    circleRef.radiusPoint = radiusPointId;
    circleRef.fallbackRadius = std::max(0.0, fallbackRadius);
    m_circles.append(circleRef);
    return m_circles.size() - 1;
}

bool GeometricConstraintSystem::setPointPosition(int pointId, const QPointF& position)
{
    if (!isValidPoint(pointId)) {
        return false;
    }
    m_points[pointId].position = position;
    if (m_points[pointId].locked) {
        m_points[pointId].fixedPosition = position;
    }
    return true;
}

bool GeometricConstraintSystem::setPointLocked(int pointId, bool locked)
{
    if (!isValidPoint(pointId)) {
        return false;
    }
    m_points[pointId].locked = locked;
    if (locked) {
        m_points[pointId].fixedPosition = m_points[pointId].position;
    }
    return true;
}

bool GeometricConstraintSystem::setPointWeight(int pointId, double weight)
{
    if (!isValidPoint(pointId) || weight <= 0.0 || !isFinite(weight)) {
        return false;
    }
    m_points[pointId].weight = weight;
    return true;
}

bool GeometricConstraintSystem::setConstraintEnabled(int constraintIndex, bool enabled)
{
    if (!validIndex(constraintIndex, m_constraints.size())) {
        return false;
    }
    m_constraints[constraintIndex].enabled = enabled;
    return true;
}

bool GeometricConstraintSystem::clearConstraints()
{
    m_constraints.clear();
    return true;
}

void GeometricConstraintSystem::clear()
{
    m_points.clear();
    m_lines.clear();
    m_circles.clear();
    m_constraints.clear();
}

QPointF GeometricConstraintSystem::pointPosition(int pointId) const
{
    return isValidPoint(pointId) ? m_points[pointId].position : QPointF();
}

ConstraintLineRef GeometricConstraintSystem::line(int lineId) const
{
    return isValidLine(lineId) ? m_lines[lineId] : ConstraintLineRef();
}

ConstraintCircleRef GeometricConstraintSystem::circle(int circleId) const
{
    return isValidCircle(circleId) ? m_circles[circleId] : ConstraintCircleRef();
}

double GeometricConstraintSystem::circleRadius(const ConstraintCircleRef& circleRef) const
{
    if (!isValidPoint(circleRef.center)) {
        return 0.0;
    }
    if (isValidPoint(circleRef.radiusPoint)) {
        return distance(m_points[circleRef.center].position, m_points[circleRef.radiusPoint].position);
    }
    return std::max(0.0, circleRef.fallbackRadius);
}

int GeometricConstraintSystem::addFixedPoint(int pointId, const QPointF& fixedPosition)
{
    if (!isValidPoint(pointId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::FixedPoint;
    constraint.p0 = pointId;
    constraint.targetPoint = fixedPosition;
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addCoincident(int pointA, int pointB)
{
    if (!isValidPoint(pointA) || !isValidPoint(pointB) || pointA == pointB) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Coincident;
    constraint.p0 = pointA;
    constraint.p1 = pointB;
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addHorizontal(int lineId)
{
    if (!isValidLine(lineId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Horizontal;
    constraint.lineA = m_lines[lineId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addVertical(int lineId)
{
    if (!isValidLine(lineId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Vertical;
    constraint.lineA = m_lines[lineId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addCollinear(int lineId, int pointId)
{
    if (!isValidLine(lineId) || !isValidPoint(pointId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Collinear;
    constraint.lineA = m_lines[lineId];
    constraint.p0 = pointId;
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addParallel(int lineAId, int lineBId)
{
    if (!isValidLine(lineAId) || !isValidLine(lineBId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Parallel;
    constraint.lineA = m_lines[lineAId];
    constraint.lineB = m_lines[lineBId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addPerpendicular(int lineAId, int lineBId)
{
    if (!isValidLine(lineAId) || !isValidLine(lineBId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Perpendicular;
    constraint.lineA = m_lines[lineAId];
    constraint.lineB = m_lines[lineBId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addEqualLength(int lineAId, int lineBId)
{
    if (!isValidLine(lineAId) || !isValidLine(lineBId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::EqualLength;
    constraint.lineA = m_lines[lineAId];
    constraint.lineB = m_lines[lineBId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addDistance(int pointA, int pointB, double distanceValue)
{
    if (!isValidPoint(pointA) || !isValidPoint(pointB) || pointA == pointB || distanceValue < 0.0) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Distance;
    constraint.p0 = pointA;
    constraint.p1 = pointB;
    constraint.targetValue = distanceValue;
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addAngle(int lineAId, int lineBId, double angleDeg)
{
    if (!isValidLine(lineAId) || !isValidLine(lineBId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Angle;
    constraint.lineA = m_lines[lineAId];
    constraint.lineB = m_lines[lineBId];
    constraint.targetValue = angleDeg;
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addPointOnLine(int pointId, int lineId)
{
    if (!isValidPoint(pointId) || !isValidLine(lineId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::PointOnLine;
    constraint.p0 = pointId;
    constraint.lineA = m_lines[lineId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addPointOnCircle(int pointId, int circleId)
{
    if (!isValidPoint(pointId) || !isValidCircle(circleId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::PointOnCircle;
    constraint.p0 = pointId;
    constraint.circleA = m_circles[circleId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addConcentric(int circleAId, int circleBId)
{
    if (!isValidCircle(circleAId) || !isValidCircle(circleBId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Concentric;
    constraint.circleA = m_circles[circleAId];
    constraint.circleB = m_circles[circleBId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addEqualRadius(int circleAId, int circleBId)
{
    if (!isValidCircle(circleAId) || !isValidCircle(circleBId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::EqualRadius;
    constraint.circleA = m_circles[circleAId];
    constraint.circleB = m_circles[circleBId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addTangentLineCircle(int lineId, int circleId)
{
    if (!isValidLine(lineId) || !isValidCircle(circleId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::TangentLineCircle;
    constraint.lineA = m_lines[lineId];
    constraint.circleA = m_circles[circleId];
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addMidpoint(int midpointId, int endpointA, int endpointB)
{
    if (!isValidPoint(midpointId) || !isValidPoint(endpointA) || !isValidPoint(endpointB) || endpointA == endpointB) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Midpoint;
    constraint.p0 = midpointId;
    constraint.p1 = endpointA;
    constraint.p2 = endpointB;
    return addConstraint(constraint);
}

int GeometricConstraintSystem::addSymmetric(int pointA, int pointB, int axisLineId)
{
    if (!isValidPoint(pointA) || !isValidPoint(pointB) || !isValidLine(axisLineId)) {
        return -1;
    }
    GeometricConstraint constraint;
    constraint.kind = ConstraintKind::Symmetric;
    constraint.p0 = pointA;
    constraint.p1 = pointB;
    constraint.lineA = m_lines[axisLineId];
    return addConstraint(constraint);
}

QVector<ConstraintResidual> GeometricConstraintSystem::residuals() const
{
    QVector<ConstraintResidual> values;
    values.reserve(m_constraints.size());
    for (int i = 0; i < m_constraints.size(); ++i) {
        if (!m_constraints[i].enabled) {
            continue;
        }
        ConstraintResidual r;
        r.constraintIndex = i;
        r.kind = m_constraints[i].kind;
        r.error = residual(m_constraints[i]);
        values.append(r);
    }
    return values;
}

double GeometricConstraintSystem::maxResidual() const
{
    double maxError = 0.0;
    for (const ConstraintResidual& value : residuals()) {
        maxError = std::max(maxError, value.error);
    }
    return maxError;
}

ConstraintGraphStats GeometricConstraintSystem::graphStats() const
{
    ConstraintGraphStats stats;
    stats.pointCount = m_points.size();
    stats.lineCount = m_lines.size();
    stats.circleCount = m_circles.size();
    for (const ConstraintPoint& point : m_points) {
        if (point.locked) {
            ++stats.lockedPointCount;
        }
    }
    for (const GeometricConstraint& constraint : m_constraints) {
        if (!constraint.enabled) {
            continue;
        }
        ++stats.enabledConstraintCount;
        stats.estimatedScalarConstraints += estimatedScalarConstraintCount(constraint.kind);
    }
    const int variableDof = std::max(0, 2 * (stats.pointCount - stats.lockedPointCount));
    stats.estimatedDegreesOfFreedom = variableDof - stats.estimatedScalarConstraints;
    stats.probablyUnderConstrained = stats.estimatedDegreesOfFreedom > 0;
    stats.probablyOverConstrained = stats.estimatedDegreesOfFreedom < 0;
    return stats;
}

QVector<ConstraintIssue> GeometricConstraintSystem::validate(double warningTolerance, double errorTolerance) const
{
    QVector<ConstraintIssue> issues;
    for (const ConstraintResidual& value : residuals()) {
        if (value.error <= warningTolerance) {
            continue;
        }
        ConstraintIssue issue;
        issue.constraintIndex = value.constraintIndex;
        issue.kind = value.kind;
        issue.residual = value.error;
        issue.severity = value.error >= errorTolerance ? ConstraintIssueSeverity::Error : ConstraintIssueSeverity::Warning;
        issues.append(issue);
    }
    return issues;
}

ConstraintSolveReport GeometricConstraintSystem::solve(const ConstraintSolveOptions& options)
{
    ConstraintSolveReport report;
    report.enabledConstraintCount = 0;
    for (const GeometricConstraint& constraint : m_constraints) {
        if (constraint.enabled) {
            ++report.enabledConstraintCount;
        }
    }

    report.initialMaxResidual = maxResidual();
    double previousResidual = report.initialMaxResidual;
    if (previousResidual <= options.tolerance) {
        report.converged = true;
        report.finalMaxResidual = previousResidual;
        return report;
    }

    for (int iteration = 0; iteration < options.maxIterations; ++iteration) {
        double maxDelta = 0.0;
        for (const GeometricConstraint& constraint : m_constraints) {
            if (!constraint.enabled) {
                continue;
            }
            maxDelta = std::max(maxDelta, applyConstraint(constraint, options));
        }

        const double currentResidual = maxResidual();
        report.iterations = iteration + 1;
        report.lastIterationDelta = maxDelta;
        report.finalMaxResidual = currentResidual;
        if (currentResidual <= options.tolerance || maxDelta <= options.tolerance * 0.1) {
            report.converged = currentResidual <= std::max(options.tolerance, previousResidual);
            break;
        }
        if (options.stopWhenDiverging && iteration > 8 && currentResidual > previousResidual * 50.0) {
            break;
        }
        previousResidual = currentResidual;
    }

    report.converged = report.finalMaxResidual <= options.tolerance;
    return report;
}

bool GeometricConstraintSystem::isValidPoint(int pointId) const
{
    return validIndex(pointId, m_points.size());
}

bool GeometricConstraintSystem::isValidLine(int lineId) const
{
    return validIndex(lineId, m_lines.size()) && isValidPoint(m_lines[lineId].start) && isValidPoint(m_lines[lineId].end);
}

bool GeometricConstraintSystem::isValidCircle(int circleId) const
{
    return validIndex(circleId, m_circles.size()) && isValidPoint(m_circles[circleId].center)
        && (m_circles[circleId].radiusPoint < 0 || isValidPoint(m_circles[circleId].radiusPoint));
}

bool GeometricConstraintSystem::isMovablePoint(int pointId) const
{
    return isValidPoint(pointId) && !m_points[pointId].locked && m_points[pointId].weight > 0.0;
}

int GeometricConstraintSystem::addConstraint(const GeometricConstraint& constraint)
{
    if (constraint.strength <= 0.0 || !isFinite(constraint.strength)) {
        return -1;
    }
    m_constraints.append(constraint);
    return m_constraints.size() - 1;
}

QPointF GeometricConstraintSystem::lineStart(const ConstraintLineRef& lineRef) const
{
    return pointPosition(lineRef.start);
}

QPointF GeometricConstraintSystem::lineEnd(const ConstraintLineRef& lineRef) const
{
    return pointPosition(lineRef.end);
}

QPointF GeometricConstraintSystem::circleCenter(const ConstraintCircleRef& circleRef) const
{
    return pointPosition(circleRef.center);
}

QPointF GeometricConstraintSystem::circleRadiusPoint(const ConstraintCircleRef& circleRef) const
{
    return isValidPoint(circleRef.radiusPoint) ? pointPosition(circleRef.radiusPoint) : QPointF();
}

double GeometricConstraintSystem::residual(const GeometricConstraint& constraint) const
{
    switch (constraint.kind) {
    case ConstraintKind::FixedPoint:
        return isValidPoint(constraint.p0) ? distance(pointPosition(constraint.p0), constraint.targetPoint) : std::numeric_limits<double>::infinity();
    case ConstraintKind::Coincident:
        return isValidPoint(constraint.p0) && isValidPoint(constraint.p1)
            ? distance(pointPosition(constraint.p0), pointPosition(constraint.p1))
            : std::numeric_limits<double>::infinity();
    case ConstraintKind::Horizontal: {
        const QPointF a = lineStart(constraint.lineA);
        const QPointF b = lineEnd(constraint.lineA);
        return std::abs(a.y() - b.y());
    }
    case ConstraintKind::Vertical: {
        const QPointF a = lineStart(constraint.lineA);
        const QPointF b = lineEnd(constraint.lineA);
        return std::abs(a.x() - b.x());
    }
    case ConstraintKind::Collinear: {
        if (!isValidPoint(constraint.p0)) {
            return std::numeric_limits<double>::infinity();
        }
        return std::abs(signedDistancePointToLine(pointPosition(constraint.p0), lineStart(constraint.lineA), lineEnd(constraint.lineA)));
    }
    case ConstraintKind::Parallel: {
        const QPointF da = lineEnd(constraint.lineA) - lineStart(constraint.lineA);
        const QPointF db = lineEnd(constraint.lineB) - lineStart(constraint.lineB);
        const double denom = std::max(kMinSolverLength, length(da) * length(db));
        return std::abs(cross(da, db)) / denom;
    }
    case ConstraintKind::Perpendicular: {
        const QPointF da = lineEnd(constraint.lineA) - lineStart(constraint.lineA);
        const QPointF db = lineEnd(constraint.lineB) - lineStart(constraint.lineB);
        const double denom = std::max(kMinSolverLength, length(da) * length(db));
        return std::abs(dot(da, db)) / denom;
    }
    case ConstraintKind::EqualLength: {
        const double la = distance(lineStart(constraint.lineA), lineEnd(constraint.lineA));
        const double lb = distance(lineStart(constraint.lineB), lineEnd(constraint.lineB));
        return std::abs(la - lb);
    }
    case ConstraintKind::Distance:
        return isValidPoint(constraint.p0) && isValidPoint(constraint.p1)
            ? std::abs(distance(pointPosition(constraint.p0), pointPosition(constraint.p1)) - constraint.targetValue)
            : std::numeric_limits<double>::infinity();
    case ConstraintKind::Angle: {
        const QPointF da = lineEnd(constraint.lineA) - lineStart(constraint.lineA);
        const QPointF db = lineEnd(constraint.lineB) - lineStart(constraint.lineB);
        if (length(da) <= kMinSolverLength || length(db) <= kMinSolverLength) {
            return std::numeric_limits<double>::infinity();
        }
        const double actual = signedAngleDegBetween(da, db);
        return std::abs(signedShortestSpanDeg(constraint.targetValue, actual));
    }
    case ConstraintKind::PointOnLine: {
        if (!isValidPoint(constraint.p0)) {
            return std::numeric_limits<double>::infinity();
        }
        return distance(pointPosition(constraint.p0), projectPointOnInfiniteLine(pointPosition(constraint.p0), lineStart(constraint.lineA), lineEnd(constraint.lineA)));
    }
    case ConstraintKind::PointOnCircle: {
        if (!isValidPoint(constraint.p0)) {
            return std::numeric_limits<double>::infinity();
        }
        return std::abs(distance(pointPosition(constraint.p0), circleCenter(constraint.circleA)) - circleRadius(constraint.circleA));
    }
    case ConstraintKind::Concentric:
        return distance(circleCenter(constraint.circleA), circleCenter(constraint.circleB));
    case ConstraintKind::EqualRadius:
        return std::abs(circleRadius(constraint.circleA) - circleRadius(constraint.circleB));
    case ConstraintKind::TangentLineCircle: {
        const double d = std::abs(signedDistancePointToLine(circleCenter(constraint.circleA), lineStart(constraint.lineA), lineEnd(constraint.lineA)));
        return std::abs(d - circleRadius(constraint.circleA));
    }
    case ConstraintKind::Midpoint: {
        if (!isValidPoint(constraint.p0) || !isValidPoint(constraint.p1) || !isValidPoint(constraint.p2)) {
            return std::numeric_limits<double>::infinity();
        }
        return distance(pointPosition(constraint.p0), midpoint(pointPosition(constraint.p1), pointPosition(constraint.p2)));
    }
    case ConstraintKind::Symmetric: {
        if (!isValidPoint(constraint.p0) || !isValidPoint(constraint.p1)) {
            return std::numeric_limits<double>::infinity();
        }
        const QPointF mirrored = mirrorPoint(pointPosition(constraint.p0), lineStart(constraint.lineA), lineEnd(constraint.lineA));
        return distance(mirrored, pointPosition(constraint.p1));
    }
    }
    return std::numeric_limits<double>::infinity();
}

void GeometricConstraintSystem::movePoint(int pointId, const QPointF& delta, double strength, const ConstraintSolveOptions& options)
{
    if (!isMovablePoint(pointId) || !isFinite(delta)) {
        return;
    }
    const double safeStrength = clamp01(options.damping * strength / std::max(kMinSolverLength, m_points[pointId].weight));
    m_points[pointId].position += limitedVector(delta * safeStrength, options.maxStep);
}

void GeometricConstraintSystem::movePointToward(int pointId, const QPointF& target, double strength, const ConstraintSolveOptions& options)
{
    if (!isValidPoint(pointId)) {
        return;
    }
    movePoint(pointId, target - pointPosition(pointId), strength, options);
}

void GeometricConstraintSystem::movePairToward(int pointA, int pointB, const QPointF& targetA, const QPointF& targetB,
                                               double strength, const ConstraintSolveOptions& options)
{
    const bool movableA = isMovablePoint(pointA);
    const bool movableB = isMovablePoint(pointB);
    if (movableA && movableB) {
        movePoint(pointA, targetA - pointPosition(pointA), strength, options);
        movePoint(pointB, targetB - pointPosition(pointB), strength, options);
    } else if (movableA) {
        movePointToward(pointA, targetA, strength, options);
    } else if (movableB) {
        movePointToward(pointB, targetB, strength, options);
    }
}

double GeometricConstraintSystem::applyConstraint(const GeometricConstraint& constraint, const ConstraintSolveOptions& options)
{
    const double before = residual(constraint);
    const double strength = clamp01(constraint.strength);

    switch (constraint.kind) {
    case ConstraintKind::FixedPoint:
        movePointToward(constraint.p0, constraint.targetPoint, strength, options);
        break;

    case ConstraintKind::Coincident: {
        if (!isValidPoint(constraint.p0) || !isValidPoint(constraint.p1)) {
            break;
        }
        const QPointF a = pointPosition(constraint.p0);
        const QPointF b = pointPosition(constraint.p1);
        if (isMovablePoint(constraint.p0) && isMovablePoint(constraint.p1)) {
            const QPointF target = midpoint(a, b);
            movePointToward(constraint.p0, target, strength, options);
            movePointToward(constraint.p1, target, strength, options);
        } else if (isMovablePoint(constraint.p0)) {
            movePointToward(constraint.p0, b, strength, options);
        } else if (isMovablePoint(constraint.p1)) {
            movePointToward(constraint.p1, a, strength, options);
        }
        break;
    }

    case ConstraintKind::Horizontal: {
        const int aId = constraint.lineA.start;
        const int bId = constraint.lineA.end;
        const QPointF a = pointPosition(aId);
        const QPointF b = pointPosition(bId);
        if (isMovablePoint(aId) && isMovablePoint(bId)) {
            const double y = (a.y() + b.y()) * 0.5;
            movePointToward(aId, QPointF(a.x(), y), strength, options);
            movePointToward(bId, QPointF(b.x(), y), strength, options);
        } else if (isMovablePoint(aId)) {
            movePointToward(aId, QPointF(a.x(), b.y()), strength, options);
        } else if (isMovablePoint(bId)) {
            movePointToward(bId, QPointF(b.x(), a.y()), strength, options);
        }
        break;
    }

    case ConstraintKind::Vertical: {
        const int aId = constraint.lineA.start;
        const int bId = constraint.lineA.end;
        const QPointF a = pointPosition(aId);
        const QPointF b = pointPosition(bId);
        if (isMovablePoint(aId) && isMovablePoint(bId)) {
            const double x = (a.x() + b.x()) * 0.5;
            movePointToward(aId, QPointF(x, a.y()), strength, options);
            movePointToward(bId, QPointF(x, b.y()), strength, options);
        } else if (isMovablePoint(aId)) {
            movePointToward(aId, QPointF(b.x(), a.y()), strength, options);
        } else if (isMovablePoint(bId)) {
            movePointToward(bId, QPointF(a.x(), b.y()), strength, options);
        }
        break;
    }

    case ConstraintKind::Collinear: {
        const QPointF projected = projectPointOnInfiniteLine(pointPosition(constraint.p0), lineStart(constraint.lineA), lineEnd(constraint.lineA));
        movePointToward(constraint.p0, projected, strength, options);
        break;
    }

    case ConstraintKind::Parallel:
    case ConstraintKind::Perpendicular:
    case ConstraintKind::Angle: {
        const int aId = constraint.lineB.start;
        const int bId = constraint.lineB.end;
        const QPointF a0 = lineStart(constraint.lineA);
        const QPointF a1 = lineEnd(constraint.lineA);
        const QPointF b0 = pointPosition(aId);
        const QPointF b1 = pointPosition(bId);
        const QPointF da = a1 - a0;
        const double lenB = distance(b0, b1);
        if (length(da) <= kMinSolverLength || lenB <= kMinSolverLength) {
            break;
        }
        double angle = 0.0;
        if (constraint.kind == ConstraintKind::Perpendicular) {
            angle = 90.0;
        } else if (constraint.kind == ConstraintKind::Angle) {
            angle = constraint.targetValue;
        }
        QPointF dir = normalized(rotateVectorDeg(da, angle));
        const QPointF currentDir = normalized(b1 - b0);
        if (dot(dir, currentDir) < 0.0 && constraint.kind != ConstraintKind::Angle) {
            dir = -dir;
        }
        const QPointF c = midpoint(b0, b1);
        const QPointF targetA = c - dir * (lenB * 0.5);
        const QPointF targetB = c + dir * (lenB * 0.5);
        movePairToward(aId, bId, targetA, targetB, strength, options);
        break;
    }

    case ConstraintKind::EqualLength: {
        const int a0Id = constraint.lineA.start;
        const int a1Id = constraint.lineA.end;
        const int b0Id = constraint.lineB.start;
        const int b1Id = constraint.lineB.end;
        const QPointF a0 = pointPosition(a0Id);
        const QPointF a1 = pointPosition(a1Id);
        const QPointF b0 = pointPosition(b0Id);
        const QPointF b1 = pointPosition(b1Id);
        const double lenA = distance(a0, a1);
        const double lenB = distance(b0, b1);
        if (lenA <= kMinSolverLength || lenB <= kMinSolverLength) {
            break;
        }
        const double targetLength = (lenA + lenB) * 0.5;
        const QPointF dirA = normalized(a1 - a0);
        const QPointF dirB = normalized(b1 - b0);
        const QPointF midA = midpoint(a0, a1);
        const QPointF midB = midpoint(b0, b1);
        movePairToward(a0Id, a1Id, midA - dirA * (targetLength * 0.5), midA + dirA * (targetLength * 0.5), strength, options);
        movePairToward(b0Id, b1Id, midB - dirB * (targetLength * 0.5), midB + dirB * (targetLength * 0.5), strength, options);
        break;
    }

    case ConstraintKind::Distance: {
        const int aId = constraint.p0;
        const int bId = constraint.p1;
        const QPointF a = pointPosition(aId);
        const QPointF b = pointPosition(bId);
        QPointF dir = safeDirection(a, b);
        const QPointF c = midpoint(a, b);
        const QPointF targetA = c - dir * (constraint.targetValue * 0.5);
        const QPointF targetB = c + dir * (constraint.targetValue * 0.5);
        if (isMovablePoint(aId) && isMovablePoint(bId)) {
            movePairToward(aId, bId, targetA, targetB, strength, options);
        } else if (isMovablePoint(aId)) {
            movePointToward(aId, b - dir * constraint.targetValue, strength, options);
        } else if (isMovablePoint(bId)) {
            movePointToward(bId, a + dir * constraint.targetValue, strength, options);
        }
        break;
    }

    case ConstraintKind::PointOnLine: {
        const QPointF projected = projectPointOnInfiniteLine(pointPosition(constraint.p0), lineStart(constraint.lineA), lineEnd(constraint.lineA));
        movePointToward(constraint.p0, projected, strength, options);
        break;
    }

    case ConstraintKind::PointOnCircle: {
        const QPointF center = circleCenter(constraint.circleA);
        const double radius = circleRadius(constraint.circleA);
        QPointF dir = pointPosition(constraint.p0) - center;
        if (length(dir) <= kMinSolverLength) {
            dir = QPointF(1.0, 0.0);
        }
        dir = normalized(dir);
        movePointToward(constraint.p0, center + dir * radius, strength, options);
        break;
    }

    case ConstraintKind::Concentric: {
        const int aId = constraint.circleA.center;
        const int bId = constraint.circleB.center;
        const QPointF a = pointPosition(aId);
        const QPointF b = pointPosition(bId);
        if (isMovablePoint(aId) && isMovablePoint(bId)) {
            const QPointF c = midpoint(a, b);
            movePointToward(aId, c, strength, options);
            movePointToward(bId, c, strength, options);
        } else if (isMovablePoint(aId)) {
            movePointToward(aId, b, strength, options);
        } else if (isMovablePoint(bId)) {
            movePointToward(bId, a, strength, options);
        }
        break;
    }

    case ConstraintKind::EqualRadius: {
        const double rA = circleRadius(constraint.circleA);
        const double rB = circleRadius(constraint.circleB);
        if (rA <= kMinSolverLength || rB <= kMinSolverLength) {
            break;
        }
        const double targetRadius = (rA + rB) * 0.5;
        if (isValidPoint(constraint.circleA.radiusPoint)) {
            const QPointF center = circleCenter(constraint.circleA);
            const QPointF dir = normalized(circleRadiusPoint(constraint.circleA) - center);
            movePointToward(constraint.circleA.radiusPoint, center + dir * targetRadius, strength, options);
        }
        if (isValidPoint(constraint.circleB.radiusPoint)) {
            const QPointF center = circleCenter(constraint.circleB);
            const QPointF dir = normalized(circleRadiusPoint(constraint.circleB) - center);
            movePointToward(constraint.circleB.radiusPoint, center + dir * targetRadius, strength, options);
        }
        break;
    }

    case ConstraintKind::TangentLineCircle: {
        const QPointF lineA = lineStart(constraint.lineA);
        const QPointF lineB = lineEnd(constraint.lineA);
        const QPointF center = circleCenter(constraint.circleA);
        const double radius = circleRadius(constraint.circleA);
        const QPointF projected = projectPointOnInfiniteLine(center, lineA, lineB);
        QPointF normal = center - projected;
        if (length(normal) <= kMinSolverLength) {
            normal = perpendicularLeft(lineB - lineA);
        }
        normal = normalized(normal);
        const QPointF targetCenter = projected + normal * radius;
        movePointToward(constraint.circleA.center, targetCenter, strength, options);
        break;
    }

    case ConstraintKind::Midpoint: {
        const QPointF a = pointPosition(constraint.p1);
        const QPointF b = pointPosition(constraint.p2);
        const QPointF m = pointPosition(constraint.p0);
        const QPointF targetMid = midpoint(a, b);
        if (isMovablePoint(constraint.p0)) {
            movePointToward(constraint.p0, targetMid, strength, options);
        } else {
            const QPointF delta = m - targetMid;
            movePoint(constraint.p1, delta, strength * 0.5, options);
            movePoint(constraint.p2, delta, strength * 0.5, options);
        }
        break;
    }

    case ConstraintKind::Symmetric: {
        const QPointF a = pointPosition(constraint.p0);
        const QPointF b = pointPosition(constraint.p1);
        const QPointF mirroredA = mirrorPoint(a, lineStart(constraint.lineA), lineEnd(constraint.lineA));
        const QPointF mirroredB = mirrorPoint(b, lineStart(constraint.lineA), lineEnd(constraint.lineA));
        if (isMovablePoint(constraint.p0) && isMovablePoint(constraint.p1)) {
            movePointToward(constraint.p0, mirroredB, strength * 0.5, options);
            movePointToward(constraint.p1, mirroredA, strength * 0.5, options);
        } else if (isMovablePoint(constraint.p0)) {
            movePointToward(constraint.p0, mirroredB, strength, options);
        } else if (isMovablePoint(constraint.p1)) {
            movePointToward(constraint.p1, mirroredA, strength, options);
        }
        break;
    }
    }

    const double after = residual(constraint);
    if (!std::isfinite(before) || !std::isfinite(after)) {
        return 0.0;
    }
    return std::abs(before - after);
}

int estimatedScalarConstraintCount(ConstraintKind kind)
{
    switch (kind) {
    case ConstraintKind::FixedPoint:
    case ConstraintKind::Coincident:
    case ConstraintKind::Concentric:
    case ConstraintKind::Midpoint:
    case ConstraintKind::Symmetric:
        return 2;
    case ConstraintKind::Horizontal:
    case ConstraintKind::Vertical:
    case ConstraintKind::Collinear:
    case ConstraintKind::Parallel:
    case ConstraintKind::Perpendicular:
    case ConstraintKind::EqualLength:
    case ConstraintKind::Distance:
    case ConstraintKind::Angle:
    case ConstraintKind::PointOnLine:
    case ConstraintKind::PointOnCircle:
    case ConstraintKind::EqualRadius:
    case ConstraintKind::TangentLineCircle:
        return 1;
    }
    return 1;
}

const char* constraintKindName(ConstraintKind kind)
{
    switch (kind) {
    case ConstraintKind::FixedPoint:
        return "FixedPoint";
    case ConstraintKind::Coincident:
        return "Coincident";
    case ConstraintKind::Horizontal:
        return "Horizontal";
    case ConstraintKind::Vertical:
        return "Vertical";
    case ConstraintKind::Collinear:
        return "Collinear";
    case ConstraintKind::Parallel:
        return "Parallel";
    case ConstraintKind::Perpendicular:
        return "Perpendicular";
    case ConstraintKind::EqualLength:
        return "EqualLength";
    case ConstraintKind::Distance:
        return "Distance";
    case ConstraintKind::Angle:
        return "Angle";
    case ConstraintKind::PointOnLine:
        return "PointOnLine";
    case ConstraintKind::PointOnCircle:
        return "PointOnCircle";
    case ConstraintKind::Concentric:
        return "Concentric";
    case ConstraintKind::EqualRadius:
        return "EqualRadius";
    case ConstraintKind::TangentLineCircle:
        return "TangentLineCircle";
    case ConstraintKind::Midpoint:
        return "Midpoint";
    case ConstraintKind::Symmetric:
        return "Symmetric";
    }
    return "Unknown";
}

} // namespace CadGeometry
