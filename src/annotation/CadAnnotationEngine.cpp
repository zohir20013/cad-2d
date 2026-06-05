#include "annotation/CadAnnotationEngine.h"

#include "geometry/GeometryKernel.h"
#include "geometry/GeometryKernelAdvanced.h"

#include <QLineF>
#include <QSizeF>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace CadAnnotation {
namespace {

constexpr double kTiny = 1.0e-9;
constexpr double kPi = 3.141592653589793238462643383279502884;

QPointF add(const QPointF& a, const QPointF& b) { return QPointF(a.x() + b.x(), a.y() + b.y()); }
QPointF sub(const QPointF& a, const QPointF& b) { return QPointF(a.x() - b.x(), a.y() - b.y()); }
QPointF mul(const QPointF& a, double s) { return QPointF(a.x() * s, a.y() * s); }
QPointF divSafe(const QPointF& a, double s) { return std::abs(s) <= kTiny ? QPointF() : QPointF(a.x() / s, a.y() / s); }

double len(const QPointF& v) { return std::hypot(v.x(), v.y()); }
QPointF unit(const QPointF& v)
{
    const double l = len(v);
    return l <= kTiny ? QPointF(1.0, 0.0) : divSafe(v, l);
}
QPointF leftNormal(const QPointF& v)
{
    const QPointF u = unit(v);
    return QPointF(-u.y(), u.x());
}

double dot2(const QPointF& a, const QPointF& b) { return a.x() * b.x() + a.y() * b.y(); }

double angleDegOf(const QPointF& v) { return qRadiansToDegrees(std::atan2(v.y(), v.x())); }

QPointF rotateVector(const QPointF& v, double angleDeg)
{
    const double r = qDegreesToRadians(angleDeg);
    const double c = std::cos(r);
    const double s = std::sin(r);
    return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
}

QPointF midpoint(const QPointF& a, const QPointF& b) { return QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5); }

QString decimalString(double value, int precision, bool suppressZeros, bool decimalComma)
{
    QString text = QString::number(value, 'f', std::max(0, precision));
    if (suppressZeros && text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    if (decimalComma) {
        text.replace(QLatin1Char('.'), QLatin1Char(','));
    }
    if (text == QLatin1String("-0")) {
        text = QStringLiteral("0");
    }
    return text;
}

QString dmsString(double valueDeg, int precision)
{
    const double sign = valueDeg < 0.0 ? -1.0 : 1.0;
    double v = std::abs(valueDeg);
    int degrees = static_cast<int>(std::floor(v));
    v = (v - degrees) * 60.0;
    int minutes = static_cast<int>(std::floor(v));
    double seconds = (v - minutes) * 60.0;
    if (sign < 0.0) {
        degrees = -degrees;
    }
    return QStringLiteral("%1°%2'%3\"")
        .arg(degrees)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(decimalString(seconds, precision, true, false));
}

void appendIfValid(DimensionVisualGeometry& visual, const ArrowheadGeometry& arrow)
{
    if (arrow.valid) {
        visual.arrows.push_back(arrow);
    }
}

AnnotationTextBlock makeTextBlock(const QString& text, const QPointF& position, double rotationDeg,
                                  const TextStyle& style, TextAttachment attachment = TextAttachment::Center)
{
    AnnotationTextBlock block;
    block.text = text;
    block.position = position;
    block.rotationDeg = rotationDeg;
    block.estimatedBox = estimateTextBox(text, style, rotationDeg);
    block.estimatedBox.moveCenter(position);
    block.attachment = attachment;
    return block;
}

QString decorateMeasurement(const QString& base, const DimensionStyle& style, const QString& explicitText)
{
    const QString generated = style.prefix + base + style.suffix;
    return applyDimensionTextOverride(generated, explicitText);
}

void addLinearTextAndArrows(DimensionVisualGeometry& visual, const QPointF& a, const QPointF& b,
                            const QString& text, const QPointF& textPoint, const QPointF& lineDir,
                            const DimensionStyle& style)
{
    const QPointF dir = unit(lineDir);
    visual.textBlocks.push_back(makeTextBlock(text, textPoint, angleDegOf(dir), style.text));
    appendIfValid(visual, makeArrowhead(a, mul(dir, -1.0), style));
    appendIfValid(visual, makeArrowhead(b, dir, style));
}

DimensionVisualGeometry fromLineGeometry(const CadGeometry::DimensionLineGeometry& g,
                                         DimensionKind kind,
                                         const QPointF& measureA,
                                         const QPointF& measureB,
                                         const DimensionStyle& style,
                                         const ToleranceSpec& tolerance,
                                         const QString& explicitText)
{
    DimensionVisualGeometry visual;
    visual.valid = g.valid;
    visual.kind = kind;
    visual.measurement = g.measurement;
    if (!g.valid) {
        visual.warnings.push_back(QStringLiteral("Invalid or degenerate dimension input."));
        return visual;
    }

    visual.extensionLines.push_back({g.extensionA0, g.extensionA1});
    visual.extensionLines.push_back({g.extensionB0, g.extensionB1});
    visual.dimensionLines.push_back({g.dimensionA, g.dimensionB});
    visual.gripPoints = {measureA, measureB, g.dimensionA, g.dimensionB, g.textPosition};
    visual.primaryText = decorateMeasurement(formatLength(g.measurement, style, tolerance), style, explicitText);
    addLinearTextAndArrows(visual, g.dimensionA, g.dimensionB, visual.primaryText, g.textPosition,
                           sub(g.dimensionB, g.dimensionA), style);
    return visual;
}

} // namespace

QString unitSuffix(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::Millimeter: return QStringLiteral(" mm");
    case LengthUnit::Centimeter: return QStringLiteral(" cm");
    case LengthUnit::Meter: return QStringLiteral(" m");
    case LengthUnit::Inch: return QStringLiteral(" in");
    case LengthUnit::Foot: return QStringLiteral(" ft");
    case LengthUnit::Unitless: break;
    }
    return QString();
}

QString formatLength(double value, const DimensionStyle& style, const ToleranceSpec& tolerance)
{
    QString core = decimalString(value, style.linearPrecision, style.suppressTrailingZeros, style.useDecimalComma);
    if (style.showUnitSuffix) {
        core += unitSuffix(style.lengthUnit);
    }

    const int precision = tolerance.precision >= 0 ? tolerance.precision : style.linearPrecision;
    auto fmt = [&](double v) {
        QString s = decimalString(v, precision, style.suppressTrailingZeros, style.useDecimalComma);
        if (style.showUnitSuffix) {
            s += unitSuffix(style.lengthUnit);
        }
        return s;
    };

    switch (tolerance.mode) {
    case ToleranceMode::Symmetric:
        return core + QStringLiteral(" ±") + fmt(std::abs(tolerance.upper));
    case ToleranceMode::Deviation:
        return core + QStringLiteral(" +") + fmt(std::abs(tolerance.upper)) + QStringLiteral(" -") + fmt(std::abs(tolerance.lower));
    case ToleranceMode::Limits:
        return fmt(value + tolerance.upper) + QStringLiteral(" / ") + fmt(value + tolerance.lower);
    case ToleranceMode::Basic:
        return QStringLiteral("[") + core + QStringLiteral("]");
    case ToleranceMode::None:
        break;
    }
    return core;
}

QString formatAngle(double valueDeg, const DimensionStyle& style, const ToleranceSpec& tolerance)
{
    QString core;
    switch (style.angleUnit) {
    case AngleUnit::Radians:
        core = decimalString(qDegreesToRadians(valueDeg), style.angularPrecision,
                             style.suppressTrailingZeros, style.useDecimalComma) + QStringLiteral(" rad");
        break;
    case AngleUnit::DegreesMinutesSeconds:
        core = dmsString(valueDeg, style.angularPrecision);
        break;
    case AngleUnit::DecimalDegrees:
    default:
        core = decimalString(valueDeg, style.angularPrecision,
                             style.suppressTrailingZeros, style.useDecimalComma) + QStringLiteral("°");
        break;
    }

    if (tolerance.mode == ToleranceMode::None) {
        return core;
    }

    const int precision = tolerance.precision >= 0 ? tolerance.precision : style.angularPrecision;
    const QString up = decimalString(std::abs(tolerance.upper), precision, style.suppressTrailingZeros, style.useDecimalComma) + QStringLiteral("°");
    const QString low = decimalString(std::abs(tolerance.lower), precision, style.suppressTrailingZeros, style.useDecimalComma) + QStringLiteral("°");
    switch (tolerance.mode) {
    case ToleranceMode::Symmetric: return core + QStringLiteral(" ±") + up;
    case ToleranceMode::Deviation: return core + QStringLiteral(" +") + up + QStringLiteral(" -") + low;
    case ToleranceMode::Limits:
        return decimalString(valueDeg + tolerance.upper, precision, style.suppressTrailingZeros, style.useDecimalComma)
            + QStringLiteral("° / ")
            + decimalString(valueDeg + tolerance.lower, precision, style.suppressTrailingZeros, style.useDecimalComma)
            + QStringLiteral("°");
    case ToleranceMode::Basic: return QStringLiteral("[") + core + QStringLiteral("]");
    case ToleranceMode::None: break;
    }
    return core;
}

QString applyDimensionTextOverride(const QString& generatedMeasurement, const QString& explicitText)
{
    if (explicitText.isEmpty()) {
        return generatedMeasurement;
    }
    QString text = explicitText;
    if (text.contains(QStringLiteral("<>"))) {
        text.replace(QStringLiteral("<>"), generatedMeasurement);
        return text;
    }
    return text;
}

QRectF estimateTextBox(const QString& text, const TextStyle& style, double rotationDeg)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    int maxCharacters = 0;
    for (const QString& line : lines) {
        maxCharacters = std::max(maxCharacters, static_cast<int>(line.size()));
    }
    const double width = std::max(1, maxCharacters) * style.height * 0.62 * style.widthFactor;
    const double height = std::max(1, static_cast<int>(lines.size())) * style.height * 1.25;
    const double r = qDegreesToRadians(rotationDeg);
    const double c = std::abs(std::cos(r));
    const double s = std::abs(std::sin(r));
    const double rotatedW = width * c + height * s;
    const double rotatedH = width * s + height * c;
    return QRectF(-rotatedW * 0.5, -rotatedH * 0.5, rotatedW, rotatedH);
}

QStringList wrapTextByWidth(const QString& text, double maxWidth, const TextStyle& style)
{
    if (maxWidth <= style.height) {
        return QStringList{text};
    }
    const int maxChars = std::max(1, static_cast<int>(std::floor(maxWidth / std::max(style.height * 0.62 * style.widthFactor, 0.1))));
    QStringList output;
    QString line;
    for (const QString& word : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!line.isEmpty() && line.size() + 1 + word.size() > maxChars) {
            output.push_back(line);
            line.clear();
        }
        if (!line.isEmpty()) {
            line += QLatin1Char(' ');
        }
        line += word;
    }
    if (!line.isEmpty()) {
        output.push_back(line);
    }
    if (output.isEmpty()) {
        output.push_back(text);
    }
    return output;
}

ArrowheadGeometry makeArrowhead(const QPointF& tip, const QPointF& directionAwayFromTip,
                                const DimensionStyle& style)
{
    return makeArrowhead(tip, directionAwayFromTip, style.arrowhead, style.arrowSize * style.scale);
}

ArrowheadGeometry makeArrowhead(const QPointF& tip, const QPointF& directionAwayFromTip,
                                ArrowheadType type, double size)
{
    ArrowheadGeometry arrow;
    arrow.type = type;
    arrow.tip = tip;
    arrow.rotationDeg = angleDegOf(directionAwayFromTip);
    if (type == ArrowheadType::None || size <= kTiny) {
        return arrow;
    }

    const QPointF dir = unit(directionAwayFromTip);
    const QPointF normal = leftNormal(dir);
    arrow.tailCenter = add(tip, mul(dir, size));
    arrow.valid = true;
    arrow.filled = type == ArrowheadType::ClosedFilled || type == ArrowheadType::Dot;

    switch (type) {
    case ArrowheadType::ClosedFilled:
    case ArrowheadType::ClosedBlank:
        arrow.outline = {tip, add(arrow.tailCenter, mul(normal, size * 0.32)), add(arrow.tailCenter, mul(normal, -size * 0.32))};
        break;
    case ArrowheadType::Open:
        arrow.outline = {add(arrow.tailCenter, mul(normal, size * 0.32)), tip,
                         add(arrow.tailCenter, mul(normal, -size * 0.32))};
        arrow.filled = false;
        break;
    case ArrowheadType::ArchitecturalTick:
        arrow.outline = {add(tip, add(mul(dir, -size * 0.35), mul(normal, -size * 0.55))),
                         add(tip, add(mul(dir, size * 0.35), mul(normal, size * 0.55)))};
        arrow.filled = false;
        break;
    case ArrowheadType::Dot: {
        const int segments = 16;
        for (int i = 0; i < segments; ++i) {
            const double a = 2.0 * kPi * static_cast<double>(i) / segments;
            arrow.outline.push_back(add(tip, QPointF(std::cos(a) * size * 0.32, std::sin(a) * size * 0.32)));
        }
        break;
    }
    case ArrowheadType::None:
        arrow.valid = false;
        break;
    }
    return arrow;
}

DimensionVisualGeometry makeAlignedDimension(const QPointF& a, const QPointF& b, double offset,
                                             const DimensionStyle& style,
                                             const ToleranceSpec& tolerance,
                                             const QString& explicitText)
{
    const auto g = CadGeometry::makeAlignedDimensionGeometry(a, b, offset, style.extensionBeyondDimension * style.scale);
    return fromLineGeometry(g, DimensionKind::Aligned, a, b, style, tolerance, explicitText);
}

DimensionVisualGeometry makeLinearDimension(const QPointF& a, const QPointF& b, double offset,
                                            bool horizontal,
                                            const DimensionStyle& style,
                                            const ToleranceSpec& tolerance,
                                            const QString& explicitText)
{
    const auto g = CadGeometry::makeLinearDimensionGeometry(a, b, offset, horizontal, style.extensionBeyondDimension * style.scale);
    return fromLineGeometry(g, horizontal ? DimensionKind::LinearHorizontal : DimensionKind::LinearVertical,
                            a, b, style, tolerance, explicitText);
}

DimensionVisualGeometry makeRotatedDimension(const QPointF& a, const QPointF& b, const QPointF& dimensionPoint,
                                             double rotationDeg,
                                             const DimensionStyle& style,
                                             const ToleranceSpec& tolerance,
                                             const QString& explicitText)
{
    DimensionVisualGeometry visual;
    visual.kind = DimensionKind::Rotated;

    const QPointF axis = rotateVector(QPointF(1.0, 0.0), rotationDeg);
    const QPointF normal = leftNormal(axis);
    const double distanceA = dot2(sub(a, dimensionPoint), normal);
    const double distanceB = dot2(sub(b, dimensionPoint), normal);
    const QPointF da = sub(a, mul(normal, distanceA));
    const QPointF db = sub(b, mul(normal, distanceB));
    const QPointF dimA = add(da, mul(normal, dot2(sub(dimensionPoint, da), normal)));
    const QPointF dimB = add(db, mul(normal, dot2(sub(dimensionPoint, db), normal)));

    const double measurement = std::abs(dot2(sub(b, a), axis));
    visual.valid = measurement > kTiny;
    if (!visual.valid) {
        visual.warnings.push_back(QStringLiteral("Rotated dimension has zero projected length."));
        return visual;
    }

    visual.measurement = measurement;
    visual.primaryText = decorateMeasurement(formatLength(measurement, style, tolerance), style, explicitText);
    visual.extensionLines = {{a, dimA}, {b, dimB}};
    visual.dimensionLines = {{dimA, dimB}};
    visual.gripPoints = {a, b, dimensionPoint, dimA, dimB};
    const QPointF textPosition = midpoint(dimA, dimB);
    addLinearTextAndArrows(visual, dimA, dimB, visual.primaryText, add(textPosition, mul(normal, style.textGap * style.scale)), axis, style);
    return visual;
}

DimensionVisualGeometry makeAngularDimension(const QPointF& center, const QPointF& armA, const QPointF& armB,
                                             double radius,
                                             const DimensionStyle& style,
                                             const ToleranceSpec& tolerance,
                                             bool clockwise,
                                             const QString& explicitText)
{
    const auto g = CadGeometry::makeAngularDimensionGeometry(center, armA, armB, radius, clockwise);
    DimensionVisualGeometry visual;
    visual.valid = g.valid;
    visual.kind = DimensionKind::Angular;
    if (!g.valid) {
        visual.warnings.push_back(QStringLiteral("Invalid angular dimension input."));
        return visual;
    }
    visual.measurement = g.measurementDeg;
    visual.measurementAngleDeg = g.spanAngleDeg;
    visual.arcs.push_back({g.center, radius, g.startAngleDeg, g.spanAngleDeg});
    visual.constructionLines.push_back({center, armA});
    visual.constructionLines.push_back({center, armB});
    visual.gripPoints = {center, armA, armB, g.arcStart, g.arcEnd, g.textPosition};
    visual.primaryText = decorateMeasurement(formatAngle(g.measurementDeg, style, tolerance), style, explicitText);
    visual.textBlocks.push_back(makeTextBlock(visual.primaryText, g.textPosition, 0.0, style.text));
    appendIfValid(visual, makeArrowhead(g.arcStart, sub(g.arcStart, g.arcEnd), style));
    appendIfValid(visual, makeArrowhead(g.arcEnd, sub(g.arcEnd, g.arcStart), style));
    return visual;
}

DimensionVisualGeometry makeRadiusDimension(const QPointF& center, const QPointF& pointOnCircle,
                                            const DimensionStyle& style,
                                            const ToleranceSpec& tolerance,
                                            const QString& explicitText)
{
    const auto g = CadGeometry::makeRadiusDimensionGeometry(center, pointOnCircle, style.textGap * style.scale);
    DimensionVisualGeometry visual = fromLineGeometry(g, DimensionKind::Radius, center, pointOnCircle, style, tolerance,
                                                      explicitText.isEmpty() ? QStringLiteral("R<>") : explicitText);
    return visual;
}

DimensionVisualGeometry makeDiameterDimension(const QPointF& center, const QPointF& pointOnCircle,
                                              const DimensionStyle& style,
                                              const ToleranceSpec& tolerance,
                                              const QString& explicitText)
{
    DimensionVisualGeometry visual;
    visual.kind = DimensionKind::Diameter;
    const double radius = len(sub(pointOnCircle, center));
    if (radius <= kTiny) {
        visual.warnings.push_back(QStringLiteral("Invalid diameter dimension radius."));
        return visual;
    }
    const QPointF dir = unit(sub(pointOnCircle, center));
    const QPointF opposite = sub(center, mul(dir, radius));
    visual.valid = true;
    visual.measurement = radius * 2.0;
    visual.dimensionLines.push_back({opposite, pointOnCircle});
    visual.gripPoints = {center, opposite, pointOnCircle};
    visual.primaryText = decorateMeasurement(formatLength(radius * 2.0, style, tolerance), style,
                                             explicitText.isEmpty() ? QStringLiteral("Ø<>") : explicitText);
    const QPointF textPosition = add(center, mul(leftNormal(dir), style.textGap * style.scale));
    addLinearTextAndArrows(visual, opposite, pointOnCircle, visual.primaryText, textPosition, dir, style);
    return visual;
}

DimensionVisualGeometry makeOrdinateDimension(const QPointF& origin, const QPointF& featurePoint,
                                              const QPointF& leaderEnd, bool xOrdinate,
                                              const DimensionStyle& style,
                                              const ToleranceSpec& tolerance,
                                              const QString& explicitText)
{
    DimensionVisualGeometry visual;
    visual.valid = true;
    visual.kind = xOrdinate ? DimensionKind::OrdinateX : DimensionKind::OrdinateY;
    const double measurement = xOrdinate ? featurePoint.x() - origin.x() : featurePoint.y() - origin.y();
    visual.measurement = measurement;
    visual.extensionLines.push_back({featurePoint, leaderEnd});
    const QPointF landingEnd = add(leaderEnd, QPointF((leaderEnd.x() >= featurePoint.x() ? 1.0 : -1.0) * 8.0 * style.scale, 0.0));
    visual.dimensionLines.push_back({leaderEnd, landingEnd});
    visual.gripPoints = {origin, featurePoint, leaderEnd, landingEnd};
    visual.primaryText = decorateMeasurement(formatLength(measurement, style, tolerance), style, explicitText);
    visual.textBlocks.push_back(makeTextBlock(visual.primaryText, add(landingEnd, QPointF(style.textGap * style.scale, 0.0)), 0.0, style.text, TextAttachment::Left));
    return visual;
}

DimensionVisualGeometry makeArcLengthDimension(const QPointF& center, double radius,
                                               double startAngleDeg, double spanAngleDeg,
                                               double offset,
                                               const DimensionStyle& style,
                                               const ToleranceSpec& tolerance,
                                               const QString& explicitText)
{
    DimensionVisualGeometry visual;
    visual.kind = DimensionKind::ArcLength;
    if (radius <= kTiny || std::abs(spanAngleDeg) <= kTiny) {
        visual.warnings.push_back(QStringLiteral("Invalid arc length dimension input."));
        return visual;
    }
    const double dimRadius = radius + offset;
    visual.valid = true;
    visual.measurement = qDegreesToRadians(std::abs(spanAngleDeg)) * radius;
    visual.arcs.push_back({center, dimRadius, startAngleDeg, spanAngleDeg});
    const QPointF a = CadGeometry::pointOnCadCircle(center, radius, startAngleDeg);
    const QPointF b = CadGeometry::pointOnCadCircle(center, radius, startAngleDeg + spanAngleDeg);
    const QPointF da = CadGeometry::pointOnCadCircle(center, dimRadius, startAngleDeg);
    const QPointF db = CadGeometry::pointOnCadCircle(center, dimRadius, startAngleDeg + spanAngleDeg);
    visual.extensionLines.push_back({a, da});
    visual.extensionLines.push_back({b, db});
    const double midAng = startAngleDeg + spanAngleDeg * 0.5;
    const QPointF textPoint = CadGeometry::pointOnCadCircle(center, dimRadius + style.textGap * style.scale, midAng);
    visual.primaryText = decorateMeasurement(formatLength(visual.measurement, style, tolerance), style,
                                             explicitText.isEmpty() ? QStringLiteral("⌒<>") : explicitText);
    visual.textBlocks.push_back(makeTextBlock(visual.primaryText, textPoint, midAng + 90.0, style.text));
    visual.gripPoints = {center, a, b, da, db, textPoint};
    appendIfValid(visual, makeArrowhead(da, sub(da, db), style));
    appendIfValid(visual, makeArrowhead(db, sub(db, da), style));
    return visual;
}

QVector<DimensionVisualGeometry> makeBaselineDimensions(const QVector<QPointF>& featurePoints,
                                                        const QPointF& origin,
                                                        double firstOffset,
                                                        double spacing,
                                                        bool horizontal,
                                                        const DimensionStyle& style)
{
    QVector<DimensionVisualGeometry> result;
    for (int i = 0; i < featurePoints.size(); ++i) {
        result.push_back(makeLinearDimension(origin, featurePoints.at(i), firstOffset + spacing * i, horizontal, style));
        result.last().kind = DimensionKind::Baseline;
    }
    return result;
}

QVector<DimensionVisualGeometry> makeContinuedDimensions(const QVector<QPointF>& orderedPoints,
                                                         double offset,
                                                         bool horizontal,
                                                         const DimensionStyle& style)
{
    QVector<DimensionVisualGeometry> result;
    if (orderedPoints.size() < 2) {
        return result;
    }
    for (int i = 0; i + 1 < orderedPoints.size(); ++i) {
        result.push_back(makeLinearDimension(orderedPoints.at(i), orderedPoints.at(i + 1), offset, horizontal, style));
        result.last().kind = DimensionKind::Continue;
    }
    return result;
}

DimensionVisualGeometry rebuildAssociativeDimension(const DimensionDefinition& definition)
{
    const auto& r = definition.reference;
    switch (definition.kind) {
    case DimensionKind::Aligned:
        return makeAlignedDimension(r.referencePointA, r.referencePointB, definition.offset,
                                    definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::LinearHorizontal:
        return makeLinearDimension(r.referencePointA, r.referencePointB, definition.offset, true,
                                   definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::LinearVertical:
        return makeLinearDimension(r.referencePointA, r.referencePointB, definition.offset, false,
                                   definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::Rotated:
        return makeRotatedDimension(r.referencePointA, r.referencePointB, definition.definitionPoint,
                                    definition.rotationDeg, definition.style, definition.tolerance,
                                    definition.explicitText);
    case DimensionKind::Angular:
        return makeAngularDimension(r.center, r.referencePointA, r.referencePointB,
                                    std::max(definition.offset, r.radius), definition.style,
                                    definition.tolerance, false, definition.explicitText);
    case DimensionKind::Radius:
        return makeRadiusDimension(r.center, r.referencePointA, definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::Diameter:
        return makeDiameterDimension(r.center, r.referencePointA, definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::OrdinateX:
        return makeOrdinateDimension(r.center, r.referencePointA, definition.definitionPoint, true,
                                     definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::OrdinateY:
        return makeOrdinateDimension(r.center, r.referencePointA, definition.definitionPoint, false,
                                     definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::ArcLength:
        return makeArcLengthDimension(r.center, r.radius, r.startAngleDeg, r.spanAngleDeg, definition.offset,
                                      definition.style, definition.tolerance, definition.explicitText);
    case DimensionKind::Baseline:
    case DimensionKind::Continue:
        break;
    }
    DimensionVisualGeometry invalid;
    invalid.warnings.push_back(QStringLiteral("Unsupported associative dimension definition."));
    return invalid;
}

LeaderGeometry makeLeader(const QVector<QPointF>& vertices, const QString& text,
                          const LeaderStyle& style)
{
    LeaderGeometry leader;
    if (vertices.size() < 2) {
        return leader;
    }
    leader.valid = true;
    leader.gripPoints = vertices;
    for (int i = 0; i + 1 < vertices.size(); ++i) {
        leader.segments.push_back({vertices.at(i), vertices.at(i + 1)});
    }
    const QPointF firstDirection = sub(vertices.at(1), vertices.at(0));
    leader.arrows.push_back(makeArrowhead(vertices.first(), firstDirection, style.arrowhead, style.arrowSize * style.scale));

    const QPointF last = vertices.last();
    QPointF textPos = last;
    if (vertices.size() >= 2) {
        const QPointF lastDir = unit(sub(vertices.last(), vertices.at(vertices.size() - 2)));
        textPos = add(last, mul(lastDir, style.textGap * style.scale));
        if (style.underline) {
            leader.segments.push_back({last, add(last, mul(lastDir, style.landingLength * style.scale))});
        }
    }
    leader.text = makeTextBlock(text, textPos, 0.0, style.text, TextAttachment::Left);
    return leader;
}

TableGeometry makeSimpleTable(const QPointF& topLeft, const QVector<QStringList>& rows,
                              const TableStyle& style)
{
    TableGeometry table;
    if (rows.isEmpty()) {
        return table;
    }
    int columns = 0;
    for (const QStringList& row : rows) {
        columns = std::max(columns, static_cast<int>(row.size()));
    }
    if (columns <= 0) {
        return table;
    }
    const double rowH = style.rowHeight * style.scale;
    const double colW = style.columnWidth * style.scale;
    const int rowCount = rows.size();
    table.valid = true;
    table.outerBox = QRectF(topLeft, QSizeF(colW * columns, rowH * rowCount));

    for (int c = 0; c <= columns; ++c) {
        const double x = topLeft.x() + c * colW;
        table.gridLines.push_back({QPointF(x, topLeft.y()), QPointF(x, topLeft.y() + rowH * rowCount)});
    }
    for (int r = 0; r <= rowCount; ++r) {
        const double y = topLeft.y() + r * rowH;
        table.gridLines.push_back({QPointF(topLeft.x(), y), QPointF(topLeft.x() + colW * columns, y)});
    }
    for (int r = 0; r < rowCount; ++r) {
        for (int c = 0; c < columns; ++c) {
            TableCellGeometry cell;
            cell.row = r;
            cell.column = c;
            cell.box = QRectF(topLeft.x() + c * colW, topLeft.y() + r * rowH, colW, rowH);
            const QString text = c < static_cast<int>(rows.at(r).size()) ? rows.at(r).at(c) : QString();
            cell.text = makeTextBlock(text, cell.box.center(), 0.0, style.text);
            table.cells.push_back(cell);
        }
    }
    return table;
}

QVector<AnnotationSegment> makeCenterMark(const QPointF& center, double radius,
                                          const DimensionStyle& style)
{
    QVector<AnnotationSegment> segments;
    const double size = std::max(style.centerMarkSize * style.scale, radius * 0.12);
    segments.push_back({QPointF(center.x() - size, center.y()), QPointF(center.x() + size, center.y())});
    segments.push_back({QPointF(center.x(), center.y() - size), QPointF(center.x(), center.y() + size)});
    return segments;
}

QVector<AnnotationSegment> makeRevisionCloud(const QRectF& bounds, double arcRadius, int lobesHint)
{
    QVector<AnnotationSegment> segments;
    if (!bounds.isValid() || arcRadius <= kTiny) {
        return segments;
    }
    const int lobes = std::max(8, lobesHint);
    QVector<QPointF> points;
    for (int i = 0; i < lobes; ++i) {
        const double t = static_cast<double>(i) / lobes;
        double x = 0.0;
        double y = 0.0;
        if (t < 0.25) {
            x = bounds.left() + bounds.width() * (t / 0.25);
            y = bounds.top();
        } else if (t < 0.5) {
            x = bounds.right();
            y = bounds.top() + bounds.height() * ((t - 0.25) / 0.25);
        } else if (t < 0.75) {
            x = bounds.right() - bounds.width() * ((t - 0.5) / 0.25);
            y = bounds.bottom();
        } else {
            x = bounds.left();
            y = bounds.bottom() - bounds.height() * ((t - 0.75) / 0.25);
        }
        const double wave = std::sin(t * 2.0 * kPi * lobes) * arcRadius * 0.25;
        const QPointF towardCenter = unit(sub(bounds.center(), QPointF(x, y)));
        points.push_back(add(QPointF(x, y), mul(towardCenter, -wave)));
    }
    for (int i = 0; i < points.size(); ++i) {
        segments.push_back({points.at(i), points.at((i + 1) % points.size())});
    }
    return segments;
}

} // namespace CadAnnotation
