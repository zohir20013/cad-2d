#pragma once

#include "cad/CadEntity.h"
#include "geometry/GeometryKernelAdvanced.h"

#include <QPointF>
#include <QVector>

#include <memory>
#include <vector>

namespace CadSnap {

enum SnapModeFlag {
    SnapEndpoint             = 1 << 0,
    SnapMidpoint             = 1 << 1,
    SnapCenter               = 1 << 2,
    SnapQuadrant             = 1 << 3,
    SnapIntersection         = 1 << 4,
    SnapPerpendicular        = 1 << 5,
    SnapTangent              = 1 << 6,
    SnapNearest              = 1 << 7,
    SnapExtension            = 1 << 8,
    SnapApparentIntersection = 1 << 9
};

using SnapModeMask = int;

struct SmartSnapContext {
    QPointF cursor;
    double aperture = 12.0;
    double drawingTolerance = 1.0e-6;
    SnapModeMask modes = SnapEndpoint | SnapMidpoint | SnapCenter | SnapIntersection | SnapNearest;
    QVector<QPointF> trackingPoints;
    QVector<double> polarAnglesDeg {0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0};
};

struct SmartSnapResult {
    bool found = false;
    CadGeometry::SnapCandidate candidate;
    QVector<CadGeometry::SnapCandidate> candidates;
};

bool hasMode(SnapModeMask mask, SnapModeFlag flag);
QVector<CadGeometry::SnapCandidate> collectEntitySnaps(const CadEntity& entity, int entityIndex,
                                                       const SmartSnapContext& context, int curveSegments = 96);
QVector<CadGeometry::SnapCandidate> collectIntersectionSnaps(const std::vector<std::unique_ptr<CadEntity>>& entities,
                                                             const SmartSnapContext& context, int curveSegments = 96);
QVector<CadGeometry::SnapCandidate> collectTrackingSnaps(const SmartSnapContext& context);
SmartSnapResult snap(const std::vector<std::unique_ptr<CadEntity>>& entities,
                     const SmartSnapContext& context, int curveSegments = 96);

QPointF snapToGrid(const QPointF& point, double gridSpacing, const QPointF& origin = QPointF());
QPointF orthogonalTrackingPoint(const QPointF& basePoint, const QPointF& cursor);
QPointF polarTrackingPoint(const QPointF& basePoint, const QPointF& cursor,
                           const QVector<double>& anglesDeg, double* chosenAngleDeg = nullptr);

} // namespace CadSnap
