#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CadGeometry {
struct DimensionLineGeometry;
struct AngularDimensionGeometry;
}

namespace CadAnnotation {

// UI-independent annotation engine for CAD dimensions, leaders, callouts and
// toleranced measurement text.  The structs deliberately contain only geometry
// and strings, so the drawing layer can render them with QPainter/QGraphicsItem
// without duplicating dimension math.

enum class LengthUnit {
    Unitless,
    Millimeter,
    Centimeter,
    Meter,
    Inch,
    Foot
};

enum class AngleUnit {
    DecimalDegrees,
    DegreesMinutesSeconds,
    Radians
};

enum class ArrowheadType {
    ClosedFilled,
    ClosedBlank,
    Open,
    ArchitecturalTick,
    Dot,
    None
};

enum class TextAttachment {
    Center,
    Above,
    Below,
    Left,
    Right,
    OverDimensionLine,
    Outside
};

enum class ToleranceMode {
    None,
    Symmetric,
    Deviation,
    Limits,
    Basic
};

enum class DimensionKind {
    Aligned,
    LinearHorizontal,
    LinearVertical,
    Rotated,
    Angular,
    Radius,
    Diameter,
    OrdinateX,
    OrdinateY,
    ArcLength,
    Baseline,
    Continue
};

enum class AssociativeReferenceType {
    None,
    PointPair,
    Line,
    Circle,
    Arc,
    PolylineSegment
};

struct TextStyle {
    QString fontFamily = QStringLiteral("Arial");
    double height = 2.5;
    double widthFactor = 1.0;
    double obliqueAngleDeg = 0.0;
    bool bold = false;
    bool italic = false;
};

struct DimensionStyle {
    TextStyle text;
    LengthUnit lengthUnit = LengthUnit::Millimeter;
    AngleUnit angleUnit = AngleUnit::DecimalDegrees;
    ArrowheadType arrowhead = ArrowheadType::ClosedFilled;
    double scale = 1.0;
    double arrowSize = 2.5;
    double extensionOffset = 1.25;
    double extensionBeyondDimension = 1.25;
    double dimensionLineGap = 0.625;
    double textGap = 0.8;
    double centerMarkSize = 2.5;
    double jogAngleDeg = 45.0;
    int linearPrecision = 2;
    int angularPrecision = 2;
    bool suppressTrailingZeros = true;
    bool showUnitSuffix = false;
    bool useDecimalComma = false;
    QString prefix;
    QString suffix;
};

struct ToleranceSpec {
    ToleranceMode mode = ToleranceMode::None;
    double upper = 0.0;
    double lower = 0.0;
    int precision = -1; // -1 means inherit dimension precision.
};

struct AnnotationTextBlock {
    QString text;
    QPointF position;
    double rotationDeg = 0.0;
    QRectF estimatedBox;
    TextAttachment attachment = TextAttachment::Center;
};

struct AnnotationSegment {
    QPointF start;
    QPointF end;
};

struct AnnotationArc {
    QPointF center;
    double radius = 0.0;
    double startAngleDeg = 0.0;
    double spanAngleDeg = 0.0;
};

struct ArrowheadGeometry {
    bool valid = false;
    ArrowheadType type = ArrowheadType::ClosedFilled;
    QVector<QPointF> outline;
    QPointF tip;
    QPointF tailCenter;
    double rotationDeg = 0.0;
    bool filled = true;
};

struct DimensionVisualGeometry {
    bool valid = false;
    DimensionKind kind = DimensionKind::Aligned;
    QVector<AnnotationSegment> dimensionLines;
    QVector<AnnotationSegment> extensionLines;
    QVector<AnnotationSegment> constructionLines;
    QVector<AnnotationArc> arcs;
    QVector<ArrowheadGeometry> arrows;
    QVector<AnnotationTextBlock> textBlocks;
    QVector<QPointF> gripPoints;
    QString primaryText;
    QString alternateText;
    double measurement = 0.0;
    double measurementAngleDeg = 0.0;
    QStringList warnings;
};

struct LeaderStyle {
    TextStyle text;
    ArrowheadType arrowhead = ArrowheadType::ClosedFilled;
    double scale = 1.0;
    double arrowSize = 2.5;
    double landingLength = 8.0;
    double textGap = 1.0;
    bool underline = false;
};

struct LeaderGeometry {
    bool valid = false;
    QVector<AnnotationSegment> segments;
    QVector<ArrowheadGeometry> arrows;
    AnnotationTextBlock text;
    QVector<QPointF> gripPoints;
};

struct TableStyle {
    TextStyle text;
    double rowHeight = 7.0;
    double columnWidth = 28.0;
    double padding = 1.5;
    double scale = 1.0;
    bool titleRow = true;
};

struct TableCellGeometry {
    QRectF box;
    AnnotationTextBlock text;
    int row = 0;
    int column = 0;
};

struct TableGeometry {
    bool valid = false;
    QRectF outerBox;
    QVector<AnnotationSegment> gridLines;
    QVector<TableCellGeometry> cells;
};

struct AssociativeDimensionReference {
    AssociativeReferenceType type = AssociativeReferenceType::None;
    int entityIdA = -1;
    int entityIdB = -1;
    int vertexIndexA = -1;
    int vertexIndexB = -1;
    QPointF referencePointA;
    QPointF referencePointB;
    QPointF center;
    double radius = 0.0;
    double startAngleDeg = 0.0;
    double spanAngleDeg = 0.0;
};

struct DimensionDefinition {
    DimensionKind kind = DimensionKind::Aligned;
    AssociativeDimensionReference reference;
    DimensionStyle style;
    ToleranceSpec tolerance;
    QPointF definitionPoint;
    QPointF textOverridePosition;
    bool hasTextOverridePosition = false;
    QString explicitText; // When non-empty, replaces generated measurement text. Use <> as measurement placeholder.
    double rotationDeg = 0.0;
    double offset = 10.0;
};

QString unitSuffix(LengthUnit unit);
QString formatLength(double value, const DimensionStyle& style, const ToleranceSpec& tolerance = ToleranceSpec());
QString formatAngle(double valueDeg, const DimensionStyle& style, const ToleranceSpec& tolerance = ToleranceSpec());
QString applyDimensionTextOverride(const QString& generatedMeasurement, const QString& explicitText);

QRectF estimateTextBox(const QString& text, const TextStyle& style, double rotationDeg = 0.0);
QStringList wrapTextByWidth(const QString& text, double maxWidth, const TextStyle& style);

ArrowheadGeometry makeArrowhead(const QPointF& tip, const QPointF& directionAwayFromTip,
                                const DimensionStyle& style);
ArrowheadGeometry makeArrowhead(const QPointF& tip, const QPointF& directionAwayFromTip,
                                ArrowheadType type, double size);

DimensionVisualGeometry makeAlignedDimension(const QPointF& a, const QPointF& b, double offset,
                                             const DimensionStyle& style = DimensionStyle(),
                                             const ToleranceSpec& tolerance = ToleranceSpec(),
                                             const QString& explicitText = QString());
DimensionVisualGeometry makeLinearDimension(const QPointF& a, const QPointF& b, double offset,
                                            bool horizontal,
                                            const DimensionStyle& style = DimensionStyle(),
                                            const ToleranceSpec& tolerance = ToleranceSpec(),
                                            const QString& explicitText = QString());
DimensionVisualGeometry makeRotatedDimension(const QPointF& a, const QPointF& b, const QPointF& dimensionPoint,
                                             double rotationDeg,
                                             const DimensionStyle& style = DimensionStyle(),
                                             const ToleranceSpec& tolerance = ToleranceSpec(),
                                             const QString& explicitText = QString());
DimensionVisualGeometry makeAngularDimension(const QPointF& center, const QPointF& armA, const QPointF& armB,
                                             double radius,
                                             const DimensionStyle& style = DimensionStyle(),
                                             const ToleranceSpec& tolerance = ToleranceSpec(),
                                             bool clockwise = false,
                                             const QString& explicitText = QString());
DimensionVisualGeometry makeRadiusDimension(const QPointF& center, const QPointF& pointOnCircle,
                                            const DimensionStyle& style = DimensionStyle(),
                                            const ToleranceSpec& tolerance = ToleranceSpec(),
                                            const QString& explicitText = QString());
DimensionVisualGeometry makeDiameterDimension(const QPointF& center, const QPointF& pointOnCircle,
                                              const DimensionStyle& style = DimensionStyle(),
                                              const ToleranceSpec& tolerance = ToleranceSpec(),
                                              const QString& explicitText = QString());
DimensionVisualGeometry makeOrdinateDimension(const QPointF& origin, const QPointF& featurePoint,
                                              const QPointF& leaderEnd, bool xOrdinate,
                                              const DimensionStyle& style = DimensionStyle(),
                                              const ToleranceSpec& tolerance = ToleranceSpec(),
                                              const QString& explicitText = QString());
DimensionVisualGeometry makeArcLengthDimension(const QPointF& center, double radius,
                                               double startAngleDeg, double spanAngleDeg,
                                               double offset,
                                               const DimensionStyle& style = DimensionStyle(),
                                               const ToleranceSpec& tolerance = ToleranceSpec(),
                                               const QString& explicitText = QString());

QVector<DimensionVisualGeometry> makeBaselineDimensions(const QVector<QPointF>& featurePoints,
                                                        const QPointF& origin,
                                                        double firstOffset,
                                                        double spacing,
                                                        bool horizontal,
                                                        const DimensionStyle& style = DimensionStyle());
QVector<DimensionVisualGeometry> makeContinuedDimensions(const QVector<QPointF>& orderedPoints,
                                                         double offset,
                                                         bool horizontal,
                                                         const DimensionStyle& style = DimensionStyle());
DimensionVisualGeometry rebuildAssociativeDimension(const DimensionDefinition& definition);

LeaderGeometry makeLeader(const QVector<QPointF>& vertices, const QString& text,
                          const LeaderStyle& style = LeaderStyle());
TableGeometry makeSimpleTable(const QPointF& topLeft, const QVector<QStringList>& rows,
                              const TableStyle& style = TableStyle());

QVector<AnnotationSegment> makeCenterMark(const QPointF& center, double radius,
                                          const DimensionStyle& style = DimensionStyle());
QVector<AnnotationSegment> makeRevisionCloud(const QRectF& bounds, double arcRadius,
                                             int lobesHint = 24);

} // namespace CadAnnotation
