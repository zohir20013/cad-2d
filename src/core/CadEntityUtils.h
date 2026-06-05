#pragma once

#include "cad/CadEntity.h"

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <vector>
#include <memory>

namespace CadCore {

struct EntityMetrics {
    QRectF bounds;
    double length = 0.0;
    double area = 0.0;
    int vertexCount = 0;
    bool closed = false;
    bool valid = true;
    QString typeName;
};

struct DrawingStatistics {
    int totalEntities = 0;
    int lineCount = 0;
    int circleCount = 0;
    int arcCount = 0;
    int polylineCount = 0;
    int textCount = 0;
    int dimensionCount = 0;
    int hatchCount = 0;
    int blockReferenceCount = 0;
    QRectF drawingBounds;
    double totalCurveLength = 0.0;
    double totalClosedArea = 0.0;
};

QString entityTypeName(CadEntity::Type type);
QString entityTypeName(const CadEntity& entity);
QVector<QPointF> representativePoints(const CadEntity& entity, int curveSegments = 96);
QRectF entityBoundingRect(const CadEntity& entity, int curveSegments = 96);
double entityLength(const CadEntity& entity, int curveSegments = 96);
double entityArea(const CadEntity& entity, int curveSegments = 96);
EntityMetrics entityMetrics(const CadEntity& entity, int curveSegments = 96);
QRectF documentBoundingRect(const std::vector<std::unique_ptr<CadEntity>>& entities, int curveSegments = 96);
DrawingStatistics drawingStatistics(const std::vector<std::unique_ptr<CadEntity>>& entities, int curveSegments = 96);

double distanceToEntity(const QPointF& point, const CadEntity& entity, QPointF* nearestPoint = nullptr, int curveSegments = 96);
bool isEntityDegenerate(const CadEntity& entity, double tolerance = 1.0e-7);
QJsonObject entitySummaryJson(const CadEntity& entity, int curveSegments = 96);

} // namespace CadCore
