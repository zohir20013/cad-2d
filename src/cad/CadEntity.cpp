#include "CadEntity.h"
#include "CadRenderStyle.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsItemGroup>
#include <QGraphicsSimpleTextItem>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsRectItem>
#include <QJsonArray>
#include <QLineF>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QTransform>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
QJsonArray pointToJson(const QPointF& p)
{
    QJsonArray a;
    a.append(p.x());
    a.append(p.y());
    return a;
}

QPointF pointFromJson(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 2) return {};
    return QPointF(a.at(0).toDouble(), a.at(1).toDouble());
}

QJsonArray pointsToJson(const QVector<QPointF>& points)
{
    QJsonArray a;
    for (const QPointF& p : points) a.append(pointToJson(p));
    return a;
}

QVector<QPointF> pointsFromJson(const QJsonValue& v)
{
    QVector<QPointF> points;
    const QJsonArray a = v.toArray();
    for (const QJsonValue& value : a) points.append(pointFromJson(value));
    return points;
}

QJsonArray rectToJson(const QRectF& r)
{
    QJsonArray a;
    a.append(r.x());
    a.append(r.y());
    a.append(r.width());
    a.append(r.height());
    return a;
}

QRectF rectFromJson(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 4) return {};
    return QRectF(a.at(0).toDouble(), a.at(1).toDouble(),
                  a.at(2).toDouble(), a.at(3).toDouble()).normalized();
}

QPainterPath polylinePath(const QVector<QPointF>& points, bool closed)
{
    QPainterPath path;
    if (points.isEmpty()) return path;
    path.moveTo(points.first());
    for (int i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
    if (closed && points.size() > 2) path.closeSubpath();
    return path;
}

double medianSegmentLength(QVector<double> values)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !qIsFinite(v) || v <= 1.0e-7; }), values.end());
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

bool hatchLoopCanBeClosedSafely(const QVector<QPointF>& loop)
{
    if (loop.size() < 3) return false;
    QVector<double> segments;
    segments.reserve(loop.size());
    double maxGap = 0.0;
    for (int i = 0; i < loop.size(); ++i) {
        const QPointF& a = loop.at(i);
        const QPointF& b = loop.at((i + 1) % loop.size());
        if (!qIsFinite(a.x()) || !qIsFinite(a.y()) || !qIsFinite(b.x()) || !qIsFinite(b.y())) return false;
        const double d = QLineF(a, b).length();
        segments.append(d);
        maxGap = std::max(maxGap, d);
    }
    const double typical = medianSegmentLength(segments);
    if (typical <= 1.0e-9) return false;
    const double closingGap = QLineF(loop.last(), loop.first()).length();
    if (maxGap > std::max(40.0, typical * 8.0)) return false;
    if (closingGap > std::max(25.0, typical * 5.0)) return false;
    return true;
}


QString normalizedHatchPatternName(const QString& pattern)
{
    QString p = pattern.trimmed().toUpper();
    p.replace('-', '_');
    p.replace(' ', '_');
    if (p.isEmpty()) p = QStringLiteral("ANSI31");
    return p;
}

bool hatchPatternIsSolid(const QString& pattern)
{
    const QString p = normalizedHatchPatternName(pattern);
    return p == QStringLiteral("SOLID")
        || p == QStringLiteral("PLEIN")
        || p == QStringLiteral("REMPLISSAGE")
        || p.startsWith(QStringLiteral("SOLID_"))
        || p.startsWith(QStringLiteral("SOLID"));
}

int solidHatchAlpha(const QString& pattern)
{
    const QString p = normalizedHatchPatternName(pattern);
    if (p == QStringLiteral("SOLID_TRANSPARENT")) return 35;
    if (p == QStringLiteral("SOLID_LIGHT")) return 55;
    if (p == QStringLiteral("SOLID_MEDIUM")) return 128;
    if (p == QStringLiteral("SOLID_DARK")) return 215;
    if (p.startsWith(QStringLiteral("SOLID_"))) {
        bool ok = false;
        const int percent = p.mid(6).toInt(&ok);
        if (ok) return qBound(12, qRound(255.0 * double(percent) / 100.0), 242);
    }
    return 135;
}

void drawHatchLine(QPainter& painter, double x1, double y1, double x2, double y2)
{
    painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
}

void drawHatchDot(QPainter& painter, double x, double y, double r)
{
    painter.drawEllipse(QPointF(x, y), r, r);
}

QBrush materialHatchBrush(const QString& pattern, QColor color, double scale, double angleDeg)
{
    const QString p = normalizedHatchPatternName(pattern);

    if (hatchPatternIsSolid(p)) {
        color.setAlpha(solidHatchAlpha(p));
        return QBrush(color, Qt::SolidPattern);
    }

    // Built-in Qt patterns are useful for very simple fills, but material hatches
    // need deterministic CAD-like tiles so DXF patterns such as concrete, wood,
    // sand, brick and insulation remain recognizable on screen.
    const double safeScale = qBound(0.10, std::abs(scale), 250.0);
    const int base = qBound(20, int(std::round(36.0 * std::sqrt(qBound(0.25, safeScale, 16.0)))), 180);
    const int tile = qMax(24, base + (base % 2));
    const double step = qMax(4.0, tile / 4.0);

    QPixmap pixmap(tile, tile);
    pixmap.fill(Qt::transparent);

    QColor stroke = color;
    stroke.setAlpha(qBound(70, stroke.alpha(), 230));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(stroke, qBound(1.0, safeScale * 0.45, 2.6));
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    auto drawDiagonal45 = [&]() {
        for (double x = -tile; x <= tile * 2.0; x += step) drawHatchLine(painter, x, tile, x + tile, 0);
    };
    auto drawDiagonal135 = [&]() {
        for (double x = -tile; x <= tile * 2.0; x += step) drawHatchLine(painter, x, 0, x + tile, tile);
    };
    auto drawHorizontal = [&]() {
        for (double y = step * 0.5; y < tile; y += step) drawHatchLine(painter, 0, y, tile, y);
    };
    auto drawVertical = [&]() {
        for (double x = step * 0.5; x < tile; x += step) drawHatchLine(painter, x, 0, x, tile);
    };
    auto drawDots = [&](double spacing, double radius) {
        painter.setBrush(stroke);
        for (double y = spacing * 0.55; y < tile; y += spacing) {
            for (double x = spacing * 0.55; x < tile; x += spacing) {
                const double offset = (int(y / spacing) % 2) ? spacing * 0.35 : 0.0;
                drawHatchDot(painter, std::fmod(x + offset, double(tile)), y, radius);
            }
        }
        painter.setBrush(Qt::NoBrush);
    };

    if (p == QStringLiteral("ANSI31") || p == QStringLiteral("STEEL") || p == QStringLiteral("METAL")) {
        drawDiagonal45();
    } else if (p == QStringLiteral("ANSI32") || p == QStringLiteral("VERTICAL")) {
        drawVertical();
    } else if (p == QStringLiteral("ANSI33") || p == QStringLiteral("HORIZONTAL")) {
        drawHorizontal();
    } else if (p == QStringLiteral("ANSI34") || p == QStringLiteral("CROSS") || p == QStringLiteral("CROSSHATCH")) {
        drawHorizontal(); drawVertical();
    } else if (p == QStringLiteral("ANSI35") || p == QStringLiteral("DIAGONAL_CROSS")) {
        drawDiagonal45(); drawDiagonal135();
    } else if (p == QStringLiteral("ANSI36") || p == QStringLiteral("ZIGZAG")) {
        for (double y = step; y < tile + step; y += step * 1.25) {
            QPainterPath wave;
            wave.moveTo(0, y);
            for (double x = 0; x <= tile; x += step * 0.5) wave.lineTo(x, y + ((int(x / (step * 0.5)) % 2) ? -step * 0.35 : step * 0.35));
            painter.drawPath(wave);
        }
    } else if (p == QStringLiteral("ANSI37") || p == QStringLiteral("GRID")) {
        drawHorizontal(); drawVertical();
        pen.setWidthF(qBound(0.8, safeScale * 0.25, 1.8));
        painter.setPen(pen);
        drawDiagonal45();
    } else if (p == QStringLiteral("ANSI38") || p == QStringLiteral("NET")) {
        drawDiagonal45(); drawDiagonal135();
        pen.setWidthF(qBound(0.8, safeScale * 0.25, 1.7));
        painter.setPen(pen);
        drawHorizontal();
    } else if (p == QStringLiteral("CONCRETE") || p == QStringLiteral("BETON") || p == QStringLiteral("AR_CONC")) {
        drawDots(step * 0.75, qBound(0.65, safeScale * 0.28, 1.7));
        for (int i = 0; i < 7; ++i) {
            const double x = std::fmod(7.0 + i * step * 1.37, double(tile));
            const double y = std::fmod(11.0 + i * step * 0.91, double(tile));
            drawHatchLine(painter, x - step * 0.25, y, x + step * 0.35, y + step * 0.18);
        }
    } else if (p == QStringLiteral("SAND") || p == QStringLiteral("SABLE") || p == QStringLiteral("DOTS")) {
        drawDots(step * 0.55, qBound(0.45, safeScale * 0.20, 1.25));
    } else if (p == QStringLiteral("GRAVEL") || p == QStringLiteral("GRAVIER")) {
        drawDots(step * 0.90, qBound(0.8, safeScale * 0.32, 2.0));
        for (int i = 0; i < 5; ++i) {
            const double x = std::fmod(5.0 + i * step * 1.55, double(tile));
            const double y = std::fmod(3.0 + i * step * 1.10, double(tile));
            painter.drawRect(QRectF(x, y, step * 0.35, step * 0.25));
        }
    } else if (p == QStringLiteral("WOOD") || p == QStringLiteral("BOIS") || p == QStringLiteral("AR_WOOD")) {
        for (double y = step * 0.35; y < tile; y += step * 0.65) {
            QPainterPath grain;
            grain.moveTo(0, y);
            grain.cubicTo(tile * 0.25, y - step * 0.35, tile * 0.55, y + step * 0.35, tile, y);
            painter.drawPath(grain);
        }
        painter.drawEllipse(QRectF(tile * 0.25, tile * 0.25, step * 0.9, step * 0.45));
        painter.drawEllipse(QRectF(tile * 0.62, tile * 0.58, step * 0.7, step * 0.36));
    } else if (p == QStringLiteral("BRICK") || p == QStringLiteral("BRIQUE") || p == QStringLiteral("MASONRY")) {
        const double h = step;
        const double w = step * 2.2;
        for (double y = 0; y <= tile; y += h) drawHatchLine(painter, 0, y, tile, y);
        for (int row = 0; row < int(tile / h) + 2; ++row) {
            const double y0 = row * h;
            const double offset = (row % 2) ? w * 0.5 : 0.0;
            for (double x = offset; x <= tile; x += w) drawHatchLine(painter, x, y0, x, y0 + h);
        }
    } else if (p == QStringLiteral("TILE") || p == QStringLiteral("CARRELAGE")) {
        drawHorizontal(); drawVertical();
    } else if (p == QStringLiteral("EARTH") || p == QStringLiteral("TERRE") || p == QStringLiteral("GROUND")) {
        drawHorizontal();
        for (double x = 0; x < tile; x += step) drawHatchLine(painter, x, step * 1.2, x + step * 0.6, step * 1.8);
    } else if (p == QStringLiteral("INSULATION") || p == QStringLiteral("ISOLATION")) {
        for (double x = -step; x < tile + step; x += step) {
            QPainterPath arc;
            arc.moveTo(x, tile * 0.5);
            arc.cubicTo(x + step * 0.25, 0, x + step * 0.75, tile, x + step, tile * 0.5);
            painter.drawPath(arc);
        }
    } else if (p == QStringLiteral("GLASS") || p == QStringLiteral("VERRE")) {
        drawDiagonal45();
        pen.setWidthF(qBound(0.8, safeScale * 0.20, 1.5));
        painter.setPen(pen);
        for (double y = step; y < tile; y += step * 1.1) drawHatchLine(painter, 0, y, tile, y - step * 0.4);
    } else if (p == QStringLiteral("WATER") || p == QStringLiteral("EAU")) {
        for (double y = step * 0.6; y < tile; y += step * 0.85) {
            QPainterPath wave;
            wave.moveTo(0, y);
            wave.cubicTo(tile * 0.20, y - step * 0.4, tile * 0.30, y + step * 0.4, tile * 0.50, y);
            wave.cubicTo(tile * 0.70, y - step * 0.4, tile * 0.80, y + step * 0.4, tile, y);
            painter.drawPath(wave);
        }
    } else if (p == QStringLiteral("GRASS") || p == QStringLiteral("HERBE")) {
        for (double x = step * 0.3; x < tile; x += step * 0.7) {
            drawHatchLine(painter, x, tile, x - step * 0.2, tile - step * 0.7);
            drawHatchLine(painter, x, tile, x + step * 0.2, tile - step * 0.6);
        }
    } else if (p == QStringLiteral("DENSE") || p == QStringLiteral("DENSE4")) {
        painter.fillRect(QRect(0, 0, tile, tile), QBrush(color, Qt::Dense4Pattern));
    } else {
        drawDiagonal45();
    }

    painter.end();
    QBrush brush(pixmap);
    QTransform transform;
    if (!qFuzzyIsNull(angleDeg)) transform.rotate(angleDeg);
    brush.setTransform(transform);
    return brush;
}

QPolygonF regularPolygon(const QPointF& center, double radius, int sides, double rotationDeg)
{
    QPolygonF polygon;
    const int n = qMax(3, sides);
    const double rotationRad = qDegreesToRadians(rotationDeg);
    for (int i = 0; i < n; ++i) {
        const double a = rotationRad + (2.0 * M_PI * i / n);
        polygon << QPointF(center.x() + radius * std::cos(a),
                           center.y() + radius * std::sin(a));
    }
    return polygon;
}

QPointF rotatePoint(const QPointF& p, const QPointF& center, double angleDeg)
{
    const double a = qDegreesToRadians(angleDeg);
    const double c = std::cos(a);
    const double s = std::sin(a);
    const double x = p.x() - center.x();
    const double y = p.y() - center.y();
    return QPointF(center.x() + x * c - y * s, center.y() + x * s + y * c);
}

QPointF scalePoint(const QPointF& p, const QPointF& center, double factor)
{
    return QPointF(center.x() + (p.x() - center.x()) * factor,
                   center.y() + (p.y() - center.y()) * factor);
}

QPointF mirrorPoint(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) return p;
    const double t = ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2;
    const QPointF proj(a.x() + t * dx, a.y() + t * dy);
    return QPointF(2.0 * proj.x() - p.x(), 2.0 * proj.y() - p.y());
}



QRectF boundingRectFromPoints(const QVector<QPointF>& points)
{
    if (points.isEmpty()) return {};
    double minX = points.first().x();
    double maxX = minX;
    double minY = points.first().y();
    double maxY = minY;
    for (const QPointF& p : points) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

QVector<QPointF> rectCorners(const QRectF& r)
{
    return {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
}
}

QPen CadEntity::buildPen() const
{
    QPen pen(CadRenderStyle::deepCadColor(m_color));
    pen.setCosmetic(true);
    if (m_lineWeight > 0.0) pen.setWidthF(qBound(1.2, m_lineWeight, 6.0));
    else pen.setWidthF(1.65);

    const QString lt = m_lineType.toLower().trimmed();
    // Only explicit UI-created linetypes are drawn dashed. Imported DWG linetypes
    // are normalized to Continuous by the DWG importer unless
    // DWGVIEWER_KEEP_DWG_LINETYPES=1 is set.
    if (lt == "dashed" || lt == "tirete" || lt == "tiret") {
        pen.setStyle(Qt::DashLine);
    } else if (lt == "dotted" || lt == "pointille" || lt == "dotted") {
        pen.setStyle(Qt::DotLine);
    } else if (lt == "dashdot" || lt == "mixte") {
        pen.setStyle(Qt::DashDotLine);
    } else {
        pen.setStyle(Qt::SolidLine);
    }

    return pen;
}

void CadEntity::writeCommonJson(QJsonObject& obj) const
{
    obj["layer"] = m_layer;
    obj["color"] = m_color.name(QColor::HexArgb);
    obj["lineWeight"] = m_lineWeight;
    obj["lineType"] = m_lineType;
}

void CadEntity::readCommonJson(const QJsonObject& obj)
{
    m_layer = obj.value("layer").toString("0");
    m_color = QColor(obj.value("color").toString("#ffffffff"));
    if (!m_color.isValid()) m_color = Qt::white;
    m_lineWeight = obj.value("lineWeight").toDouble(0.0);
    m_lineType = obj.value("lineType").toString("Continuous");
}

void CadEntity::copyStyleTo(CadEntity& target) const
{
    target.setLayer(layer());
    target.setColor(color());
    target.setLineWeight(lineWeight());
    target.setLineType(lineType());
}

std::unique_ptr<CadEntity> CadEntity::fromJson(const QJsonObject& obj)
{
    const QString typeName = obj.value("type").toString();
    if (typeName == "line") return CadLine::fromJsonObject(obj);
    if (typeName == "circle") return CadCircle::fromJsonObject(obj);
    if (typeName == "rectangle") return CadRectangle::fromJsonObject(obj);
    if (typeName == "polyline") return CadPolyline::fromJsonObject(obj);
    if (typeName == "arc") return CadArc::fromJsonObject(obj);
    if (typeName == "ellipse") return CadEllipse::fromJsonObject(obj);
    if (typeName == "polygon") return CadPolygon::fromJsonObject(obj);
    if (typeName == "text") return CadText::fromJsonObject(obj);
    if (typeName == "linearDimension") return CadLinearDimension::fromJsonObject(obj);
    if (typeName == "leader") return CadLeader::fromJsonObject(obj);
    if (typeName == "hatch") return CadHatch::fromJsonObject(obj);
    if (typeName == "blockReference") return CadBlockReference::fromJsonObject(obj);
    return nullptr;
}

CadLine::CadLine(const QPointF& start, const QPointF& end)
    : m_start(start), m_end(end)
{
}

QGraphicsItem* CadLine::createGraphicsItem() const
{
    auto* item = new QGraphicsLineItem(QLineF(m_start, m_end));
    item->setPen(buildPen());
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadLine::toJson() const
{
    QJsonObject obj;
    obj["type"] = "line";
    writeCommonJson(obj);
    obj["start"] = pointToJson(m_start);
    obj["end"] = pointToJson(m_end);
    return obj;
}

std::unique_ptr<CadLine> CadLine::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadLine>(pointFromJson(obj.value("start")),
                                            pointFromJson(obj.value("end")));
    entity->readCommonJson(obj);
    return entity;
}

CadCircle::CadCircle(const QPointF& center, double radius)
    : m_center(center), m_radius(radius)
{
}

QGraphicsItem* CadCircle::createGraphicsItem() const
{
    auto* item = new QGraphicsEllipseItem(m_center.x() - m_radius,
                                          m_center.y() - m_radius,
                                          2.0 * m_radius,
                                          2.0 * m_radius);
    item->setPen(buildPen());
    item->setBrush(Qt::NoBrush);
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadCircle::toJson() const
{
    QJsonObject obj;
    obj["type"] = "circle";
    writeCommonJson(obj);
    obj["center"] = pointToJson(m_center);
    obj["radius"] = m_radius;
    return obj;
}

std::unique_ptr<CadCircle> CadCircle::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadCircle>(pointFromJson(obj.value("center")),
                                              obj.value("radius").toDouble());
    entity->readCommonJson(obj);
    return entity;
}

CadRectangle::CadRectangle(const QRectF& rect)
    : m_rect(rect.normalized())
{
}

QGraphicsItem* CadRectangle::createGraphicsItem() const
{
    auto* item = new QGraphicsRectItem(m_rect);
    item->setPen(buildPen());
    item->setBrush(Qt::NoBrush);
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadRectangle::toJson() const
{
    QJsonObject obj;
    obj["type"] = "rectangle";
    writeCommonJson(obj);
    obj["rect"] = rectToJson(m_rect);
    return obj;
}

std::unique_ptr<CadRectangle> CadRectangle::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadRectangle>(rectFromJson(obj.value("rect")));
    entity->readCommonJson(obj);
    return entity;
}

CadPolyline::CadPolyline(const QVector<QPointF>& points, bool closed)
    : m_points(points), m_closed(closed)
{
}

QGraphicsItem* CadPolyline::createGraphicsItem() const
{
    auto* item = new QGraphicsPathItem(polylinePath(m_points, m_closed));
    item->setPen(buildPen());
    item->setBrush(Qt::NoBrush);
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadPolyline::toJson() const
{
    QJsonObject obj;
    obj["type"] = "polyline";
    writeCommonJson(obj);
    obj["points"] = pointsToJson(m_points);
    obj["closed"] = m_closed;
    return obj;
}

std::unique_ptr<CadPolyline> CadPolyline::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadPolyline>(pointsFromJson(obj.value("points")),
                                                obj.value("closed").toBool(false));
    entity->readCommonJson(obj);
    return entity;
}

CadArc::CadArc(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg)
    : m_center(center), m_radius(radius), m_startAngleDeg(startAngleDeg), m_spanAngleDeg(spanAngleDeg)
{
}

QGraphicsItem* CadArc::createGraphicsItem() const
{
    QRectF r(m_center.x() - m_radius, m_center.y() - m_radius, 2.0 * m_radius, 2.0 * m_radius);
    QPainterPath path;
    path.arcMoveTo(r, m_startAngleDeg);
    path.arcTo(r, m_startAngleDeg, m_spanAngleDeg);
    auto* item = new QGraphicsPathItem(path);
    item->setPen(buildPen());
    item->setBrush(Qt::NoBrush);
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadArc::toJson() const
{
    QJsonObject obj;
    obj["type"] = "arc";
    writeCommonJson(obj);
    obj["center"] = pointToJson(m_center);
    obj["radius"] = m_radius;
    obj["startAngleDeg"] = m_startAngleDeg;
    obj["spanAngleDeg"] = m_spanAngleDeg;
    return obj;
}

std::unique_ptr<CadArc> CadArc::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadArc>(pointFromJson(obj.value("center")),
                                           obj.value("radius").toDouble(),
                                           obj.value("startAngleDeg").toDouble(),
                                           obj.value("spanAngleDeg").toDouble());
    entity->readCommonJson(obj);
    return entity;
}

CadEllipse::CadEllipse(const QRectF& rect)
    : m_rect(rect.normalized())
{
}

QGraphicsItem* CadEllipse::createGraphicsItem() const
{
    auto* item = new QGraphicsEllipseItem(m_rect);
    item->setPen(buildPen());
    item->setBrush(Qt::NoBrush);
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadEllipse::toJson() const
{
    QJsonObject obj;
    obj["type"] = "ellipse";
    writeCommonJson(obj);
    obj["rect"] = rectToJson(m_rect);
    return obj;
}

std::unique_ptr<CadEllipse> CadEllipse::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadEllipse>(rectFromJson(obj.value("rect")));
    entity->readCommonJson(obj);
    return entity;
}

CadPolygon::CadPolygon(const QPointF& center, double radius, int sides, double rotationDeg)
    : m_center(center), m_radius(radius), m_sides(qMax(3, sides)), m_rotationDeg(rotationDeg)
{
}

QGraphicsItem* CadPolygon::createGraphicsItem() const
{
    auto* item = new QGraphicsPolygonItem(regularPolygon(m_center, m_radius, m_sides, m_rotationDeg));
    item->setPen(buildPen());
    item->setBrush(Qt::NoBrush);
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadPolygon::toJson() const
{
    QJsonObject obj;
    obj["type"] = "polygon";
    writeCommonJson(obj);
    obj["center"] = pointToJson(m_center);
    obj["radius"] = m_radius;
    obj["sides"] = m_sides;
    obj["rotationDeg"] = m_rotationDeg;
    return obj;
}

std::unique_ptr<CadPolygon> CadPolygon::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadPolygon>(pointFromJson(obj.value("center")),
                                               obj.value("radius").toDouble(),
                                               obj.value("sides").toInt(6),
                                               obj.value("rotationDeg").toDouble(-90.0));
    entity->readCommonJson(obj);
    return entity;
}

// ─────────────────────────────────────────────────────────
// Transformations CAD 2D utilisées par les outils Modifier
// ─────────────────────────────────────────────────────────
std::unique_ptr<CadEntity> CadLine::clone() const { return std::make_unique<CadLine>(*this); }
void CadLine::translate(const QPointF& delta) { m_start += delta; m_end += delta; }
void CadLine::rotate(const QPointF& center, double angleDeg) { m_start = rotatePoint(m_start, center, angleDeg); m_end = rotatePoint(m_end, center, angleDeg); }
void CadLine::scale(const QPointF& center, double factor) { m_start = scalePoint(m_start, center, factor); m_end = scalePoint(m_end, center, factor); }
void CadLine::mirror(const QPointF& axisA, const QPointF& axisB) { m_start = mirrorPoint(m_start, axisA, axisB); m_end = mirrorPoint(m_end, axisA, axisB); }

std::unique_ptr<CadEntity> CadCircle::clone() const { return std::make_unique<CadCircle>(*this); }
void CadCircle::translate(const QPointF& delta) { m_center += delta; }
void CadCircle::rotate(const QPointF& center, double angleDeg) { m_center = rotatePoint(m_center, center, angleDeg); }
void CadCircle::scale(const QPointF& center, double factor) { m_center = scalePoint(m_center, center, factor); m_radius *= std::abs(factor); }
void CadCircle::mirror(const QPointF& axisA, const QPointF& axisB) { m_center = mirrorPoint(m_center, axisA, axisB); }

std::unique_ptr<CadEntity> CadRectangle::clone() const { return std::make_unique<CadRectangle>(*this); }
void CadRectangle::translate(const QPointF& delta) { m_rect.translate(delta); }
void CadRectangle::rotate(const QPointF& center, double angleDeg)
{
    QVector<QPointF> pts = rectCorners(m_rect);
    for (QPointF& p : pts) p = rotatePoint(p, center, angleDeg);
    m_rect = boundingRectFromPoints(pts);
}
void CadRectangle::scale(const QPointF& center, double factor)
{
    QVector<QPointF> pts = rectCorners(m_rect);
    for (QPointF& p : pts) p = scalePoint(p, center, factor);
    m_rect = boundingRectFromPoints(pts);
}
void CadRectangle::mirror(const QPointF& axisA, const QPointF& axisB)
{
    QVector<QPointF> pts = rectCorners(m_rect);
    for (QPointF& p : pts) p = mirrorPoint(p, axisA, axisB);
    m_rect = boundingRectFromPoints(pts);
}

std::unique_ptr<CadEntity> CadPolyline::clone() const { return std::make_unique<CadPolyline>(*this); }
void CadPolyline::translate(const QPointF& delta) { for (QPointF& p : m_points) p += delta; }
void CadPolyline::rotate(const QPointF& center, double angleDeg) { for (QPointF& p : m_points) p = rotatePoint(p, center, angleDeg); }
void CadPolyline::scale(const QPointF& center, double factor) { for (QPointF& p : m_points) p = scalePoint(p, center, factor); }
void CadPolyline::mirror(const QPointF& axisA, const QPointF& axisB) { for (QPointF& p : m_points) p = mirrorPoint(p, axisA, axisB); }

std::unique_ptr<CadEntity> CadArc::clone() const { return std::make_unique<CadArc>(*this); }
void CadArc::translate(const QPointF& delta) { m_center += delta; }
void CadArc::rotate(const QPointF& center, double angleDeg) { m_center = rotatePoint(m_center, center, angleDeg); m_startAngleDeg += angleDeg; }
void CadArc::scale(const QPointF& center, double factor) { m_center = scalePoint(m_center, center, factor); m_radius *= std::abs(factor); }
void CadArc::mirror(const QPointF& axisA, const QPointF& axisB)
{
    const double startRad = qDegreesToRadians(m_startAngleDeg);
    const double endRad = qDegreesToRadians(m_startAngleDeg + m_spanAngleDeg);
    QPointF start(m_center.x() + m_radius * std::cos(startRad), m_center.y() - m_radius * std::sin(startRad));
    QPointF end(m_center.x() + m_radius * std::cos(endRad), m_center.y() - m_radius * std::sin(endRad));
    m_center = mirrorPoint(m_center, axisA, axisB);
    start = mirrorPoint(start, axisA, axisB);
    end = mirrorPoint(end, axisA, axisB);
    m_startAngleDeg = qRadiansToDegrees(std::atan2(m_center.y() - start.y(), start.x() - m_center.x()));
    const double endAngle = qRadiansToDegrees(std::atan2(m_center.y() - end.y(), end.x() - m_center.x()));
    m_spanAngleDeg = endAngle - m_startAngleDeg;
}

std::unique_ptr<CadEntity> CadEllipse::clone() const { return std::make_unique<CadEllipse>(*this); }
void CadEllipse::translate(const QPointF& delta) { m_rect.translate(delta); }
void CadEllipse::rotate(const QPointF& center, double angleDeg)
{
    QVector<QPointF> pts = rectCorners(m_rect);
    for (QPointF& p : pts) p = rotatePoint(p, center, angleDeg);
    m_rect = boundingRectFromPoints(pts);
}
void CadEllipse::scale(const QPointF& center, double factor)
{
    QVector<QPointF> pts = rectCorners(m_rect);
    for (QPointF& p : pts) p = scalePoint(p, center, factor);
    m_rect = boundingRectFromPoints(pts);
}
void CadEllipse::mirror(const QPointF& axisA, const QPointF& axisB)
{
    QVector<QPointF> pts = rectCorners(m_rect);
    for (QPointF& p : pts) p = mirrorPoint(p, axisA, axisB);
    m_rect = boundingRectFromPoints(pts);
}

std::unique_ptr<CadEntity> CadPolygon::clone() const { return std::make_unique<CadPolygon>(*this); }
void CadPolygon::translate(const QPointF& delta) { m_center += delta; }
void CadPolygon::rotate(const QPointF& center, double angleDeg) { m_center = rotatePoint(m_center, center, angleDeg); m_rotationDeg += angleDeg; }
void CadPolygon::scale(const QPointF& center, double factor) { m_center = scalePoint(m_center, center, factor); m_radius *= std::abs(factor); }
void CadPolygon::mirror(const QPointF& axisA, const QPointF& axisB)
{
    const double a = qDegreesToRadians(m_rotationDeg);
    QPointF firstVertex(m_center.x() + m_radius * std::cos(a), m_center.y() + m_radius * std::sin(a));
    m_center = mirrorPoint(m_center, axisA, axisB);
    firstVertex = mirrorPoint(firstVertex, axisA, axisB);
    m_rotationDeg = qRadiansToDegrees(std::atan2(firstVertex.y() - m_center.y(), firstVertex.x() - m_center.x()));
}



// ─────────────────────────────────────────────────────────
// Blocks / symboles 2D
// ─────────────────────────────────────────────────────────
CadBlockReference::CadBlockReference(const QString& name, const QPointF& basePoint, const QPointF& insertionPoint,
                                     const QJsonArray& entities, double scaleFactor, double rotationDeg)
    : m_name(name.trimmed().isEmpty() ? QStringLiteral("Block") : name.trimmed()),
      m_basePoint(basePoint),
      m_insertionPoint(insertionPoint),
      m_entities(entities),
      m_scaleFactor(scaleFactor == 0.0 ? 1.0 : scaleFactor),
      m_rotationDeg(rotationDeg)
{
}

QGraphicsItem* CadBlockReference::createGraphicsItem() const
{
    auto* group = new QGraphicsItemGroup();

    // Mode bloc leger: utilise pour les gros DWG AutoCAD.
    // On evite de recreer des milliers de QGraphicsItem pour chaque INSERT.
    if (m_entities.isEmpty()) {
        const double r = 4.0;
        QPen pen(color().isValid() ? color() : QColor(0, 255, 255));
        pen.setCosmetic(true);
        pen.setWidthF(0.0);

        auto* h = new QGraphicsLineItem(-r, 0.0, r, 0.0);
        auto* v = new QGraphicsLineItem(0.0, -r, 0.0, r);
        auto* box = new QGraphicsRectItem(-r, -r, 2.0 * r, 2.0 * r);
        h->setPen(pen);
        v->setPen(pen);
        box->setPen(pen);
        box->setBrush(Qt::NoBrush);
        h->setFlag(QGraphicsItem::ItemIsSelectable, false);
        v->setFlag(QGraphicsItem::ItemIsSelectable, false);
        box->setFlag(QGraphicsItem::ItemIsSelectable, false);
        group->addToGroup(h);
        group->addToGroup(v);
        group->addToGroup(box);

        auto* label = new QGraphicsSimpleTextItem(m_name);
        label->setBrush(QBrush(pen.color()));
        label->setScale(0.8);
        label->setPos(r + 1.0, -r);
        label->setFlag(QGraphicsItem::ItemIsSelectable, false);
        group->addToGroup(label);
    } else {
        for (const QJsonValue& value : m_entities) {
            QJsonObject obj = value.toObject();
            auto entity = CadEntity::fromJson(obj);
            if (!entity) continue;

            entity->translate(-m_basePoint);
            QGraphicsItem* child = entity->createGraphicsItem();
            if (!child) continue;
            child->setFlag(QGraphicsItem::ItemIsSelectable, false);
            group->addToGroup(child);
        }
    }

    group->setPos(m_insertionPoint);
    group->setScale(m_scaleFactor == 0.0 ? 1.0 : m_scaleFactor);
    group->setRotation(-m_rotationDeg);
    group->setData(0, layer());
    group->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return group;
}

QJsonObject CadBlockReference::toJson() const
{
    QJsonObject obj;
    obj["type"] = "blockReference";
    writeCommonJson(obj);
    obj["name"] = m_name;
    obj["basePoint"] = pointToJson(m_basePoint);
    obj["insertionPoint"] = pointToJson(m_insertionPoint);
    obj["scaleFactor"] = m_scaleFactor;
    obj["rotationDeg"] = m_rotationDeg;
    obj["entities"] = m_entities;
    return obj;
}

std::unique_ptr<CadBlockReference> CadBlockReference::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadBlockReference>(
        obj.value("name").toString("Block"),
        pointFromJson(obj.value("basePoint")),
        pointFromJson(obj.value("insertionPoint")),
        obj.value("entities").toArray(),
        obj.value("scaleFactor").toDouble(1.0),
        obj.value("rotationDeg").toDouble(0.0));
    entity->readCommonJson(obj);
    return entity;
}

std::unique_ptr<CadEntity> CadBlockReference::clone() const { return std::make_unique<CadBlockReference>(*this); }
void CadBlockReference::translate(const QPointF& delta) { m_insertionPoint += delta; }
void CadBlockReference::rotate(const QPointF& center, double angleDeg)
{
    m_insertionPoint = rotatePoint(m_insertionPoint, center, angleDeg);
    m_rotationDeg += angleDeg;
}
void CadBlockReference::scale(const QPointF& center, double factor)
{
    m_insertionPoint = scalePoint(m_insertionPoint, center, factor);
    m_scaleFactor *= std::abs(factor);
}
void CadBlockReference::mirror(const QPointF& axisA, const QPointF& axisB)
{
    m_insertionPoint = mirrorPoint(m_insertionPoint, axisA, axisB);
    m_scaleFactor = -m_scaleFactor;
}

// ─────────────────────────────────────────────────────────
// Annotation CAD 2D : texte, cotation linéaire, ligne de repère
// ─────────────────────────────────────────────────────────
namespace {
QString cadAnnotationFontFamily(QString fontName)
{
    fontName = fontName.trimmed();
    if (fontName.isEmpty()) return QStringLiteral("Arial");
    if (fontName.contains(QLatin1Char('|'))) fontName = fontName.section(QLatin1Char('|'), 0, 0).trimmed();
    if (fontName.endsWith(QStringLiteral(".shx"), Qt::CaseInsensitive)) fontName.chop(4);
    const QString upper = fontName.toUpper();
    if (upper == QStringLiteral("TXT") || upper == QStringLiteral("STANDARD") || upper == QStringLiteral("ROMANS") || upper == QStringLiteral("SIMPLEX"))
        return QStringLiteral("Arial");
    return fontName;
}

QStringList wrapAnnotationLines(const QString& text, const QFontMetricsF& metrics, double boxWidth, double widthFactor)
{
    QStringList source = text.split(QLatin1Char('\n'));
    if (source.isEmpty()) source << QString();
    if (!(boxWidth > 1.0e-9)) return source;

    const double effectiveWidth = qMax(1.0, boxWidth / qBound(0.01, widthFactor, 100.0));
    QStringList wrapped;
    for (const QString& paragraph : source) {
        if (paragraph.trimmed().isEmpty()) {
            wrapped << QString();
            continue;
        }

        QString current;
        const QStringList words = paragraph.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        for (const QString& word : words) {
            const QString candidate = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
            if (!current.isEmpty() && metrics.horizontalAdvance(candidate) > effectiveWidth) {
                wrapped << current;
                current = word;
            } else {
                current = candidate;
            }
        }
        if (!current.isEmpty()) wrapped << current;
    }
    return wrapped.isEmpty() ? QStringList{text} : wrapped;
}

QGraphicsPathItem* makeAnnotationText(const QString& text, const QPointF& position, double height, const QColor& color,
                                      double rotationDeg = 0.0, double widthFactor = 1.0,
                                      double obliqueAngleDeg = 0.0, const QString& fontName = QStringLiteral("TXT"),
                                      double boxWidth = 0.0, double lineSpacingFactor = 1.0)
{
    // Texte vectoriel CAD. Contrairement a QPainterPath::addText() avec une chaine brute,
    // on dessine les lignes MTEXT une par une. Cela evite les textes DXF qui se superposent,
    // les retours \P ignores, et les annotations qui traversent les nuages/revclouds.
    QFont font(cadAnnotationFontFamily(fontName));
    font.setPointSizeF(qMax(0.1, height));
    font.setStyleStrategy(QFont::PreferAntialias);

    const double safeWidthFactor = qBound(0.01, widthFactor, 100.0);
    const double safeLineSpacing = qBound(0.25, lineSpacingFactor, 4.0);
    QFontMetricsF metrics(font);
    const QStringList lines = wrapAnnotationLines(text, metrics, boxWidth, safeWidthFactor);
    const double lineAdvance = qMax(height * 1.05, metrics.height() * 0.95) * safeLineSpacing;

    QPainterPath path;
    double y = 0.0;
    for (const QString& line : lines) {
        if (!line.isEmpty()) path.addText(QPointF(0.0, y), font, line);
        y += lineAdvance;
    }

    QTransform textTransform;
    textTransform.shear(std::tan(qDegreesToRadians(qBound(-85.0, obliqueAngleDeg, 85.0))), 0.0);
    textTransform.scale(safeWidthFactor, 1.0);
    path = textTransform.map(path);

    auto* item = new QGraphicsPathItem(path);
    item->setPen(Qt::NoPen);
    item->setBrush(QBrush(CadRenderStyle::deepCadColor(color)));
    item->setPos(position);
    item->setRotation(-rotationDeg);
    return item;
}

QPolygonF arrowHead(const QPointF& tip, const QPointF& from, double size)
{
    const double angle = std::atan2(from.y() - tip.y(), from.x() - tip.x());
    const double a1 = angle + M_PI / 7.0;
    const double a2 = angle - M_PI / 7.0;
    QPolygonF poly;
    poly << tip
         << QPointF(tip.x() + size * std::cos(a1), tip.y() + size * std::sin(a1))
         << QPointF(tip.x() + size * std::cos(a2), tip.y() + size * std::sin(a2));
    return poly;
}
}


// ─────────────────────────────────────────────────────────
// Hatch / fill 2D
// ─────────────────────────────────────────────────────────
CadHatch::CadHatch(const QVector<QPointF>& boundary, const QString& pattern, double scale, double angleDeg)
    : m_boundary(boundary),
      m_pattern(pattern.trimmed().isEmpty() ? QStringLiteral("ANSI31") : pattern.trimmed()),
      m_scale(std::max(0.01, scale)),
      m_angleDeg(angleDeg)
{
    if (boundary.size() >= 3) m_loops.append(boundary);
}

CadHatch::CadHatch(const QVector<QVector<QPointF>>& loops, const QString& pattern, double scale, double angleDeg)
    : m_pattern(pattern.trimmed().isEmpty() ? QStringLiteral("ANSI31") : pattern.trimmed()),
      m_scale(std::max(0.01, scale)),
      m_angleDeg(angleDeg)
{
    for (const QVector<QPointF>& loop : loops) {
        if (loop.size() >= 3) m_loops.append(loop);
    }
    if (!m_loops.isEmpty()) m_boundary = m_loops.first();
}

QGraphicsItem* CadHatch::createGraphicsItem() const
{
    QPainterPath hatchPath;
    hatchPath.setFillRule(Qt::OddEvenFill);
    if (!m_loops.isEmpty()) {
        for (const QVector<QPointF>& loop : m_loops) {
            if (hatchLoopCanBeClosedSafely(loop)) hatchPath.addPath(polylinePath(loop, true));
        }
    } else if (hatchLoopCanBeClosedSafely(m_boundary)) {
        hatchPath = polylinePath(m_boundary, true);
        hatchPath.setFillRule(Qt::OddEvenFill);
    }
    auto* item = new QGraphicsPathItem(hatchPath);
    QPen pen = buildPen();
    pen.setStyle(Qt::SolidLine);
    item->setPen(pen);

    QColor fill = CadRenderStyle::deepCadColor(color());
    if (fill.alpha() == 255) fill.setAlpha(170);
    item->setBrush(materialHatchBrush(m_pattern, fill, m_scale, m_angleDeg));
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadHatch::toJson() const
{
    QJsonObject obj;
    obj["type"] = "hatch";
    writeCommonJson(obj);
    obj["boundary"] = pointsToJson(m_boundary);
    QJsonArray loopsArray;
    for (const QVector<QPointF>& loop : m_loops) loopsArray.append(pointsToJson(loop));
    obj["loops"] = loopsArray;
    obj["pattern"] = m_pattern;
    obj["scale"] = m_scale;
    obj["angleDeg"] = m_angleDeg;
    return obj;
}

std::unique_ptr<CadHatch> CadHatch::fromJsonObject(const QJsonObject& obj)
{
    QVector<QVector<QPointF>> loops;
    const QJsonArray loopsArray = obj.value("loops").toArray();
    for (const QJsonValue& value : loopsArray) {
        QVector<QPointF> loop = pointsFromJson(value);
        if (loop.size() >= 3) loops.append(loop);
    }
    std::unique_ptr<CadHatch> entity;
    if (!loops.isEmpty()) {
        entity = std::make_unique<CadHatch>(loops, obj.value("pattern").toString("ANSI31"),
                                            obj.value("scale").toDouble(1.0),
                                            obj.value("angleDeg").toDouble(45.0));
    } else {
        entity = std::make_unique<CadHatch>(pointsFromJson(obj.value("boundary")),
                                            obj.value("pattern").toString("ANSI31"),
                                            obj.value("scale").toDouble(1.0),
                                            obj.value("angleDeg").toDouble(45.0));
    }
    entity->readCommonJson(obj);
    return entity;
}

std::unique_ptr<CadEntity> CadHatch::clone() const { return std::make_unique<CadHatch>(*this); }

void CadHatch::translate(const QPointF& delta)
{
    for (QPointF& p : m_boundary) p += delta;
    for (QVector<QPointF>& loop : m_loops) for (QPointF& p : loop) p += delta;
}

void CadHatch::rotate(const QPointF& center, double angleDeg)
{
    for (QPointF& p : m_boundary) p = rotatePoint(p, center, angleDeg);
    for (QVector<QPointF>& loop : m_loops) for (QPointF& p : loop) p = rotatePoint(p, center, angleDeg);
    m_angleDeg += angleDeg;
}

void CadHatch::scale(const QPointF& center, double factor)
{
    for (QPointF& p : m_boundary) p = scalePoint(p, center, factor);
    for (QVector<QPointF>& loop : m_loops) for (QPointF& p : loop) p = scalePoint(p, center, factor);
    m_scale *= std::abs(factor);
}

void CadHatch::mirror(const QPointF& axisA, const QPointF& axisB)
{
    for (QPointF& p : m_boundary) p = mirrorPoint(p, axisA, axisB);
    for (QVector<QPointF>& loop : m_loops) for (QPointF& p : loop) p = mirrorPoint(p, axisA, axisB);
    m_angleDeg = -m_angleDeg;
}

CadText::CadText(const QPointF& position, const QString& text, double height, double rotationDeg,
                 double widthFactor, double obliqueAngleDeg, const QString& fontName)
    : m_position(position), m_text(text.trimmed().isEmpty() ? QStringLiteral("Text") : text),
      m_height(qMax(1.0e-6, height)), m_rotationDeg(rotationDeg),
      m_widthFactor(qBound(0.01, widthFactor, 100.0)),
      m_obliqueAngleDeg(qBound(-85.0, obliqueAngleDeg, 85.0)),
      m_fontName(fontName.trimmed().isEmpty() ? QStringLiteral("TXT") : fontName.trimmed())
{
}

QGraphicsItem* CadText::createGraphicsItem() const
{
    // DXF MTEXT/MLEADER notes are often anchored on the landing line.
    // Lift the rendered glyphs slightly so underline/leader geometry does not
    // cut through the text, matching common AutoCAD/BricsCAD display behavior.
    QPointF renderPosition = m_position;
    if (m_isMText) renderPosition += QPointF(0.0, -m_height * 0.22);

    auto* item = makeAnnotationText(m_text, renderPosition, m_height, color(), m_rotationDeg,
                                    m_widthFactor, m_obliqueAngleDeg, m_fontName,
                                    m_boxWidth, m_lineSpacingFactor);
    item->setData(0, layer());
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return item;
}

QJsonObject CadText::toJson() const
{
    QJsonObject obj;
    obj["type"] = "text";
    writeCommonJson(obj);
    obj["position"] = pointToJson(m_position);
    obj["text"] = m_text;
    obj["height"] = m_height;
    obj["rotationDeg"] = m_rotationDeg;
    obj["widthFactor"] = m_widthFactor;
    obj["obliqueAngleDeg"] = m_obliqueAngleDeg;
    obj["fontName"] = m_fontName;
    obj["horizontalJustification"] = m_horizontalJustification;
    obj["verticalJustification"] = m_verticalJustification;
    obj["boxWidth"] = m_boxWidth;
    obj["lineSpacingFactor"] = m_lineSpacingFactor;
    obj["isMText"] = m_isMText;
    return obj;
}

std::unique_ptr<CadText> CadText::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadText>(pointFromJson(obj.value("position")),
                                            obj.value("text").toString("Text"),
                                            obj.value("height").toDouble(8.0),
                                            obj.value("rotationDeg").toDouble(0.0));
    entity->setWidthFactor(obj.value("widthFactor").toDouble(1.0));
    entity->setObliqueAngleDeg(obj.value("obliqueAngleDeg").toDouble(0.0));
    entity->setFontName(obj.value("fontName").toString("TXT"));
    entity->setHorizontalJustification(obj.value("horizontalJustification").toInt(0));
    entity->setVerticalJustification(obj.value("verticalJustification").toInt(0));
    entity->setBoxWidth(obj.value("boxWidth").toDouble(0.0));
    entity->setLineSpacingFactor(obj.value("lineSpacingFactor").toDouble(1.0));
    entity->setMText(obj.value("isMText").toBool(false));
    entity->readCommonJson(obj);
    return entity;
}

std::unique_ptr<CadEntity> CadText::clone() const { return std::make_unique<CadText>(*this); }
void CadText::translate(const QPointF& delta) { m_position += delta; }
void CadText::rotate(const QPointF& center, double angleDeg) { m_position = rotatePoint(m_position, center, angleDeg); m_rotationDeg += angleDeg; }
void CadText::scale(const QPointF& center, double factor) { m_position = scalePoint(m_position, center, factor); m_height = qMax(1.0e-6, m_height * std::abs(factor)); m_boxWidth *= std::abs(factor); }
void CadText::mirror(const QPointF& axisA, const QPointF& axisB) { m_position = mirrorPoint(m_position, axisA, axisB); m_rotationDeg = -m_rotationDeg; }

CadLinearDimension::CadLinearDimension(const QPointF& first, const QPointF& second, const QPointF& dimensionPoint, double textHeight, double arrowSize)
    : m_first(first), m_second(second), m_dimensionPoint(dimensionPoint), m_textHeight(qMax(1.0e-6, textHeight)), m_arrowSize(qMax(0.0, arrowSize))
{
}

double CadLinearDimension::measuredLength() const
{
    const double dx = m_second.x() - m_first.x();
    const double dy = m_second.y() - m_first.y();
    return std::sqrt(dx * dx + dy * dy);
}

QGraphicsItem* CadLinearDimension::createGraphicsItem() const
{
    auto* group = new QGraphicsItemGroup();
    const QPen pen = buildPen();
    const double len = measuredLength();
    if (len < 1e-9) return group;

    QPointF u((m_second.x() - m_first.x()) / len, (m_second.y() - m_first.y()) / len);
    QPointF n(-u.y(), u.x());
    const QPointF mid((m_first.x() + m_second.x()) / 2.0, (m_first.y() + m_second.y()) / 2.0);
    const double offset = (m_dimensionPoint.x() - mid.x()) * n.x() + (m_dimensionPoint.y() - mid.y()) * n.y();
    const QPointF d1 = m_first + n * offset;
    const QPointF d2 = m_second + n * offset;

    auto addLine = [&](const QPointF& a, const QPointF& b) {
        auto* l = new QGraphicsLineItem(QLineF(a, b));
        l->setPen(pen);
        group->addToGroup(l);
    };
    addLine(m_first, d1);
    addLine(m_second, d2);
    addLine(d1, d2);

    const double arrowSize = m_arrowSize > 1.0e-9 ? m_arrowSize : qMax(2.0, m_textHeight * 0.55);
    auto* a1 = new QGraphicsPolygonItem(arrowHead(d1, d2, arrowSize));
    auto* a2 = new QGraphicsPolygonItem(arrowHead(d2, d1, arrowSize));
    a1->setPen(pen); a1->setBrush(QBrush(color()));
    a2->setPen(pen); a2->setBrush(QBrush(color()));
    group->addToGroup(a1);
    group->addToGroup(a2);

    const QString label = QString::number(len, 'f', 2);
    auto* txt = makeAnnotationText(label, QPointF((d1.x()+d2.x())/2.0, (d1.y()+d2.y())/2.0) + n * (m_textHeight * 0.35), m_textHeight, color(), qRadiansToDegrees(std::atan2(u.y(), u.x())));
    QRectF br = txt->boundingRect();
    txt->setPos(txt->pos() - QPointF(br.width()/2.0, br.height()/2.0));
    group->addToGroup(txt);

    group->setData(0, layer());
    group->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return group;
}

QJsonObject CadLinearDimension::toJson() const
{
    QJsonObject obj;
    obj["type"] = "linearDimension";
    writeCommonJson(obj);
    obj["first"] = pointToJson(m_first);
    obj["second"] = pointToJson(m_second);
    obj["dimensionPoint"] = pointToJson(m_dimensionPoint);
    obj["textHeight"] = m_textHeight;
    obj["arrowSize"] = m_arrowSize;
    return obj;
}

std::unique_ptr<CadLinearDimension> CadLinearDimension::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadLinearDimension>(pointFromJson(obj.value("first")),
                                                       pointFromJson(obj.value("second")),
                                                       pointFromJson(obj.value("dimensionPoint")),
                                                       obj.value("textHeight").toDouble(6.0),
                                                       obj.value("arrowSize").toDouble(0.0));
    entity->readCommonJson(obj);
    return entity;
}

std::unique_ptr<CadEntity> CadLinearDimension::clone() const { return std::make_unique<CadLinearDimension>(*this); }
void CadLinearDimension::translate(const QPointF& delta) { m_first += delta; m_second += delta; m_dimensionPoint += delta; }
void CadLinearDimension::rotate(const QPointF& center, double angleDeg) { m_first = rotatePoint(m_first, center, angleDeg); m_second = rotatePoint(m_second, center, angleDeg); m_dimensionPoint = rotatePoint(m_dimensionPoint, center, angleDeg); }
void CadLinearDimension::scale(const QPointF& center, double factor) { m_first = scalePoint(m_first, center, factor); m_second = scalePoint(m_second, center, factor); m_dimensionPoint = scalePoint(m_dimensionPoint, center, factor); m_textHeight *= std::abs(factor); m_arrowSize *= std::abs(factor); }
void CadLinearDimension::mirror(const QPointF& axisA, const QPointF& axisB) { m_first = mirrorPoint(m_first, axisA, axisB); m_second = mirrorPoint(m_second, axisA, axisB); m_dimensionPoint = mirrorPoint(m_dimensionPoint, axisA, axisB); }

CadLeader::CadLeader(const QPointF& arrowPoint, const QPointF& textPoint, const QString& text, double textHeight)
    : m_arrowPoint(arrowPoint), m_textPoint(textPoint), m_text(text.trimmed()), m_textHeight(qMax(1.0, textHeight))
{
}

QGraphicsItem* CadLeader::createGraphicsItem() const
{
    auto* group = new QGraphicsItemGroup();
    const QPen pen = buildPen();

    QLineF leaderLine(m_arrowPoint, m_textPoint);
    auto* l = new QGraphicsLineItem(leaderLine);
    l->setPen(pen);
    group->addToGroup(l);

    const double arrowSize = qMax(2.0, m_textHeight * 0.65);
    auto* arrow = new QGraphicsPolygonItem(arrowHead(m_arrowPoint, m_textPoint, arrowSize));
    arrow->setPen(pen);
    arrow->setBrush(QBrush(color()));
    group->addToGroup(arrow);

    if (!m_text.trimmed().isEmpty()) {
        // Keep leader/MLEADER text above the landing line.  In many DXF files
        // the leader endpoint is the underline/landing point, not the text
        // baseline. Drawing directly on m_textPoint makes the line pass through
        // the letters.  Use the leader direction plus an upward normal offset.
        QPointF u(1.0, 0.0);
        if (leaderLine.length() > 1.0e-9) {
            u = QPointF(leaderLine.dx() / leaderLine.length(), leaderLine.dy() / leaderLine.length());
        }
        QPointF n(-u.y(), u.x());
        // In the QGraphics scene used by the viewer, visually above is negative Y.
        if (n.y() > 0.0) n = -n;
        if (std::abs(n.y()) < 0.2) n = QPointF(0.0, -1.0);

        const QPointF textOrigin = m_textPoint + u * (m_textHeight * 0.35) + n * (m_textHeight * 0.95);
        auto* txt = makeAnnotationText(m_text, textOrigin,
                                       m_textHeight, color(), 0.0, 1.0, 0.0, QStringLiteral("Arial"),
                                       0.0, 1.0);
        group->addToGroup(txt);
    }

    group->setData(0, layer());
    group->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
    return group;
}

QJsonObject CadLeader::toJson() const
{
    QJsonObject obj;
    obj["type"] = "leader";
    writeCommonJson(obj);
    obj["arrowPoint"] = pointToJson(m_arrowPoint);
    obj["textPoint"] = pointToJson(m_textPoint);
    obj["text"] = m_text;
    obj["textHeight"] = m_textHeight;
    return obj;
}

std::unique_ptr<CadLeader> CadLeader::fromJsonObject(const QJsonObject& obj)
{
    auto entity = std::make_unique<CadLeader>(pointFromJson(obj.value("arrowPoint")),
                                              pointFromJson(obj.value("textPoint")),
                                              obj.value("text").toString("Leader"),
                                              obj.value("textHeight").toDouble(6.0));
    entity->readCommonJson(obj);
    return entity;
}

std::unique_ptr<CadEntity> CadLeader::clone() const { return std::make_unique<CadLeader>(*this); }
void CadLeader::translate(const QPointF& delta) { m_arrowPoint += delta; m_textPoint += delta; }
void CadLeader::rotate(const QPointF& center, double angleDeg) { m_arrowPoint = rotatePoint(m_arrowPoint, center, angleDeg); m_textPoint = rotatePoint(m_textPoint, center, angleDeg); }
void CadLeader::scale(const QPointF& center, double factor) { m_arrowPoint = scalePoint(m_arrowPoint, center, factor); m_textPoint = scalePoint(m_textPoint, center, factor); m_textHeight *= std::abs(factor); }
void CadLeader::mirror(const QPointF& axisA, const QPointF& axisB) { m_arrowPoint = mirrorPoint(m_arrowPoint, axisA, axisB); m_textPoint = mirrorPoint(m_textPoint, axisA, axisB); }
