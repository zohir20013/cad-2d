#pragma once

#include "cad/CadDocument.h"
#include "cad/CadEntity.h"

#include <QJsonObject>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace CadMeasure {

enum class MeasurementKind {
    Distance,
    Angle,
    Length,
    Area,
    Perimeter,
    Radius,
    Diameter,
    BoundingBox,
    Coordinate,
    Unknown
};

struct MeasurementResult {
    MeasurementKind kind = MeasurementKind::Unknown;
    double value = 0.0;
    double secondaryValue = 0.0;
    QString unit;
    QString label;
    QVector<QPointF> referencePoints;
    QRectF bounds;
    bool valid = false;

    QJsonObject toJson() const;
};

struct EntityMeasurement {
    int index = -1;
    QString entityType;
    QString layer;
    double length = 0.0;
    double area = 0.0;
    QRectF bounds;
    bool degenerate = false;

    QJsonObject toJson() const;
};

struct DrawingMeasurementSummary {
    int entityCount = 0;
    int measuredCount = 0;
    double totalLength = 0.0;
    double totalClosedArea = 0.0;
    QRectF extents;
    QVector<EntityMeasurement> entities;

    QJsonObject toJson() const;
};

class CadMeasurementEngine
{
public:
    static MeasurementResult distance(const QPointF& a, const QPointF& b, const QString& unit = QStringLiteral("drawing units"));
    static MeasurementResult angle(const QPointF& a, const QPointF& vertex, const QPointF& b, bool degrees = true);
    static MeasurementResult coordinate(const QPointF& point, const QString& unit = QStringLiteral("drawing units"));
    static MeasurementResult boundingBox(const QRectF& rect, const QString& unit = QStringLiteral("drawing units"));

    static double polylineLength(const QVector<QPointF>& points, bool closed = false);
    static double polygonArea(const QVector<QPointF>& points);
    static QPointF polygonCentroid(const QVector<QPointF>& points);
    static double entityLength(const CadEntity& entity);
    static double entityArea(const CadEntity& entity);
    static QRectF entityBounds(const CadEntity& entity);

    static EntityMeasurement measureEntity(const CadEntity& entity, int index = -1);
    static DrawingMeasurementSummary summarizeDocument(const CadDocument& document, const QVector<int>& indices = QVector<int>());
    static MeasurementResult selectionLength(const CadDocument& document, const QVector<int>& indices, const QString& unit = QStringLiteral("drawing units"));
    static MeasurementResult selectionArea(const CadDocument& document, const QVector<int>& indices, const QString& unit = QStringLiteral("drawing units"));

    static int nearestEntity(const CadDocument& document, const QPointF& point, double* distanceOut = nullptr, const QVector<int>& candidateIndices = QVector<int>());
    static QVector<int> entitiesInsideRadius(const CadDocument& document, const QPointF& center, double radius);
    static QString formatNumber(double value, int precision = 3, bool trimTrailingZeros = true);
    static QString formatDistance(double value, const QString& unit, int precision = 3);
    static QString formatArea(double value, const QString& unit, int precision = 3);

private:
    static QVector<int> normalizeIndices(const CadDocument& document, const QVector<int>& indices);
};

} // namespace CadMeasure
