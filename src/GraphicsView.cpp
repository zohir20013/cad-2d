#include "GraphicsView.h"
#include "cad/CadDocument.h"
#include "cad/CadEntity.h"
#include "cad/CadRenderStyle.h"
#include "DebugLogger.h"
#include "geometry/GeometryKernel.h"

#include <QGraphicsScene>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QGraphicsItem>
#include <QWidget>
#include <QLineEdit>
#include <QEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSet>
#include <QPen>
#include <QBrush>
#include <QPainterPath>
#include <QTimer>
#include <QRegularExpression>
#include <QKeySequence>
#include <QCursor>
#include <QPolygonF>
#include <QPair>
#include <QSizeF>
#include <QtMath>
#include <cmath>
#include <memory>
#include <algorithm>
#include <limits>
#include <typeinfo>

namespace {
QPen drawingPen(Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(QColor(255, 255, 255));
    pen.setCosmetic(true);
    pen.setWidthF(1.5);
    pen.setStyle(style);
    return pen;
}

double distanceBetween(const QPointF& a, const QPointF& b)
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    return std::sqrt(dx * dx + dy * dy);
}

// EXTEND: seules les entités ouvertes avec deux extrémités peuvent être sources.
// Les entités fermées (rectangle, cercle, ellipse, polygone, hachure, polyligne fermée)
// restent utilisables comme limites, mais ne doivent jamais être prolongées.
bool isExtendableSourceEntityForExtend(const CadEntity* entity);

bool circleThroughThreePoints(const QPointF& p1, const QPointF& p2, const QPointF& p3,
                              QPointF& center, double& radius)
{
    const double x1 = p1.x();
    const double y1 = p1.y();
    const double x2 = p2.x();
    const double y2 = p2.y();
    const double x3 = p3.x();
    const double y3 = p3.y();

    const double d = 2.0 * (x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2));
    if (std::abs(d) < 1e-9) return false;

    const double x1s = x1 * x1 + y1 * y1;
    const double x2s = x2 * x2 + y2 * y2;
    const double x3s = x3 * x3 + y3 * y3;

    const double ux = (x1s * (y2 - y3) + x2s * (y3 - y1) + x3s * (y1 - y2)) / d;
    const double uy = (x1s * (x3 - x2) + x2s * (x1 - x3) + x3s * (x2 - x1)) / d;

    center = QPointF(ux, uy);
    radius = distanceBetween(center, p1);
    return radius > 1e-9;
}

double normalizedAngle(double a)
{
    while (a < 0.0) a += 360.0;
    while (a >= 360.0) a -= 360.0;
    return a;
}

double pointAngleDeg(const QPointF& center, const QPointF& p)
{
    return normalizedAngle(qRadiansToDegrees(std::atan2(center.y() - p.y(), p.x() - center.x())));
}

double ccwSpan(double start, double end)
{
    double span = normalizedAngle(end) - normalizedAngle(start);
    if (span < 0.0) span += 360.0;
    return span;
}

bool angleOnCcwArc(double start, double span, double angle)
{
    const double s = normalizedAngle(start);
    const double a = ccwSpan(s, angle);
    return a >= -1e-6 && a <= span + 1e-6;
}

bool arcFromThreePoints(const QPointF& start, const QPointF& mid, const QPointF& end,
                        QPointF& center, double& radius, double& startAngle, double& spanAngle)
{
    if (!circleThroughThreePoints(start, mid, end, center, radius)) return false;
    startAngle = pointAngleDeg(center, start);
    const double midAngle = pointAngleDeg(center, mid);
    const double endAngle = pointAngleDeg(center, end);
    const double spanCcw = ccwSpan(startAngle, endAngle);
    spanAngle = angleOnCcwArc(startAngle, spanCcw, midAngle) ? spanCcw : -(360.0 - spanCcw);
    return std::abs(spanAngle) > 1e-6;
}

QPainterPath polylinePath(const QVector<QPointF>& points, const QPointF& floatingPoint, bool includeFloating)
{
    QPainterPath path;
    if (points.isEmpty()) return path;
    path.moveTo(points.first());
    for (int i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
    if (includeFloating) path.lineTo(floatingPoint);
    return path;
}

struct Segment {
    QPointF a;
    QPointF b;
};

struct CircleInfo {
    QPointF center;
    double radius = 0.0;
};

double sceneToleranceFromView(const QGraphicsView* view, double pixels)
{
    if (!view) return pixels;
    const QPointF p0 = view->mapToScene(QPoint(0, 0));
    const QPointF px = view->mapToScene(QPoint(static_cast<int>(pixels), 0));
    const double d = distanceBetween(p0, px);
    return d > 1e-9 ? d : pixels;
}

QPointF nearestPointOnCircle(const QPointF& point, const CircleInfo& circle)
{
    const double dx = point.x() - circle.center.x();
    const double dy = point.y() - circle.center.y();
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d <= 1e-12 || circle.radius <= 1e-9) {
        return QPointF(circle.center.x() + circle.radius, circle.center.y());
    }
    return QPointF(circle.center.x() + circle.radius * dx / d,
                   circle.center.y() + circle.radius * dy / d);
}

QVector<QPointF> tangentPointsFromPointToCircle(const QPointF& point, const CircleInfo& circle)
{
    QVector<QPointF> points;
    const double dx = point.x() - circle.center.x();
    const double dy = point.y() - circle.center.y();
    const double d = std::sqrt(dx * dx + dy * dy);
    if (circle.radius <= 1e-9 || d <= circle.radius + 1e-9) return points;

    const double base = std::atan2(dy, dx);
    const double offset = std::acos(circle.radius / d);
    for (double a : {base + offset, base - offset}) {
        points.append(QPointF(circle.center.x() + circle.radius * std::cos(a),
                              circle.center.y() + circle.radius * std::sin(a)));
    }
    return points;
}

QVector<QPair<QPointF, QPointF>> circleCircleTangentPairs(const CircleInfo& a, const CircleInfo& b)
{
    QVector<QPair<QPointF, QPointF>> pairs;
    const QPointF d(b.center.x() - a.center.x(), b.center.y() - a.center.y());
    const double sq = d.x() * d.x() + d.y() * d.y();
    if (sq <= 1e-12 || a.radius <= 1e-9 || b.radius <= 1e-9) return pairs;

    for (double sign1 : {-1.0, 1.0}) {
        const double r = a.radius - sign1 * b.radius;
        const double h2 = sq - r * r;
        if (h2 < -1e-9) continue;
        const double h = std::sqrt(std::max(0.0, h2));
        for (double sign2 : {-1.0, 1.0}) {
            const QPointF v((d.x() * r - d.y() * h * sign2) / sq,
                            (d.y() * r + d.x() * h * sign2) / sq);
            const QPointF p1(a.center.x() + v.x() * a.radius,
                             a.center.y() + v.y() * a.radius);
            const QPointF p2(b.center.x() + v.x() * b.radius * sign1,
                             b.center.y() + v.y() * b.radius * sign1);
            pairs.append(qMakePair(p1, p2));
        }
    }
    return pairs;
}



double pointToSegmentDistance(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 1e-18) return distanceBetween(p, a);
    const double t = std::clamp(((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2, 0.0, 1.0);
    const QPointF projection(a.x() + t * dx, a.y() + t * dy);
    return distanceBetween(p, projection);
}

bool segmentNearPoint(const QPointF& p, const QPointF& a, const QPointF& b, double tolerance)
{
    QRectF box(a, b);
    box = box.normalized().adjusted(-tolerance, -tolerance, tolerance, tolerance);
    if (!box.contains(p)) return false;
    return pointToSegmentDistance(p, a, b) <= tolerance;
}

bool circleNearPoint(const QPointF& p, const CircleInfo& circle, double tolerance)
{
    if (circle.radius <= 1e-9) return false;
    return std::abs(distanceBetween(p, circle.center) - circle.radius) <= tolerance;
}


QRectF pointsBounds(const QVector<QPointF>& points)
{
    if (points.isEmpty()) return QRectF();
    QRectF bounds(points.first(), QSizeF(0.0, 0.0));
    for (const QPointF& p : points) {
        bounds = bounds.united(QRectF(p, QSizeF(0.0, 0.0)));
    }
    return bounds.normalized();
}

QRectF entitySnapBounds(const CadEntity* e)
{
    if (!e) return QRectF();

    switch (e->type()) {
    case CadEntity::Type::Line: {
        const auto* l = dynamic_cast<const CadLine*>(e);
        return l ? QRectF(l->start(), l->end()).normalized() : QRectF();
    }
    case CadEntity::Type::Circle: {
        const auto* c = dynamic_cast<const CadCircle*>(e);
        if (!c || c->radius() <= 0.0) return QRectF();
        const double r = c->radius();
        return QRectF(c->center().x() - r, c->center().y() - r, 2.0 * r, 2.0 * r);
    }
    case CadEntity::Type::Rectangle: {
        const auto* r = dynamic_cast<const CadRectangle*>(e);
        return r ? r->rect().normalized() : QRectF();
    }
    case CadEntity::Type::Polyline: {
        const auto* pl = dynamic_cast<const CadPolyline*>(e);
        return pl ? pointsBounds(pl->points()) : QRectF();
    }
    case CadEntity::Type::Arc: {
        const auto* a = dynamic_cast<const CadArc*>(e);
        if (!a || a->radius() <= 0.0) return QRectF();
        const double r = a->radius();
        return QRectF(a->center().x() - r, a->center().y() - r, 2.0 * r, 2.0 * r);
    }
    case CadEntity::Type::Ellipse: {
        const auto* el = dynamic_cast<const CadEllipse*>(e);
        return el ? el->rect().normalized() : QRectF();
    }
    case CadEntity::Type::Polygon: {
        const auto* pg = dynamic_cast<const CadPolygon*>(e);
        if (!pg || pg->radius() <= 0.0) return QRectF();
        const double r = pg->radius();
        return QRectF(pg->center().x() - r, pg->center().y() - r, 2.0 * r, 2.0 * r);
    }
    case CadEntity::Type::Hatch: {
        const auto* h = dynamic_cast<const CadHatch*>(e);
        if (!h) return QRectF();
        QRectF b = pointsBounds(h->boundary());
        for (const QVector<QPointF>& loop : h->loops()) {
            const QRectF lb = pointsBounds(loop);
            if (lb.isValid() && !lb.isNull()) b = (b.isValid() && !b.isNull()) ? b.united(lb) : lb;
        }
        return b.normalized();
    }
    case CadEntity::Type::LinearDimension: {
        const auto* d = dynamic_cast<const CadLinearDimension*>(e);
        if (!d) return QRectF();
        QVector<QPointF> pts{d->first(), d->second(), d->dimensionPoint()};
        return pointsBounds(pts);
    }
    case CadEntity::Type::Leader: {
        const auto* l = dynamic_cast<const CadLeader*>(e);
        if (!l) return QRectF();
        return QRectF(l->arrowPoint(), l->textPoint()).normalized();
    }
    case CadEntity::Type::Text: {
        const auto* t = dynamic_cast<const CadText*>(e);
        if (!t) return QRectF();
        const double h = std::max(1.0e-6, t->height());
        return QRectF(t->position().x() - h, t->position().y() - h, h * 2.0, h * 2.0);
    }
    case CadEntity::Type::BlockReference: {
        const auto* b = dynamic_cast<const CadBlockReference*>(e);
        if (!b) return QRectF();
        const double s = std::max(1.0, std::abs(b->scaleFactor()));
        return QRectF(b->insertionPoint().x() - s, b->insertionPoint().y() - s, s * 2.0, s * 2.0);
    }
    }

    return QRectF();
}



double normalizedArcSpanAbs(double spanDeg)
{
    double span = std::abs(spanDeg);
    while (span > 360.0) span -= 360.0;
    return span;
}

double entityPickDistance(const CadEntity* e, const QPointF& p, double tolerance)
{
    if (!e) return std::numeric_limits<double>::max();

    auto polylineDistance = [&](const QVector<QPointF>& pts, bool closed) -> double {
        if (pts.isEmpty()) return std::numeric_limits<double>::max();
        double best = std::numeric_limits<double>::max();
        for (int i = 1; i < pts.size(); ++i) {
            best = std::min(best, pointToSegmentDistance(p, pts.at(i - 1), pts.at(i)));
        }
        if (closed && pts.size() > 2) {
            best = std::min(best, pointToSegmentDistance(p, pts.last(), pts.first()));
        }
        return best;
    };

    switch (e->type()) {
    case CadEntity::Type::Line: {
        const auto* l = dynamic_cast<const CadLine*>(e);
        return l ? pointToSegmentDistance(p, l->start(), l->end()) : std::numeric_limits<double>::max();
    }
    case CadEntity::Type::Circle: {
        const auto* c = dynamic_cast<const CadCircle*>(e);
        if (!c || c->radius() <= 1.0e-9) return std::numeric_limits<double>::max();
        return std::abs(distanceBetween(p, c->center()) - c->radius());
    }
    case CadEntity::Type::Rectangle: {
        const auto* r = dynamic_cast<const CadRectangle*>(e);
        if (!r) return std::numeric_limits<double>::max();
        const QRectF rr = r->rect().normalized();
        QVector<QPointF> pts{rr.topLeft(), rr.topRight(), rr.bottomRight(), rr.bottomLeft()};
        return polylineDistance(pts, true);
    }
    case CadEntity::Type::Polyline: {
        const auto* pl = dynamic_cast<const CadPolyline*>(e);
        return pl ? polylineDistance(pl->points(), pl->closed()) : std::numeric_limits<double>::max();
    }
    case CadEntity::Type::Arc: {
        const auto* a = dynamic_cast<const CadArc*>(e);
        if (!a || a->radius() <= 1.0e-9) return std::numeric_limits<double>::max();
        const double angle = pointAngleDeg(a->center(), p);
        const double start = normalizedAngle(a->startAngleDeg());
        const double span = a->spanAngleDeg();
        const double absSpan = normalizedArcSpanAbs(span);
        const bool fullLike = absSpan >= 360.0 - 1.0e-6;
        bool onArc = fullLike;
        if (!onArc) {
            if (span >= 0.0) {
                onArc = angleOnCcwArc(start, absSpan, angle);
            } else {
                onArc = angleOnCcwArc(normalizedAngle(start + span), absSpan, angle);
            }
        }
        if (!onArc) return std::numeric_limits<double>::max();
        return std::abs(distanceBetween(p, a->center()) - a->radius());
    }
    case CadEntity::Type::Ellipse: {
        const auto* el = dynamic_cast<const CadEllipse*>(e);
        if (!el) return std::numeric_limits<double>::max();
        const QRectF r = el->rect().normalized();
        const double rx = r.width() * 0.5;
        const double ry = r.height() * 0.5;
        if (rx <= 1.0e-9 || ry <= 1.0e-9) return std::numeric_limits<double>::max();
        const QPointF c = r.center();
        const double nx = (p.x() - c.x()) / rx;
        const double ny = (p.y() - c.y()) / ry;
        const double len = std::sqrt(nx * nx + ny * ny);
        if (len <= 1.0e-12) return std::min(rx, ry);
        const QPointF boundary(c.x() + rx * nx / len, c.y() + ry * ny / len);
        return distanceBetween(p, boundary);
    }
    case CadEntity::Type::Polygon: {
        const auto* pg = dynamic_cast<const CadPolygon*>(e);
        if (!pg || pg->radius() <= 1.0e-9 || pg->sides() < 3) return std::numeric_limits<double>::max();
        QVector<QPointF> pts;
        const int n = std::max(3, pg->sides());
        const double base = qDegreesToRadians(pg->rotationDeg());
        for (int i = 0; i < n; ++i) {
            const double a = base + 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
            pts.append(QPointF(pg->center().x() + pg->radius() * std::cos(a),
                               pg->center().y() + pg->radius() * std::sin(a)));
        }
        return polylineDistance(pts, true);
    }
    case CadEntity::Type::Hatch: {
        const auto* h = dynamic_cast<const CadHatch*>(e);
        if (!h) return std::numeric_limits<double>::max();
        double best = polylineDistance(h->boundary(), true);
        for (const QVector<QPointF>& loop : h->loops()) best = std::min(best, polylineDistance(loop, true));
        return best;
    }
    case CadEntity::Type::LinearDimension: {
        const auto* d = dynamic_cast<const CadLinearDimension*>(e);
        if (!d) return std::numeric_limits<double>::max();
        return std::min({pointToSegmentDistance(p, d->first(), d->second()),
                         pointToSegmentDistance(p, d->first(), d->dimensionPoint()),
                         pointToSegmentDistance(p, d->second(), d->dimensionPoint())});
    }
    case CadEntity::Type::Leader: {
        const auto* l = dynamic_cast<const CadLeader*>(e);
        return l ? pointToSegmentDistance(p, l->arrowPoint(), l->textPoint()) : std::numeric_limits<double>::max();
    }
    case CadEntity::Type::Text: {
        const auto* t = dynamic_cast<const CadText*>(e);
        if (!t) return std::numeric_limits<double>::max();
        const double h = std::max(1.0e-6, t->height());
        const QRectF r(t->position().x() - h, t->position().y() - h, h * 2.0, h * 2.0);
        return r.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p) ? 0.0 : std::numeric_limits<double>::max();
    }
    case CadEntity::Type::BlockReference: {
        const auto* b = dynamic_cast<const CadBlockReference*>(e);
        return b ? distanceBetween(p, b->insertionPoint()) : std::numeric_limits<double>::max();
    }
    }

    return std::numeric_limits<double>::max();
}



QPainterPath entitySelectionPath(const CadEntity* e)
{
    QPainterPath path;
    if (!e) return path;

    auto addPolyline = [&](const QVector<QPointF>& pts, bool closed) {
        if (pts.isEmpty()) return;
        path.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i) path.lineTo(pts.at(i));
        if (closed && pts.size() > 2) path.closeSubpath();
    };

    switch (e->type()) {
    case CadEntity::Type::Line: {
        const auto* l = dynamic_cast<const CadLine*>(e);
        if (l) { path.moveTo(l->start()); path.lineTo(l->end()); }
        break;
    }
    case CadEntity::Type::Circle: {
        const auto* c = dynamic_cast<const CadCircle*>(e);
        if (c && c->radius() > 0.0) {
            const double r = c->radius();
            path.addEllipse(QRectF(c->center().x() - r, c->center().y() - r, 2.0 * r, 2.0 * r));
        }
        break;
    }
    case CadEntity::Type::Rectangle: {
        const auto* r = dynamic_cast<const CadRectangle*>(e);
        if (r) path.addRect(r->rect().normalized());
        break;
    }
    case CadEntity::Type::Polyline: {
        const auto* pl = dynamic_cast<const CadPolyline*>(e);
        if (pl) addPolyline(pl->points(), pl->closed());
        break;
    }
    case CadEntity::Type::Arc: {
        const auto* a = dynamic_cast<const CadArc*>(e);
        if (a && a->radius() > 0.0) {
            const double r = a->radius();
            const QRectF rr(a->center().x() - r, a->center().y() - r, 2.0 * r, 2.0 * r);
            path.arcMoveTo(rr, a->startAngleDeg());
            path.arcTo(rr, a->startAngleDeg(), a->spanAngleDeg());
        }
        break;
    }
    case CadEntity::Type::Ellipse: {
        const auto* el = dynamic_cast<const CadEllipse*>(e);
        if (el) path.addEllipse(el->rect().normalized());
        break;
    }
    case CadEntity::Type::Polygon: {
        const auto* pg = dynamic_cast<const CadPolygon*>(e);
        if (pg && pg->radius() > 0.0 && pg->sides() >= 3) {
            QVector<QPointF> pts;
            const int n = qMax(3, pg->sides());
            const double base = qDegreesToRadians(pg->rotationDeg());
            pts.reserve(n);
            for (int i = 0; i < n; ++i) {
                const double a = base + 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
                pts.append(QPointF(pg->center().x() + pg->radius() * std::cos(a),
                                   pg->center().y() + pg->radius() * std::sin(a)));
            }
            addPolyline(pts, true);
        }
        break;
    }
    case CadEntity::Type::Hatch: {
        const auto* h = dynamic_cast<const CadHatch*>(e);
        if (h) {
            addPolyline(h->boundary(), true);
            for (const QVector<QPointF>& loop : h->loops()) addPolyline(loop, true);
        }
        break;
    }
    case CadEntity::Type::LinearDimension: {
        const auto* d = dynamic_cast<const CadLinearDimension*>(e);
        if (d) {
            path.moveTo(d->first()); path.lineTo(d->dimensionPoint());
            path.moveTo(d->second()); path.lineTo(d->dimensionPoint());
            path.moveTo(d->first()); path.lineTo(d->second());
        }
        break;
    }
    case CadEntity::Type::Leader: {
        const auto* l = dynamic_cast<const CadLeader*>(e);
        if (l) { path.moveTo(l->arrowPoint()); path.lineTo(l->textPoint()); }
        break;
    }
    case CadEntity::Type::Text: {
        const auto* t = dynamic_cast<const CadText*>(e);
        if (t) {
            const double h = std::max(1.0e-6, t->height());
            path.addRect(QRectF(t->position().x() - h, t->position().y() - h, h * 2.0, h * 2.0));
        }
        break;
    }
    case CadEntity::Type::BlockReference: {
        const auto* b = dynamic_cast<const CadBlockReference*>(e);
        if (b) {
            const double s = std::max(1.0, std::abs(b->scaleFactor()));
            const QPointF p = b->insertionPoint();
            path.moveTo(QPointF(p.x() - s, p.y())); path.lineTo(QPointF(p.x() + s, p.y()));
            path.moveTo(QPointF(p.x(), p.y() - s)); path.lineTo(QPointF(p.x(), p.y() + s));
        }
        break;
    }
    }

    return path;
}

bool segmentIntersection(const Segment& s1, const Segment& s2, QPointF& out)
{
    const double x1 = s1.a.x();
    const double y1 = s1.a.y();
    const double x2 = s1.b.x();
    const double y2 = s1.b.y();
    const double x3 = s2.a.x();
    const double y3 = s2.a.y();
    const double x4 = s2.b.x();
    const double y4 = s2.b.y();

    const double den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(den) < 1e-9) return false;

    const double px = ((x1 * y2 - y1 * x2) * (x3 - x4) -
                       (x1 - x2) * (x3 * y4 - y3 * x4)) / den;
    const double py = ((x1 * y2 - y1 * x2) * (y3 - y4) -
                       (y1 - y2) * (x3 * y4 - y3 * x4)) / den;

    auto between = [](double value, double a, double b) {
        return value >= std::min(a, b) - 1e-7 && value <= std::max(a, b) + 1e-7;
    };

    if (!between(px, x1, x2) || !between(py, y1, y2) ||
        !between(px, x3, x4) || !between(py, y3, y4)) {
        return false;
    }

    out = QPointF(px, py);
    return true;
}
}

GraphicsView::GraphicsView(QWidget* parent)
    : QGraphicsView(parent)
{
    // Use the standard raster QWidget viewport by default.
    // The previous default QOpenGLWidget viewport can fail on some Linux/X11
    // systems with: QOpenGLContext::makeCurrent() called with non-opengl surface.
    // To test the old OpenGL viewport manually, set DWGVIEWER_USE_OPENGL_VIEWPORT=1
    // and add the QOpenGLWidget code back, but raster is safer and compatible.
    setViewport(new QWidget(this));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setAttribute(Qt::WA_NoSystemBackground, true);

    setScene(new QGraphicsScene(this));
    scene()->setItemIndexMethod(QGraphicsScene::BspTreeIndex);

    setCacheMode(QGraphicsView::CacheBackground);
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);

    // Défaut rapide comme l'ancienne version: l'antialiasing complet est enabled
    // seulement hors mode gros fichier. Sur les grands DWG/DXF il dégrade fortement
    // le zoom/pan.
    setRenderHints(QPainter::TextAntialiasing);

    // Fond CAD plus profond: les couleurs DXF paraissent plus vives et le dessin est plus lisible.
    setBackgroundBrush(CadRenderStyle::backgroundColor());

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_navigationRestoreTimer = new QTimer(this);
    m_navigationRestoreTimer->setSingleShot(true);
    connect(m_navigationRestoreTimer, &QTimer::timeout, this, &GraphicsView::endInteractiveNavigation);

    m_textEditBlinkTimer = new QTimer(this);
    m_textEditBlinkTimer->setInterval(450);
    connect(m_textEditBlinkTimer, &QTimer::timeout, this, [this]() {
        if (!m_editingTextItem) {
            m_textEditBlinkTimer->stop();
            return;
        }
        m_editingTextBlinkVisible = !m_editingTextBlinkVisible;
        m_editingTextItem->setOpacity(m_editingTextBlinkVisible ? m_editingTextOriginalOpacity : 0.20);
        viewport()->update();
    });

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { updateTextInlineEditorGeometry(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { updateTextInlineEditorGeometry(); });
}

void GraphicsView::setDocument(CadDocument* document)
{
    m_document = document;
    m_documentSelectionIndices.clear();
    if (scene() && m_document) {
        m_cachedContentBounds = safeRectForView(m_document->limits());
        scene()->setSceneRect(m_cachedContentBounds.adjusted(-20.0, -20.0, 20.0, 20.0));
    }
}

void GraphicsView::setDrawingTool(DrawingTool tool)
{
    if (m_textInlineEditor) finishTextInlineEdit(true);
    m_zoomWindowActive = false;
    m_zoomWindowDragging = false;
    cancelDrawing();
    clearDimensionPickMarkers();
    m_drawingTool = tool;
    if (tool == DrawingTool::Noe) {
        clearFixedAngleConstraint();
    }
    setCursor(tool == DrawingTool::Noe ? Qt::ArrowCursor : Qt::CrossCursor);
    setDragMode(tool == DrawingTool::Noe ? QGraphicsView::RubberBandDrag : QGraphicsView::NoDrag);
    logDrawEvent(QStringLiteral("OUTIL %1").arg(drawingToolName(tool)));
    if (m_fixedAngleConstraintEnabled) {
        logDrawEvent(QStringLiteral("Contrainte angle active: %1 (%2°)")
                         .arg(m_fixedAngleConstraintLabel.isEmpty() ? QStringLiteral("QCAD Draw") : m_fixedAngleConstraintLabel)
                         .arg(m_fixedAngleConstraintDeg, 0, 'f', 4));
    }
    emitDrawPrompt();
}

void GraphicsView::setFixedAngleConstraint(bool enabled, double angleDeg, const QString& label)
{
    m_fixedAngleConstraintEnabled = enabled;
    m_fixedAngleConstraintDeg = angleDeg;
    m_fixedAngleConstraintLabel = label;
    if (enabled) {
        logDrawEvent(QStringLiteral("QCAD Draw compatibility: contrainte angle %1° %2")
                         .arg(angleDeg, 0, 'f', 4)
                         .arg(label));
    } else {
        logDrawEvent(QStringLiteral("Contrainte angle disablede."));
    }
    emitDrawPrompt();
}

void GraphicsView::clearFixedAngleConstraint()
{
    m_fixedAngleConstraintEnabled = false;
    m_fixedAngleConstraintDeg = 0.0;
    m_fixedAngleConstraintLabel.clear();
}

QString GraphicsView::drawingToolName(DrawingTool tool)
{
    switch (tool) {
    case DrawingTool::Noe: return QStringLiteral("SELECT");
    case DrawingTool::Line: return QStringLiteral("LINE");
    case DrawingTool::Polyline: return QStringLiteral("PLINE");
    case DrawingTool::Circle: return QStringLiteral("CIRCLE");
    case DrawingTool::CircleDiameter: return QStringLiteral("CIRCLE_DIAMETER");
    case DrawingTool::Circle3Points: return QStringLiteral("CIRCLE_3P");
    case DrawingTool::Arc3Points: return QStringLiteral("ARC_3P");
    case DrawingTool::Rectangle: return QStringLiteral("RECTANGLE");
    case DrawingTool::Ellipse: return QStringLiteral("ELLIPSE");
    case DrawingTool::Polygon: return QStringLiteral("POLYGON");
    case DrawingTool::Spline: return QStringLiteral("SPLINE");
    case DrawingTool::Text: return QStringLiteral("TEXT");
    case DrawingTool::LinearDimension: return QStringLiteral("DIMLINEAR");
    case DrawingTool::Leader: return QStringLiteral("LEADER");
    case DrawingTool::Hatch: return QStringLiteral("HATCH");
    }
    return QStringLiteral("UNKNOWN");
}

void GraphicsView::logDrawEvent(const QString& message)
{
    emit drawEventLogged(message);
}

QString GraphicsView::currentDrawPrompt() const
{
    if (m_zoomWindowActive) {
        return m_zoomWindowDragging
            ? QStringLiteral("ZOOM WINDOW - release the mouse to apply zoom; Esc cancels")
            : QStringLiteral("ZOOM WINDOW - click-drag a zoom window; Esc cancels");
    }
    if (!m_interactiveModifyCommand.isEmpty()) return interactiveModifyStagePrompt();
    auto coordHelp = QStringLiteral("Coordinates: x,y | relative: @dx,dy | polar: @distance<angle | distance: 100");
    if (m_fixedAngleConstraintEnabled) {
        coordHelp += QStringLiteral(" | locked angle: %1°").arg(m_fixedAngleConstraintDeg, 0, 'f', 2);
    }
    switch (m_drawingTool) {
    case DrawingTool::Noe:
        return QStringLiteral("Commande ou outil: LINE, PLINE, CIRCLE, RECTANGLE, TEXT, DIM, HATCH...");
    case DrawingTool::Line:
        return m_drawing ? QStringLiteral("LINE - second point | length 100 | @100<45 | A 45 | U undo") : QStringLiteral("LINE - premier point: clic, x,y, @dx,dy ou @dist<angle");
    case DrawingTool::Polyline:
    case DrawingTool::Spline:
        return m_drawing ? QStringLiteral("%1 - point suivant, distance, C/close, Enter/fin, Backspace").arg(drawingToolName(m_drawingTool)) : QStringLiteral("%1 - premier point").arg(drawingToolName(m_drawingTool));
    case DrawingTool::Hatch:
        return m_drawing ? QStringLiteral("HATCH - point contour suivant, C/close, Enter/fin") : QStringLiteral("HATCH - premier point contour");
    case DrawingTool::Circle:
        return m_drawing ? QStringLiteral("CIRCLE - radius: point, R 50, DIA 100 or distance") : QStringLiteral("CIRCLE - center: click or coordinates");
    case DrawingTool::CircleDiameter:
        return m_drawing ? QStringLiteral("CIRCLE_DIAMETER - second endpoint or length") : QStringLiteral("CIRCLE_DIAMETER - first endpoint");
    case DrawingTool::Circle3Points:
        return QStringLiteral("CIRCLE_3P - point %1/3. %2").arg(m_points.size() + 1).arg(coordHelp);
    case DrawingTool::Arc3Points:
        return QStringLiteral("ARC_3P - point %1/3. %2").arg(m_points.size() + 1).arg(coordHelp);
    case DrawingTool::Rectangle:
        return m_drawing ? QStringLiteral("RECTANGLE - second corner, @width,height, x,y or distance") : QStringLiteral("RECTANGLE - first corner: click or coordinates");
    case DrawingTool::Ellipse:
        return m_drawing ? QStringLiteral("ELLIPSE - second box corner") : QStringLiteral("ELLIPSE - first box corner");
    case DrawingTool::Polygon:
        return m_drawing ? QStringLiteral("POLYGON - radius/vertex") : QStringLiteral("POLYGON - center");
    case DrawingTool::Text:
        return QStringLiteral("TEXT - insertion point. Type: TEXT your text to change the content");
    case DrawingTool::LinearDimension:
        return m_points.size() < 2
            ? QStringLiteral("DIMLINEAR - origin %1/2: internal OSNAP auto-reference; small yellow square = detected point").arg(m_points.size() + 1)
            : QStringLiteral("DIMLINEAR - dimension position: click the dimension location");
    case DrawingTool::Leader:
        return QStringLiteral("LEADER - point %1/2").arg(m_points.size() + 1);
    }
    return coordHelp;
}

void GraphicsView::emitDrawPrompt()
{
    emit drawPromptChanged(currentDrawPrompt());
}

QString GraphicsView::activeCommandName() const
{
    if (m_zoomWindowActive) return QStringLiteral("ZOOM_WINDOW");
    return drawingToolName(m_drawingTool);
}

void GraphicsView::repeatLastDrawCommand()
{
    if (m_lastDrawCommand.trimmed().isEmpty()) {
        logDrawEvent(QStringLiteral("No previous DRAW command to repeat."));
        emitDrawPrompt();
        return;
    }
    logDrawEvent(QStringLiteral("Repeating command: %1").arg(m_lastDrawCommand));
    processDrawConsoleCommand(m_lastDrawCommand);
}

QPointF GraphicsView::commandAnchorPoint() const
{
    if (m_drawing) {
        if (!m_points.isEmpty()) return m_points.last();
        return m_drawStart;
    }
    if (m_hasLastCursorScenePos) return m_lastCursorScenePos;
    return QPointF(0.0, 0.0);
}

void GraphicsView::undoLastCommandPoint()
{
    const bool listTool = m_drawingTool == DrawingTool::Polyline ||
                          m_drawingTool == DrawingTool::Spline ||
                          m_drawingTool == DrawingTool::Hatch;
    const bool sequenceTool = m_drawingTool == DrawingTool::CircleDiameter ||
                              m_drawingTool == DrawingTool::Circle3Points ||
                              m_drawingTool == DrawingTool::Arc3Points ||
                              m_drawingTool == DrawingTool::LinearDimension ||
                              m_drawingTool == DrawingTool::Leader;

    if ((listTool || sequenceTool) && m_drawing && !m_points.isEmpty()) {
        m_points.removeLast();
        logDrawEvent(QStringLiteral("UNDO point: last point removed."));
        if (m_points.isEmpty()) {
            m_drawing = false;
            clearPreviewItem();
        } else {
            updatePreviewItem(m_points.last());
        }
        emitDrawPrompt();
        return;
    }

    if (m_drawing) {
        logDrawEvent(QStringLiteral("UNDO point: first point canceled."));
        m_drawing = false;
        clearPreviewItem();
        emitDrawPrompt();
        return;
    }

    logDrawEvent(QStringLiteral("UNDO point: no active point in the command."));
    emitDrawPrompt();
}

bool GraphicsView::processConsoleAngleDistance(double angleDeg, double distance)
{
    if (distance <= 0.0) {
        logDrawEvent(QStringLiteral("Invalid distance: %1").arg(distance));
        return true;
    }
    const QPointF anchor = commandAnchorPoint();
    const double a = qDegreesToRadians(angleDeg);
    const QPointF p(anchor.x() + distance * std::cos(a),
                    anchor.y() + distance * std::sin(a));
    logDrawEvent(QStringLiteral("Point angle/distance: angle=%1 distance=%2 depuis X=%3 Y=%4 -> X=%5 Y=%6")
                     .arg(angleDeg, 0, 'f', 4)
                     .arg(distance, 0, 'f', 4)
                     .arg(anchor.x(), 0, 'f', 4)
                     .arg(anchor.y(), 0, 'f', 4)
                     .arg(p.x(), 0, 'f', 4)
                     .arg(p.y(), 0, 'f', 4));
    return processConsolePoint(p);
}

bool GraphicsView::parseConsolePoint(const QString& text, QPointF& point, bool& relative) const
{
    QString t = text.trimmed();
    relative = false;
    if (t.startsWith(QLatin1Char('@'))) {
        relative = true;
        t.remove(0, 1);
    }
    t.remove(QLatin1Char(' '));
    const QRegularExpression re(QStringLiteral(R"(^([-+]?(?:\d+(?:\.\d*)?|\.\d+))(?:[,;])([-+]?(?:\d+(?:\.\d*)?|\.\d+))$)"));
    const QRegularExpressionMatch m = re.match(t);
    if (!m.hasMatch()) return false;
    bool okX = false, okY = false;
    const double x = m.captured(1).toDouble(&okX);
    const double y = m.captured(2).toDouble(&okY);
    if (!okX || !okY) return false;
    point = QPointF(x, y);
    return true;
}

bool GraphicsView::parseConsolePolar(const QString& text, double& distance, double& angleDeg) const
{
    QString t = text.trimmed();
    if (t.startsWith(QLatin1Char('@'))) t.remove(0, 1);
    t.remove(QLatin1Char(' '));
    const QRegularExpression re(QStringLiteral(R"(^([-+]?(?:\d+(?:\.\d*)?|\.\d+))<([-+]?(?:\d+(?:\.\d*)?|\.\d+))$)"));
    const QRegularExpressionMatch m = re.match(t);
    if (!m.hasMatch()) return false;
    bool okD = false, okA = false;
    distance = m.captured(1).toDouble(&okD);
    angleDeg = m.captured(2).toDouble(&okA);
    return okD && okA && distance > 0.0;
}

bool GraphicsView::processConsolePoint(const QPointF& scenePos)
{
    auto isPointListTool = [this]() {
        return m_drawingTool == DrawingTool::Polyline ||
               m_drawingTool == DrawingTool::Spline ||
               m_drawingTool == DrawingTool::Hatch;
    };
    auto isTwoPointTool = [this]() {
        return m_drawingTool == DrawingTool::Line ||
               m_drawingTool == DrawingTool::Circle ||
               m_drawingTool == DrawingTool::Rectangle ||
               m_drawingTool == DrawingTool::Ellipse ||
               m_drawingTool == DrawingTool::Polygon;
    };
    auto isClickSequenceTool = [this]() {
        return m_drawingTool == DrawingTool::CircleDiameter ||
               m_drawingTool == DrawingTool::Circle3Points ||
               m_drawingTool == DrawingTool::Arc3Points ||
               m_drawingTool == DrawingTool::LinearDimension ||
               m_drawingTool == DrawingTool::Leader;
    };

    if (m_drawingTool == DrawingTool::Noe) {
        logDrawEvent(QStringLiteral("No active tool: type LINE, PLINE, CIRCLE, RECTANGLE..."));
        emitDrawPrompt();
        return true;
    }

    if (m_drawingTool == DrawingTool::Text) {
        addCadEntity(makeCurrentTextEntity(scenePos));
        logDrawEvent(QStringLiteral("TEXT placed at X=%1 Y=%2").arg(scenePos.x(), 0, 'f', 4).arg(scenePos.y(), 0, 'f', 4));
        emitDrawPrompt();
        return true;
    }

    if (isPointListTool()) {
        if (!m_drawing) {
            m_drawing = true;
            m_points.clear();
        }
        m_points.append(scenePos);
        updatePreviewItem(scenePos);
        logDrawEvent(QStringLiteral("%1 point %2: X=%3 Y=%4")
                         .arg(drawingToolName(m_drawingTool))
                         .arg(m_points.size())
                         .arg(scenePos.x(), 0, 'f', 4)
                         .arg(scenePos.y(), 0, 'f', 4));
        emitDrawPrompt();
        return true;
    }

    if (isClickSequenceTool()) {
        if (!m_drawing) {
            m_drawing = true;
            m_points.clear();
        }
        m_points.append(scenePos);
        logDrawEvent(QStringLiteral("%1 point %2: X=%3 Y=%4")
                         .arg(drawingToolName(m_drawingTool))
                         .arg(m_points.size())
                         .arg(scenePos.x(), 0, 'f', 4)
                         .arg(scenePos.y(), 0, 'f', 4));
        finishClickSequenceIfReady();
        if (m_drawing) updatePreviewItem(scenePos);
        emitDrawPrompt();
        return true;
    }

    if (isTwoPointTool()) {
        if (!m_drawing) {
            m_drawing = true;
            m_drawStart = scenePos;
            updatePreviewItem(scenePos);
            logDrawEvent(QStringLiteral("%1 premier point: X=%2 Y=%3")
                             .arg(drawingToolName(m_drawingTool))
                             .arg(scenePos.x(), 0, 'f', 4)
                             .arg(scenePos.y(), 0, 'f', 4));
        } else {
            logDrawEvent(QStringLiteral("%1 second point: X=%2 Y=%3")
                             .arg(drawingToolName(m_drawingTool))
                             .arg(scenePos.x(), 0, 'f', 4)
                             .arg(scenePos.y(), 0, 'f', 4));
            finishDragDrawing(scenePos);
        }
        emitDrawPrompt();
        return true;
    }

    return false;
}

bool GraphicsView::processConsoleDistance(double distance)
{
    if (distance <= 0.0) {
        logDrawEvent(QStringLiteral("Invalid distance: %1").arg(distance));
        return true;
    }
    if (!m_drawing) {
        logDrawEvent(QStringLiteral("Distance received (%1), but no first point is defined.").arg(distance));
        emitDrawPrompt();
        return true;
    }

    QPointF anchor;
    if (m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch ||
        m_drawingTool == DrawingTool::CircleDiameter || m_drawingTool == DrawingTool::Circle3Points ||
        m_drawingTool == DrawingTool::Arc3Points || m_drawingTool == DrawingTool::LinearDimension ||
        m_drawingTool == DrawingTool::Leader) {
        anchor = m_points.isEmpty() ? m_drawStart : m_points.last();
    } else {
        anchor = m_drawStart;
    }

    QPointF direction(1.0, 0.0);
    if (m_fixedAngleConstraintEnabled) {
        const double a = qDegreesToRadians(m_fixedAngleConstraintDeg);
        direction = QPointF(std::cos(a), std::sin(a));
    } else if (m_hasLastCursorScenePos && distanceBetween(anchor, m_lastCursorScenePos) > 1.0e-9) {
        direction = m_lastCursorScenePos - anchor;
        const double len = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
        direction = QPointF(direction.x() / len, direction.y() / len);
    }

    const QPointF p(anchor.x() + direction.x() * distance,
                    anchor.y() + direction.y() * distance);
    logDrawEvent(QStringLiteral("Distance clavier %1 depuis X=%2 Y=%3 vers X=%4 Y=%5")
                     .arg(distance, 0, 'f', 4)
                     .arg(anchor.x(), 0, 'f', 4)
                     .arg(anchor.y(), 0, 'f', 4)
                     .arg(p.x(), 0, 'f', 4)
                     .arg(p.y(), 0, 'f', 4));
    return processConsolePoint(p);
}

bool GraphicsView::parseCommandOption(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty()) return false;
    const QString upper = text.toUpper();

    auto numericAfter = [&](const QString& prefix, double& value) -> bool {
        if (!upper.startsWith(prefix)) return false;
        bool ok = false;
        value = text.mid(prefix.size()).trimmed().toDouble(&ok);
        return ok;
    };

    if (upper == QStringLiteral("U") || upper == QStringLiteral("UNDOPOINT") || upper == QStringLiteral("BACK")) {
        undoLastCommandPoint();
        return true;
    }

    if (upper == QStringLiteral("C") || upper == QStringLiteral("CL") || upper == QStringLiteral("CLOSE")) {
        if ((m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) && m_drawing) {
            finishPolyline(true);
            logDrawEvent(QStringLiteral("Option C/CLOSE: %1 closed.").arg(drawingToolName(m_drawingTool)));
            emitDrawPrompt();
            return true;
        }
        return false;
    }

    if (upper == QStringLiteral("END") || upper == QStringLiteral("DONE") || upper == QStringLiteral("FIN")) {
        if ((m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) && m_drawing) {
            finishPolyline(false);
            logDrawEvent(QStringLiteral("Option END: %1 finished.").arg(drawingToolName(m_drawingTool)));
            emitDrawPrompt();
            return true;
        }
        return false;
    }

    if (upper.startsWith(QStringLiteral("TEXT ")) || upper.startsWith(QStringLiteral("MTEXT "))) {
        const int split = text.indexOf(QLatin1Char(' '));
        const QString newText = split >= 0 ? text.mid(split + 1).trimmed() : QString();
        if (!newText.isEmpty()) {
            setAnnotationText(newText);
            setDrawingTool(DrawingTool::Text);
            logDrawEvent(QStringLiteral("TEXT content set: %1").arg(m_annotationText));
            emitDrawPrompt();
            return true;
        }
    }

    double value = 0.0;
    if (numericAfter(QStringLiteral("HEIGHT "), value) || numericAfter(QStringLiteral("H "), value)) {
        if (value > 0.0) {
            setAnnotationTextHeight(value);
            logDrawEvent(QStringLiteral("Text/dimension height = %1").arg(value, 0, 'f', 4));
        }
        emitDrawPrompt();
        return true;
    }

    if (numericAfter(QStringLiteral("ANGLE "), value) || numericAfter(QStringLiteral("A "), value)) {
        setFixedAngleConstraint(true, value, QStringLiteral("Option ANGLE"));
        emitDrawPrompt();
        return true;
    }

    if (numericAfter(QStringLiteral("RADIUS "), value) || numericAfter(QStringLiteral("R "), value)) {
        if (m_drawingTool == DrawingTool::Circle && m_drawing) {
            return processConsoleDistance(value);
        }
        if (m_drawingTool == DrawingTool::CircleDiameter && m_drawing) {
            return processConsoleDistance(value * 2.0);
        }
        setDrawingTool(DrawingTool::Circle);
        logDrawEvent(QStringLiteral("Option RADIUS=%1. Specify the circle center.").arg(value, 0, 'f', 4));
        emitDrawPrompt();
        return true;
    }

    if (numericAfter(QStringLiteral("DIAMETER "), value) || numericAfter(QStringLiteral("DIA "), value)) {
        if (m_drawingTool == DrawingTool::Circle && m_drawing) {
            return processConsoleDistance(value * 0.5);
        }
        if (m_drawingTool == DrawingTool::CircleDiameter && m_drawing) {
            return processConsoleDistance(value);
        }
        setDrawingTool(DrawingTool::CircleDiameter);
        logDrawEvent(QStringLiteral("Option DIAMETER=%1. Specify the first diameter point.").arg(value, 0, 'f', 4));
        emitDrawPrompt();
        return true;
    }

    // AutoCAD/QCAD style: "A 45 100" or "ANGLE 45 100" creates the next point.
    {
        const QRegularExpression re(QStringLiteral(R"(^(?:A|ANGLE)\s+([-+]?(?:\d+(?:\.\d*)?|\.\d+))\s+([-+]?(?:\d+(?:\.\d*)?|\.\d+))$)"),
                                    QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = re.match(text);
        if (m.hasMatch()) {
            bool okA = false, okD = false;
            const double a = m.captured(1).toDouble(&okA);
            const double d = m.captured(2).toDouble(&okD);
            if (okA && okD) return processConsoleAngleDistance(a, d);
        }
    }

    return false;
}

bool GraphicsView::processDrawConsoleCommand(const QString& command)
{
    const QString raw = command.trimmed();
    if (raw.isEmpty()) return true;
    const QString upper = raw.toUpper();
    logDrawEvent(QStringLiteral("> %1").arg(raw));

    if (parseCommandOption(raw)) {
        return true;
    }

    auto setToolByCommand = [&](DrawingTool tool) {
        const QString commandName = upper.section(QLatin1Char(' '), 0, 0);
        if (!commandName.isEmpty()) m_lastDrawCommand = commandName;
        setDrawingTool(tool);
        logDrawEvent(QStringLiteral("COMMANDE active: %1").arg(drawingToolName(tool)));
        emitDrawPrompt();
        return true;
    };

    if (upper == QStringLiteral("ESC") || upper == QStringLiteral("CANCEL")) {
        cancelActiveAction();
        logDrawEvent(QStringLiteral("Command canceled."));
        emitDrawPrompt();
        return true;
    }
    if (upper == QStringLiteral("ZOOM") || upper == QStringLiteral("Z")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        logDrawEvent(QStringLiteral("ZOOM: wheel zooms to the mouse cursor; use ZW for window, ZE for extents."));
        return true;
    }
    if (upper == QStringLiteral("ZOOMEXTENTS") || upper == QStringLiteral("ZE") || upper == QStringLiteral("ZEXTENTS")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        zoomToExtents();
        return true;
    }
    if (upper == QStringLiteral("ZOOMIN") || upper == QStringLiteral("ZI")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        zoomByAt(1.10, viewport() ? viewport()->mapFromGlobal(QCursor::pos()) : QPoint());
        return true;
    }
    if (upper == QStringLiteral("ZOOMOUT") || upper == QStringLiteral("ZO")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        zoomByAt(1.0 / 1.10, viewport() ? viewport()->mapFromGlobal(QCursor::pos()) : QPoint());
        return true;
    }
    if (upper == QStringLiteral("PAN") || upper == QStringLiteral("P")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        logDrawEvent(QStringLiteral("PAN: hold the middle mouse button or Space+left button and drag."));
        return true;
    }
    if (upper == QStringLiteral("ZOOMWINDOW") || upper == QStringLiteral("ZW") || upper == QStringLiteral("ZOOMW")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        startZoomWindowMode();
        return true;
    }
    if (upper == QStringLiteral("LINE") || upper == QStringLiteral("L") || upper == QStringLiteral("LINE2P")) { clearFixedAngleConstraint(); return setToolByCommand(DrawingTool::Line); }
    if (upper == QStringLiteral("LINEH") || upper == QStringLiteral("LINEHORIZONTAL")) { setFixedAngleConstraint(true, 0.0, QStringLiteral("QCAD LineHorizontal")); return setToolByCommand(DrawingTool::Line); }
    if (upper == QStringLiteral("LINEV") || upper == QStringLiteral("LINEVERTICAL")) { setFixedAngleConstraint(true, 90.0, QStringLiteral("QCAD LineVertical")); return setToolByCommand(DrawingTool::Line); }
    if (upper.startsWith(QStringLiteral("LINEANGLE "))) { bool ok=false; const double a=raw.section(QLatin1Char(' '),1).trimmed().toDouble(&ok); if(ok){ setFixedAngleConstraint(true,a,QStringLiteral("QCAD LineAngle")); return setToolByCommand(DrawingTool::Line);} }
    if (upper == QStringLiteral("RAY") || upper == QStringLiteral("XLINE") || upper == QStringLiteral("LINEFREEHAND") || upper == QStringLiteral("LINEPARALLEL") || upper == QStringLiteral("LINEPARALLELTHROUGH") || upper == QStringLiteral("LINEORTHOGONAL") || upper == QStringLiteral("LINETANGENT1") || upper == QStringLiteral("LINETANGENT2") || upper == QStringLiteral("LINEBISECTOR")) {
        clearFixedAngleConstraint();
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        setDrawingTool(DrawingTool::Line);
        logDrawEvent(QStringLiteral("QCAD Draw compatibility: %1 mapped to 2-point LINE in DWGView.").arg(upper));
        return true;
    }
    if (upper == QStringLiteral("PLINE") || upper == QStringLiteral("PL")) return setToolByCommand(DrawingTool::Polyline);
    if (upper == QStringLiteral("SPLINE") || upper == QStringLiteral("SPL")) return setToolByCommand(DrawingTool::Spline);
    if (upper == QStringLiteral("CIRCLE") || upper == QStringLiteral("CIR") || upper == QStringLiteral("CIRCLECP") || upper == QStringLiteral("CIRCLECR")) return setToolByCommand(DrawingTool::Circle);
    if (upper == QStringLiteral("CIRCLED") || upper == QStringLiteral("DIAMETER") || upper == QStringLiteral("CIRCLE_DIAMETER") || upper == QStringLiteral("CIRCLE2P")) return setToolByCommand(DrawingTool::CircleDiameter);
    if (upper == QStringLiteral("CIRCLE3P") || upper == QStringLiteral("3P")) return setToolByCommand(DrawingTool::Circle3Points);
    if (upper == QStringLiteral("CIRCLE2PR") || upper == QStringLiteral("CIRCLE2TP") || upper == QStringLiteral("CIRCLE2TR") || upper == QStringLiteral("CIRCLE3T") || upper == QStringLiteral("CIRCLECD") || upper == QStringLiteral("CIRCLECONCENTRIC") || upper == QStringLiteral("CIRCLET2P") || upper == QStringLiteral("CIRCLETPR")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        setDrawingTool(DrawingTool::Circle);
        logDrawEvent(QStringLiteral("QCAD Draw compatibility: %1 mapped to center/radius CIRCLE; use tangent snaps if needed.").arg(upper));
        return true;
    }
    if (upper == QStringLiteral("ARC") || upper == QStringLiteral("A") || upper == QStringLiteral("ARC3P")) return setToolByCommand(DrawingTool::Arc3Points);
    if (upper.startsWith(QStringLiteral("ARC")) || upper == QStringLiteral("ARCTANGENTIAL")) {
        m_lastDrawCommand = upper.section(QLatin1Char(' '), 0, 0);
        setDrawingTool(DrawingTool::Arc3Points);
        logDrawEvent(QStringLiteral("QCAD Draw compatibility: %1 mapped to 3-point ARC.").arg(upper));
        return true;
    }
    if (upper == QStringLiteral("RECTANGLE") || upper == QStringLiteral("RECTANG") || upper == QStringLiteral("REC") || upper == QStringLiteral("SHAPERECTANGLEPP") || upper == QStringLiteral("SHAPERECTANGLESIZE")) return setToolByCommand(DrawingTool::Rectangle);
    if (upper == QStringLiteral("ELLIPSE") || upper == QStringLiteral("EL") || upper.startsWith(QStringLiteral("ELLIPSE"))) return setToolByCommand(DrawingTool::Ellipse);
    if (upper == QStringLiteral("POLYGON") || upper == QStringLiteral("POL") || upper.startsWith(QStringLiteral("SHAPEPOLYGON"))) return setToolByCommand(DrawingTool::Polygon);
    if (upper == QStringLiteral("TEXT") || upper == QStringLiteral("DTEXT") || upper == QStringLiteral("MTEXT")) return setToolByCommand(DrawingTool::Text);
    if (upper == QStringLiteral("DIM") || upper == QStringLiteral("DIMLINEAR") ||
        upper == QStringLiteral("DIMALIGNED") || upper == QStringLiteral("DIMROTATED") ||
        upper == QStringLiteral("DIMBASELINE") || upper == QStringLiteral("DIMCONTINUE") ||
        upper == QStringLiteral("DIMARC") || upper == QStringLiteral("DIMANGULAR") ||
        upper == QStringLiteral("DIMANGULAR3P") || upper == QStringLiteral("DIMANGULARARC")) {
        setDrawingTool(DrawingTool::LinearDimension);
        if (upper != QStringLiteral("DIM") && upper != QStringLiteral("DIMLINEAR") && upper != QStringLiteral("DIMALIGNED")) {
            logDrawEvent(QStringLiteral("QCAD Dimension compatibility: %1 mapped to visible linear dimension in DWGView.").arg(upper));
        }
        return true;
    }
    if (upper == QStringLiteral("DIMORDINATE") || upper == QStringLiteral("DIMORDINATEX") || upper == QStringLiteral("DIMORDINATEY") || upper == QStringLiteral("TOLERANCE")) {
        setDrawingTool(DrawingTool::Leader);
        logDrawEvent(QStringLiteral("QCAD Dimension compatibility: %1 mapped to LEADER/TEXT for visible annotation.").arg(upper));
        return true;
    }
    if (upper == QStringLiteral("DIMJOGGED")) {
        setDrawingTool(DrawingTool::LinearDimension);
        logDrawEvent(QStringLiteral("QCAD Dimension compatibility: DIMJOGGED mapped to visible radius/linear dimension."));
        return true;
    }
    if (upper == QStringLiteral("LEADER") || upper == QStringLiteral("MLEADER")) return setToolByCommand(DrawingTool::Leader);
    if (upper == QStringLiteral("HATCH") || upper == QStringLiteral("BHATCH") || upper == QStringLiteral("HATCHFROMSELECTION")) return setToolByCommand(DrawingTool::Hatch);
    if (upper == QStringLiteral("POINT") || upper == QStringLiteral("POINT1P")) { setAnnotationText(QStringLiteral("•")); setAnnotationTextHeight(std::max(1.0, m_annotationTextHeight)); return setToolByCommand(DrawingTool::Text); }
    if (upper == QStringLiteral("IMAGE")) { logDrawEvent(QStringLiteral("QCAD Draw/Image: utiliser File > attacher image / IMAGEATTACH.")); return true; }
    if (upper == QStringLiteral("SELECT")) return setToolByCommand(DrawingTool::Noe);

    if (upper.startsWith(QStringLiteral("TEXT "))) {
        setAnnotationText(raw.mid(5));
        logDrawEvent(QStringLiteral("Text courant = \"%1\"").arg(m_annotationText));
        emitDrawPrompt();
        return true;
    }
    if (upper.startsWith(QStringLiteral("HEIGHT ")) || upper.startsWith(QStringLiteral("TEXTHEIGHT "))) {
        const QString val = raw.section(QLatin1Char(' '), 1).trimmed();
        bool ok = false;
        const double h = val.toDouble(&ok);
        if (ok && h > 0.0) {
            setAnnotationTextHeight(h);
            logDrawEvent(QStringLiteral("Text/dimension height = %1").arg(h));
        } else {
            logDrawEvent(QStringLiteral("Invalid height: %1").arg(val));
        }
        emitDrawPrompt();
        return true;
    }
    if (upper.startsWith(QStringLiteral("SIDES "))) {
        bool ok = false;
        const int sides = raw.section(QLatin1Char(' '), 1).trimmed().toInt(&ok);
        if (ok) {
            setPolygonSides(sides);
            logDrawEvent(QStringLiteral("Polygon sides = %1").arg(m_polygonSides));
        }
        emitDrawPrompt();
        return true;
    }

    auto toggle = [&](const QString& name, bool value) {
        if (name == QStringLiteral("GRID")) setGridEnabled(value);
        else if (name == QStringLiteral("SNAP")) setSnapEnabled(value);
        else if (name == QStringLiteral("ORTHO")) setOrthoEnabled(value);
        else if (name == QStringLiteral("OSNAP")) setObjectSnapEnabled(value);
        logDrawEvent(QStringLiteral("%1 %2").arg(name, value ? QStringLiteral("ON") : QStringLiteral("OFF")));
        emitDrawPrompt();
        return true;
    };
    for (const QString& name : {QStringLiteral("GRID"), QStringLiteral("SNAP"), QStringLiteral("ORTHO"), QStringLiteral("OSNAP")}) {
        if (upper == name + QStringLiteral(" ON")) return toggle(name, true);
        if (upper == name + QStringLiteral(" OFF")) return toggle(name, false);
    }

    if ((upper == QStringLiteral("ENTER") || upper == QStringLiteral("END") || upper == QStringLiteral("DONE") || upper == QStringLiteral("FIN")) && m_drawing) {
        if (m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) {
            finishPolyline(false);
            logDrawEvent(QStringLiteral("%1 finished.").arg(drawingToolName(m_drawingTool)));
            emitDrawPrompt();
            return true;
        }
    }
    if ((upper == QStringLiteral("CLOSE") || upper == QStringLiteral("CL")) && m_drawing) {
        if (m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) {
            finishPolyline(true);
            logDrawEvent(QStringLiteral("%1 closed.").arg(drawingToolName(m_drawingTool)));
            emitDrawPrompt();
            return true;
        }
    }
    if (upper == QStringLiteral("BACK") || upper == QStringLiteral("UNDOPOINT")) {
        if ((m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) && m_drawing && !m_points.isEmpty()) {
            m_points.removeLast();
            logDrawEvent(QStringLiteral("Last point removed."));
            if (m_points.isEmpty()) cancelDrawing();
            else updatePreviewItem(m_points.last());
            emitDrawPrompt();
            return true;
        }
    }

    QString valueText = raw;
    const QString upperValue = valueText.toUpper();
    for (const QString& prefix : {QStringLiteral("LEN "), QStringLiteral("LENGTH "), QStringLiteral("DIST "), QStringLiteral("D ")}) {
        if (upperValue.startsWith(prefix)) {
            valueText = valueText.mid(prefix.size()).trimmed();
            break;
        }
    }

    double polarDistance = 0.0;
    double polarAngle = 0.0;
    if (parseConsolePolar(valueText, polarDistance, polarAngle)) {
        QPointF anchor;
        if (m_drawing) {
            anchor = (!m_points.isEmpty() ? m_points.last() : m_drawStart);
        } else {
            anchor = m_hasLastCursorScenePos ? m_lastCursorScenePos : QPointF(0.0, 0.0);
        }
        const double a = qDegreesToRadians(polarAngle);
        const QPointF p(anchor.x() + polarDistance * std::cos(a),
                        anchor.y() + polarDistance * std::sin(a));
        logDrawEvent(QStringLiteral("Point polaire: distance=%1 angle=%2 -> X=%3 Y=%4")
                         .arg(polarDistance, 0, 'f', 4)
                         .arg(polarAngle, 0, 'f', 4)
                         .arg(p.x(), 0, 'f', 4)
                         .arg(p.y(), 0, 'f', 4));
        return processConsolePoint(p);
    }

    QPointF point;
    bool relative = false;
    if (parseConsolePoint(valueText, point, relative)) {
        if (relative) {
            const QPointF anchor = m_drawing ? (!m_points.isEmpty() ? m_points.last() : m_drawStart)
                                             : (m_hasLastCursorScenePos ? m_lastCursorScenePos : QPointF(0.0, 0.0));
            point = anchor + point;
        }
        return processConsolePoint(point);
    }

    bool ok = false;
    const double distance = valueText.toDouble(&ok);
    if (ok) return processConsoleDistance(distance);

    return false;
}


void GraphicsView::setPolygonSides(int sides)
{
    m_polygonSides = qMax(3, sides);
}

void GraphicsView::setAnnotationText(const QString& text)
{
    m_annotationText = text.trimmed().isEmpty() ? QStringLiteral("Text") : text;
}

void GraphicsView::setAnnotationTextHeight(double height)
{
    if (height > 0.0) m_annotationTextHeight = height;
}

void GraphicsView::setAnnotationTextWidthFactor(double factor)
{
    m_annotationTextWidthFactor = qBound(0.01, factor, 100.0);
}

void GraphicsView::setAnnotationTextObliqueAngle(double angleDeg)
{
    m_annotationTextObliqueAngleDeg = qBound(-85.0, angleDeg, 85.0);
}

void GraphicsView::setAnnotationFontName(const QString& fontName)
{
    m_annotationFontName = fontName.trimmed().isEmpty() ? QStringLiteral("TXT") : fontName.trimmed();
}

void GraphicsView::setHatchPattern(const QString& pattern)
{
    m_hatchPattern = pattern.trimmed().isEmpty() ? QStringLiteral("ANSI31") : pattern.trimmed();
}

void GraphicsView::setHatchScale(double scale)
{
    m_hatchScale = qMax(0.01, scale);
}

void GraphicsView::setHatchAngleDeg(double angleDeg)
{
    m_hatchAngleDeg = angleDeg;
}

void GraphicsView::setGridEnabled(bool enabled)
{
    if (m_gridEnabled == enabled) return;
    m_gridEnabled = enabled;
    viewport()->update();
}

void GraphicsView::setSnapEnabled(bool enabled)
{
    m_snapEnabled = enabled;
}

void GraphicsView::setOrthoEnabled(bool enabled)
{
    m_orthoEnabled = enabled;
}

void GraphicsView::setObjectSnapEnabled(bool enabled)
{
    m_objectSnapEnabled = enabled;
    if (!enabled) setCurrentSnapMarker(nullptr);
}

void GraphicsView::setObjectSnapModeEnabled(ObjectSnapMode mode, bool enabled)
{
    switch (mode) {
    case ObjectSnapMode::Endpoint:     m_snapEndpointEnabled = enabled; break;
    case ObjectSnapMode::Midpoint:     m_snapMidpointEnabled = enabled; break;
    case ObjectSnapMode::Center:       m_snapCenterEnabled = enabled; break;
    case ObjectSnapMode::Quadrant:     m_snapQuadrantEnabled = enabled; break;
    case ObjectSnapMode::Intersection: m_snapIntersectionEnabled = enabled; break;
    case ObjectSnapMode::Tangent:      m_snapTangentEnabled = enabled; break;
    case ObjectSnapMode::Vertex:       m_snapVertexEnabled = enabled; break;
    }
    if (!enabled && m_hasCurrentSnapMarker && m_currentSnapCandidate.mode == mode) {
        setCurrentSnapMarker(nullptr);
    }
}

bool GraphicsView::objectSnapModeEnabled(ObjectSnapMode mode) const
{
    switch (mode) {
    case ObjectSnapMode::Endpoint:     return m_snapEndpointEnabled;
    case ObjectSnapMode::Midpoint:     return m_snapMidpointEnabled;
    case ObjectSnapMode::Center:       return m_snapCenterEnabled;
    case ObjectSnapMode::Quadrant:     return m_snapQuadrantEnabled;
    case ObjectSnapMode::Intersection: return m_snapIntersectionEnabled;
    case ObjectSnapMode::Tangent:      return m_snapTangentEnabled;
    case ObjectSnapMode::Vertex:       return m_snapVertexEnabled;
    }
    return false;
}

QString GraphicsView::objectSnapModeName(ObjectSnapMode mode)
{
    switch (mode) {
    case ObjectSnapMode::Endpoint:     return QStringLiteral("Endpoint");
    case ObjectSnapMode::Midpoint:     return QStringLiteral("Midpoint");
    case ObjectSnapMode::Center:       return QStringLiteral("Center");
    case ObjectSnapMode::Quadrant:     return QStringLiteral("Quadrant");
    case ObjectSnapMode::Intersection: return QStringLiteral("Intersection");
    case ObjectSnapMode::Tangent:      return QStringLiteral("Tangent");
    case ObjectSnapMode::Vertex:       return QStringLiteral("Vertex");
    }
    return QStringLiteral("Accrochage");
}

void GraphicsView::setGridSpacing(double spacing)
{
    if (spacing <= 0.0) return;
    m_gridSpacing = spacing;
    viewport()->update();
}

QPointF GraphicsView::gridSnappedPoint(const QPointF& point) const
{
    if (m_gridSpacing <= 0.0) return point;
    const double x = std::round(point.x() / m_gridSpacing) * m_gridSpacing;
    const double y = std::round(point.y() / m_gridSpacing) * m_gridSpacing;
    return QPointF(x, y);
}

QPointF GraphicsView::orthoConstrainedPoint(const QPointF& anchor, const QPointF& point) const
{
    const double dx = std::abs(point.x() - anchor.x());
    const double dy = std::abs(point.y() - anchor.y());
    if (dx >= dy) return QPointF(point.x(), anchor.y());
    return QPointF(anchor.x(), point.y());
}

QPointF GraphicsView::fixedAngleConstrainedPoint(const QPointF& anchor, const QPointF& point) const
{
    const double angle = qDegreesToRadians(m_fixedAngleConstraintDeg);
    const QPointF axis(std::cos(angle), std::sin(angle));
    const QPointF delta = point - anchor;
    double projection = delta.x() * axis.x() + delta.y() * axis.y();
    if (std::abs(projection) <= 1.0e-12) {
        projection = distanceBetween(anchor, point);
    }
    return QPointF(anchor.x() + axis.x() * projection,
                   anchor.y() + axis.y() * projection);
}


QVector<int> GraphicsView::nearbyEntityIndices(const QPointF& rawPoint, const QPointF* tangentAnchor) const
{
    QVector<int> result;
    if (!scene() || !m_document) return result;

    const double sceneTol = sceneToleranceFromView(this, m_objectSnapTolerancePx);
    const double queryRadius = std::max(sceneTol * 3.0, 1.0);

    auto collectAround = [&](const QPointF& p) {
        const QRectF queryRect(p.x() - queryRadius,
                               p.y() - queryRadius,
                               queryRadius * 2.0,
                               queryRadius * 2.0);

        // Fast path for normal documents: use the QGraphicsScene BSP index.
        // In huge imported DWG/DXF files, however, CadDocument::renderToScene
        // can replace thousands of editable items by FastCadBatchItem objects
        // (data(1) == -1). Those are visual-only and cannot serve object snap.
        const QList<QGraphicsItem*> nearItems = scene()->items(queryRect,
                                                               Qt::IntersectsItemBoundingRect,
                                                               Qt::DescendingOrder,
                                                               QTransform());
        for (QGraphicsItem* item : nearItems) {
            if (!item) continue;
            if (item->data(2).toBool()) continue; // locked layer

            bool ok = false;
            const int index = item->data(1).toInt(&ok);
            if (!ok || index < 0) continue;
            if (index >= static_cast<int>(m_document->entities().size())) continue;
            result.append(index);
        }
    };

    collectAround(rawPoint);
    if (tangentAnchor) {
        collectAround(*tangentAnchor);
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    // Robust snap fallback for existing external DWG/DXF files in batch mode.
    // When the scene contains only grouped paths, the scene index cannot return
    // individual entity IDs. We then search the CadDocument geometry itself in a
    // small CAD-space window around the cursor. This restores endpoint/midpoint/
    // center snapping while keeping the fast visual batches.
    const bool needsDocumentFallback = result.size() < 24 || (m_largeDocumentMode && m_document->entityCount() > 50000);
    if (needsDocumentFallback) {
        struct ScoredIndex { double score; int index; };
        QVector<ScoredIndex> scored;
        scored.reserve(512);

        auto scanAround = [&](const QPointF& p) {
            const QRectF queryRect(p.x() - queryRadius,
                                   p.y() - queryRadius,
                                   queryRadius * 2.0,
                                   queryRadius * 2.0);
            const auto& allEntities = m_document->entities();
            const auto& layers = m_document->layers();
            for (int i = 0; i < static_cast<int>(allEntities.size()); ++i) {
                const auto& e = allEntities[static_cast<size_t>(i)];
                if (!e) continue;
                const CadLayer layer = layers.value(e->layer(), layers.value(QStringLiteral("0")));
                if (!layer.visible || layer.locked) continue;

                QRectF b = entitySnapBounds(e.get());
                if (!b.isValid() || b.isNull()) continue;
                b = b.adjusted(-queryRadius, -queryRadius, queryRadius, queryRadius);
                if (!b.intersects(queryRect) && !b.contains(p)) continue;

                const QPointF c = b.center();
                const double dx = c.x() - p.x();
                const double dy = c.y() - p.y();
                scored.append({dx * dx + dy * dy, i});
            }
        };

        scanAround(rawPoint);
        if (tangentAnchor) scanAround(*tangentAnchor);

        std::sort(scored.begin(), scored.end(), [](const ScoredIndex& a, const ScoredIndex& b) {
            if (a.score == b.score) return a.index < b.index;
            return a.score < b.score;
        });

        constexpr int kMaxDocumentFallbackEntities = 3000;
        for (const ScoredIndex& si : scored) {
            if (!result.contains(si.index)) result.append(si.index);
            if (result.size() >= kMaxDocumentFallbackEntities) break;
        }

    }

    // Safety limit for drawings containing many long items whose bounding boxes
    // all cross the cursor. The closest visible candidates will still be tested,
    // while the UI stays responsive.
    constexpr int kMaxLocalEntitiesForOsnap = 3000;
    if (result.size() > kMaxLocalEntitiesForOsnap) {
        result.resize(kMaxLocalEntitiesForOsnap);
    }

    return result;
}

void GraphicsView::collectSnapCandidates(QVector<SnapCandidate>& candidates, const QPointF& rawPoint, const QPointF* tangentAnchor) const
{
    if (!m_document) return;

    const QPoint rawView = mapFromScene(rawPoint);
    const double sceneTol = sceneToleranceFromView(this, m_objectSnapTolerancePx);
    const double looseSceneTol = sceneTol * 4.0;

    int currentSnapEntityIndex = -1;
    auto addCandidate = [&](const QPointF& point, ObjectSnapMode mode, int forcedEntityIndex = std::numeric_limits<int>::min()) {
        if (!objectSnapModeEnabled(mode)) return;
        const int candidateEntityIndex = forcedEntityIndex == std::numeric_limits<int>::min()
                                           ? currentSnapEntityIndex
                                           : forcedEntityIndex;

        // Optimisation importante pour les gros DWG : on ne garde que les
        // points vraiment proches du curseur en pixels. Sans ce filtre,
        // l'accrochage parcourt des dizaines de milliers de points à chaque
        // déplacement souris et semble ne plus fonctionner.
        const QPoint candidateView = mapFromScene(point);
        const double dx = static_cast<double>(candidateView.x() - rawView.x());
        const double dy = static_cast<double>(candidateView.y() - rawView.y());
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= m_objectSnapTolerancePx) {
            candidates.append({point, mode, candidateEntityIndex});
        }
    };

    QVector<Segment> localSegments;
    QVector<CircleInfo> circlesNearCursor;
    QVector<CircleInfo> circlesNearAnchor;

    auto addSegment = [&](const QPointF& a, const QPointF& b) {
        if (distanceBetween(a, b) <= 1e-9) return;
        if (segmentNearPoint(rawPoint, a, b, looseSceneTol)) {
            localSegments.append({a, b});
        }
    };

    auto addCircle = [&](const QPointF& center, double radius) {
        if (radius <= 1e-9) return;
        const CircleInfo circle{center, radius};
        if (circleNearPoint(rawPoint, circle, looseSceneTol)) {
            circlesNearCursor.append(circle);
        }
        if (tangentAnchor && circleNearPoint(*tangentAnchor, circle, looseSceneTol)) {
            circlesNearAnchor.append(circle);
        }
    };

    const QVector<int> localEntityIndices = nearbyEntityIndices(rawPoint, tangentAnchor);
    if (localEntityIndices.isEmpty()) return;

    const auto& allEntities = m_document->entities();
    for (int entityIndex : localEntityIndices) {
        currentSnapEntityIndex = entityIndex;
        if (entityIndex < 0 || entityIndex >= static_cast<int>(allEntities.size())) continue;
        const auto& entityPtr = allEntities[static_cast<size_t>(entityIndex)];
        if (!entityPtr) continue;

        if (auto* line = dynamic_cast<CadLine*>(entityPtr.get())) {
            const QPointF a = line->start();
            const QPointF b = line->end();
            addCandidate(a, ObjectSnapMode::Endpoint);
            addCandidate(b, ObjectSnapMode::Endpoint);
            addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
            addSegment(a, b);
        } else if (auto* circle = dynamic_cast<CadCircle*>(entityPtr.get())) {
            const QPointF c = circle->center();
            const double r = circle->radius();
            addCandidate(c, ObjectSnapMode::Center);
            addCandidate(QPointF(c.x() + r, c.y()), ObjectSnapMode::Quadrant);
            addCandidate(QPointF(c.x() - r, c.y()), ObjectSnapMode::Quadrant);
            addCandidate(QPointF(c.x(), c.y() + r), ObjectSnapMode::Quadrant);
            addCandidate(QPointF(c.x(), c.y() - r), ObjectSnapMode::Quadrant);
            addCircle(c, r);
        } else if (auto* rect = dynamic_cast<CadRectangle*>(entityPtr.get())) {
            const QRectF r = rect->rect();
            const QPointF tl = r.topLeft();
            const QPointF tr = r.topRight();
            const QPointF bl = r.bottomLeft();
            const QPointF br = r.bottomRight();
            addCandidate(tl, ObjectSnapMode::Endpoint);
            addCandidate(tr, ObjectSnapMode::Endpoint);
            addCandidate(bl, ObjectSnapMode::Endpoint);
            addCandidate(br, ObjectSnapMode::Endpoint);
            addCandidate(QPointF(r.center().x(), r.top()), ObjectSnapMode::Midpoint);
            addCandidate(QPointF(r.center().x(), r.bottom()), ObjectSnapMode::Midpoint);
            addCandidate(QPointF(r.left(), r.center().y()), ObjectSnapMode::Midpoint);
            addCandidate(QPointF(r.right(), r.center().y()), ObjectSnapMode::Midpoint);
            addCandidate(r.center(), ObjectSnapMode::Center);
            addSegment(tl, tr);
            addSegment(tr, br);
            addSegment(br, bl);
            addSegment(bl, tl);
        } else if (auto* polyline = dynamic_cast<CadPolyline*>(entityPtr.get())) {
            const QVector<QPointF>& pts = polyline->points();
            for (const QPointF& p : pts) addCandidate(p, ObjectSnapMode::Vertex);
            for (int i = 1; i < pts.size(); ++i) {
                const QPointF a = pts.at(i - 1);
                const QPointF b = pts.at(i);
                addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
                addSegment(a, b);
            }
            if (polyline->closed() && pts.size() > 2) {
                const QPointF a = pts.last();
                const QPointF b = pts.first();
                addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
                addSegment(a, b);
            }
        } else if (auto* arc = dynamic_cast<CadArc*>(entityPtr.get())) {
            const QPointF c = arc->center();
            const double r = arc->radius();
            const double a0 = qDegreesToRadians(arc->startAngleDeg());
            const double a1 = qDegreesToRadians(arc->startAngleDeg() + arc->spanAngleDeg());
            addCandidate(c, ObjectSnapMode::Center);
            addCandidate(QPointF(c.x() + r * std::cos(a0), c.y() - r * std::sin(a0)), ObjectSnapMode::Endpoint);
            addCandidate(QPointF(c.x() + r * std::cos(a1), c.y() - r * std::sin(a1)), ObjectSnapMode::Endpoint);
            addCircle(c, r);
        } else if (auto* ellipse = dynamic_cast<CadEllipse*>(entityPtr.get())) {
            const QRectF r = ellipse->rect();
            addCandidate(r.center(), ObjectSnapMode::Center);
            addCandidate(QPointF(r.left(), r.center().y()), ObjectSnapMode::Quadrant);
            addCandidate(QPointF(r.right(), r.center().y()), ObjectSnapMode::Quadrant);
            addCandidate(QPointF(r.center().x(), r.top()), ObjectSnapMode::Quadrant);
            addCandidate(QPointF(r.center().x(), r.bottom()), ObjectSnapMode::Quadrant);
        } else if (auto* polygon = dynamic_cast<CadPolygon*>(entityPtr.get())) {
            addCandidate(polygon->center(), ObjectSnapMode::Center);
            QVector<QPointF> vertices;
            const double base = qDegreesToRadians(polygon->rotationDeg());
            for (int i = 0; i < polygon->sides(); ++i) {
                const double a = base + (2.0 * M_PI * i / polygon->sides());
                vertices << QPointF(polygon->center().x() + polygon->radius() * std::cos(a),
                                    polygon->center().y() + polygon->radius() * std::sin(a));
                addCandidate(vertices.last(), ObjectSnapMode::Vertex);
            }
            for (int i = 0; i < vertices.size(); ++i) {
                const QPointF a = vertices.at(i);
                const QPointF b = vertices.at((i + 1) % vertices.size());
                addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
                addSegment(a, b);
            }
        } else if (auto* hatch = dynamic_cast<CadHatch*>(entityPtr.get())) {
            auto addLoopSnap = [&](const QVector<QPointF>& pts, bool closed) {
                for (const QPointF& p : pts) addCandidate(p, ObjectSnapMode::Vertex);
                for (int i = 1; i < pts.size(); ++i) {
                    const QPointF a = pts.at(i - 1);
                    const QPointF b = pts.at(i);
                    addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
                    addSegment(a, b);
                }
                if (closed && pts.size() > 2) {
                    const QPointF a = pts.last();
                    const QPointF b = pts.first();
                    addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
                    addSegment(a, b);
                }
            };
            addLoopSnap(hatch->boundary(), true);
            for (const QVector<QPointF>& loop : hatch->loops()) addLoopSnap(loop, true);
        } else if (auto* dim = dynamic_cast<CadLinearDimension*>(entityPtr.get())) {
            const QPointF a = dim->first();
            const QPointF b = dim->second();
            const QPointF d = dim->dimensionPoint();
            addCandidate(a, ObjectSnapMode::Endpoint);
            addCandidate(b, ObjectSnapMode::Endpoint);
            addCandidate(d, ObjectSnapMode::Vertex);
            addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
            addSegment(a, b);
            addSegment(a, d);
            addSegment(b, d);
        } else if (auto* leader = dynamic_cast<CadLeader*>(entityPtr.get())) {
            const QPointF a = leader->arrowPoint();
            const QPointF b = leader->textPoint();
            addCandidate(a, ObjectSnapMode::Endpoint);
            addCandidate(b, ObjectSnapMode::Endpoint);
            addCandidate(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0), ObjectSnapMode::Midpoint);
            addSegment(a, b);
        } else if (auto* text = dynamic_cast<CadText*>(entityPtr.get())) {
            addCandidate(text->position(), ObjectSnapMode::Vertex);
        } else if (auto* block = dynamic_cast<CadBlockReference*>(entityPtr.get())) {
            addCandidate(block->insertionPoint(), ObjectSnapMode::Vertex);
        }
    }

    currentSnapEntityIndex = -1;

    // Intersection locale seulement : indispensable pour les gros DWG.
    // On évite le O(n^2) global et on ne teste que les segments proches du curseur.
    if (m_snapIntersectionEnabled && localSegments.size() <= 500) {
        for (int i = 0; i < localSegments.size(); ++i) {
            for (int j = i + 1; j < localSegments.size(); ++j) {
                QPointF p;
                if (segmentIntersection(localSegments.at(i), localSegments.at(j), p)) {
                    addCandidate(p, ObjectSnapMode::Intersection, -1);
                }
            }
        }
    }

    if (m_snapTangentEnabled) {
        if (!tangentAnchor) {
            for (const CircleInfo& circle : circlesNearCursor) {
                addCandidate(nearestPointOnCircle(rawPoint, circle), ObjectSnapMode::Tangent, -1);
            }
        } else {
            // Point -> cercle : le curseur doit être proche du cercle cible.
            for (const CircleInfo& circle : circlesNearCursor) {
                for (const QPointF& p : tangentPointsFromPointToCircle(*tangentAnchor, circle)) {
                    addCandidate(p, ObjectSnapMode::Tangent, -1);
                }
            }

            // Circle -> cercle : le point de départ doit être proche d'un cercle source,
            // et le curseur proche du cercle cible.
            for (const CircleInfo& source : circlesNearAnchor) {
                for (const CircleInfo& target : circlesNearCursor) {
                    if (distanceBetween(source.center, target.center) <= 1e-9 &&
                        std::abs(source.radius - target.radius) <= 1e-9) {
                        continue;
                    }
                    for (const auto& pair : circleCircleTangentPairs(source, target)) {
                        if (distanceBetween(pair.first, *tangentAnchor) <= looseSceneTol) {
                            addCandidate(pair.second, ObjectSnapMode::Tangent, -1);
                        }
                    }
                }
            }
        }
    }
}

bool GraphicsView::nearestObjectSnapPoint(const QPointF& rawPoint, SnapCandidate& snappedCandidate, const QPointF* tangentAnchor) const
{
    QVector<SnapCandidate> candidates;
    collectSnapCandidates(candidates, rawPoint, tangentAnchor);
    if (candidates.isEmpty()) return false;

    const QPoint rawView = mapFromScene(rawPoint);
    double bestDistance = m_objectSnapTolerancePx;
    bool found = false;

    for (const SnapCandidate& candidate : candidates) {
        const QPoint candidateView = mapFromScene(candidate.point);
        const double dx = static_cast<double>(candidateView.x() - rawView.x());
        const double dy = static_cast<double>(candidateView.y() - rawView.y());
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d <= bestDistance) {
            bestDistance = d;
            snappedCandidate = candidate;
            found = true;
        }
    }

    return found;
}

void GraphicsView::setCurrentSnapMarker(const SnapCandidate* candidate)
{
    if (candidate) {
        m_currentSnapCandidate = *candidate;
        m_hasCurrentSnapMarker = true;
    } else {
        m_hasCurrentSnapMarker = false;
    }
    viewport()->update();
}

QPointF GraphicsView::effectiveScenePoint(const QPointF& rawPoint, const QPointF* orthoAnchor)
{
    QPointF p = rawPoint;
    SnapCandidate objectCandidate;
    const bool snappedToObject = m_objectSnapEnabled && nearestObjectSnapPoint(rawPoint, objectCandidate, orthoAnchor);

    if (snappedToObject) {
        p = objectCandidate.point;
        setCurrentSnapMarker(&objectCandidate);
    } else {
        setCurrentSnapMarker(nullptr);
        if (m_snapEnabled) p = gridSnappedPoint(p);
    }

    if (m_fixedAngleConstraintEnabled && orthoAnchor) {
        p = fixedAngleConstrainedPoint(*orthoAnchor, p);
        if (!snappedToObject && m_snapEnabled) p = gridSnappedPoint(p);
    } else if (m_orthoEnabled && orthoAnchor) {
        p = orthoConstrainedPoint(*orthoAnchor, p);
        if (!snappedToObject && m_snapEnabled) p = gridSnappedPoint(p);
    }

    return p;
}

void GraphicsView::drawObjectSnapMarker(QPainter* painter) const
{
    if (!m_hasCurrentSnapMarker) return;

    const QPointF viewPoint = mapFromScene(m_currentSnapCandidate.point);
    const double x = viewPoint.x();
    const double y = viewPoint.y();
    const double s = 8.0;

    painter->save();
    painter->resetTransform();
    QPen pen(QColor(0, 255, 0));
    pen.setWidthF(1.8);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    switch (m_currentSnapCandidate.mode) {
    case ObjectSnapMode::Endpoint:
        painter->drawRect(QRectF(x - s / 2.0, y - s / 2.0, s, s));
        break;
    case ObjectSnapMode::Midpoint: {
        QPolygonF triangle;
        triangle << QPointF(x, y - s / 2.0) << QPointF(x + s / 2.0, y + s / 2.0) << QPointF(x - s / 2.0, y + s / 2.0);
        painter->drawPolygon(triangle);
        break;
    }
    case ObjectSnapMode::Center:
        painter->drawEllipse(QPointF(x, y), s / 2.0, s / 2.0);
        painter->drawLine(QPointF(x - s, y), QPointF(x + s, y));
        painter->drawLine(QPointF(x, y - s), QPointF(x, y + s));
        break;
    case ObjectSnapMode::Quadrant: {
        QPolygonF diamond;
        diamond << QPointF(x, y - s / 2.0) << QPointF(x + s / 2.0, y) << QPointF(x, y + s / 2.0) << QPointF(x - s / 2.0, y);
        painter->drawPolygon(diamond);
        break;
    }
    case ObjectSnapMode::Intersection:
        painter->drawLine(QPointF(x - s / 2.0, y - s / 2.0), QPointF(x + s / 2.0, y + s / 2.0));
        painter->drawLine(QPointF(x - s / 2.0, y + s / 2.0), QPointF(x + s / 2.0, y - s / 2.0));
        break;
    case ObjectSnapMode::Tangent:
        painter->drawEllipse(QPointF(x, y), s / 2.0, s / 2.0);
        painter->drawLine(QPointF(x + s / 2.0, y), QPointF(x + s, y));
        painter->drawLine(QPointF(x + s, y), QPointF(x + s, y - s / 2.0));
        break;
    case ObjectSnapMode::Vertex:
        painter->drawRect(QRectF(x - s / 2.0, y - s / 2.0, s, s));
        painter->drawLine(QPointF(x - s, y), QPointF(x + s, y));
        painter->drawLine(QPointF(x, y - s), QPointF(x, y + s));
        break;
    }

    painter->drawText(QPointF(x + 10.0, y - 10.0), objectSnapModeName(m_currentSnapCandidate.mode));
    painter->restore();
}

void GraphicsView::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawBackground(painter, rect);

    if (!m_gridEnabled || m_gridSpacing <= 0.0) return;

    const double lod = transform().m11();
    if (m_largeDocumentMode && lod < 0.003) return; // ne pas dessiner une grille massive quand on est tres loin
    double spacing = m_gridSpacing;
    while (spacing * lod < 8.0) spacing *= 2.0;
    while (spacing * lod > 80.0 && spacing / 2.0 >= m_gridSpacing) spacing /= 2.0;

    const double left = std::floor(rect.left() / spacing) * spacing;
    const double right = std::ceil(rect.right() / spacing) * spacing;
    const double top = std::floor(rect.top() / spacing) * spacing;
    const double bottom = std::ceil(rect.bottom() / spacing) * spacing;

    QPen minorPen(CadRenderStyle::minorGridColor());
    minorPen.setCosmetic(true);
    minorPen.setWidthF(1.0);
    painter->setPen(minorPen);

    for (double x = left; x <= right; x += spacing) {
        painter->drawLine(QPointF(x, top), QPointF(x, bottom));
    }
    for (double y = top; y <= bottom; y += spacing) {
        painter->drawLine(QPointF(left, y), QPointF(right, y));
    }

    QPen axisPen(CadRenderStyle::axisGridColor());
    axisPen.setCosmetic(true);
    axisPen.setWidthF(1.4);
    painter->setPen(axisPen);
    painter->drawLine(QPointF(0.0, top), QPointF(0.0, bottom));
    painter->drawLine(QPointF(left, 0.0), QPointF(right, 0.0));
}

void GraphicsView::drawForeground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawForeground(painter, rect);
    drawSelectionHighlight(painter);
    drawObjectSnapMarker(painter);
    drawDimensionPickMarkers(painter);
    drawZoomWindowRubberBand(painter);
}

void GraphicsView::drawZoomWindowRubberBand(QPainter* painter) const
{
    if (!m_zoomWindowActive || !m_zoomWindowDragging) return;

    const QRect rubber = QRect(m_zoomWindowStart, m_zoomWindowEnd).normalized();
    if (rubber.width() < 2 || rubber.height() < 2) return;

    painter->save();
    painter->resetTransform();

    QPen pen(QColor(255, 230, 0));
    pen.setWidthF(1.6);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(QColor(255, 230, 0, 28));
    painter->drawRect(rubber);

    painter->restore();
}

void GraphicsView::finishZoomWindow(const QRect& viewportRect)
{
    QRect r = viewportRect.normalized();
    if (!viewport()) return;

    const int minPixels = 8;
    if (r.width() < minPixels || r.height() < minPixels) {
        logDrawEvent(QStringLiteral("ZOOM WINDOW: rectangle too small; click-drag a real window."));
        m_zoomWindowDragging = false;
        viewport()->update();
        emitDrawPrompt();
        return;
    }

    r = r.intersected(viewport()->rect());
    if (r.width() < minPixels || r.height() < minPixels) {
        logDrawEvent(QStringLiteral("ZOOM WINDOW: rectangle hors viewport; recommencez."));
        m_zoomWindowDragging = false;
        viewport()->update();
        emitDrawPrompt();
        return;
    }

    const QRectF sceneRect(mapToScene(r.topLeft()), mapToScene(r.bottomRight()));
    const QRectF target = safeRectForView(sceneRect.normalized());

    m_zoomWindowActive = false;
    m_zoomWindowDragging = false;
    m_drawingTool = DrawingTool::Noe;
    setCursor(Qt::ArrowCursor);
    setDragMode(QGraphicsView::RubberBandDrag);

    fitSceneRectStable(target, 0.02);
    logDrawEvent(QStringLiteral("ZOOM WINDOW applied: x=%1 y=%2 w=%3 h=%4")
                     .arg(target.x(), 0, 'f', 4)
                     .arg(target.y(), 0, 'f', 4)
                     .arg(target.width(), 0, 'f', 4)
                     .arg(target.height(), 0, 'f', 4));
    emitDrawPrompt();
}

void GraphicsView::drawSelectionHighlight(QPainter* painter) const
{
    if (!painter) return;

    const QList<QGraphicsItem*> selected = scene() ? scene()->selectedItems() : QList<QGraphicsItem*>();
    const QVector<int> documentSelected = selectedEntityIndices();
    if (selected.isEmpty() && documentSelected.isEmpty()) return;

    painter->save();

    QPen highlightPen(QColor(0, 215, 255));
    highlightPen.setCosmetic(true);
    highlightPen.setWidthF(2.8);
    painter->setPen(highlightPen);
    painter->setBrush(Qt::NoBrush);

    const double gripSize = sceneToleranceFromView(this, 5.0);
    const double halfGrip = gripSize / 2.0;
    QBrush gripBrush(QColor(0, 215, 255));

    QSet<int> itemDrawnIndices;
    auto drawGrips = [&](const QRectF& r) {
        const QVector<QPointF> grips = { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(), r.center() };
        painter->setPen(Qt::NoPen);
        painter->setBrush(gripBrush);
        for (const QPointF& g : grips) {
            painter->drawRect(QRectF(g.x() - halfGrip, g.y() - halfGrip, gripSize, gripSize));
        }
        painter->setPen(highlightPen);
        painter->setBrush(Qt::NoBrush);
    };

    // Items Qt individuels : comportement classique.
    for (QGraphicsItem* item : selected) {
        if (!item || !item->data(1).isValid()) continue;

        bool ok = false;
        const int index = item->data(1).toInt(&ok);
        if (ok && index >= 0) itemDrawnIndices.insert(index);

        painter->drawPath(item->mapToScene(item->shape()));
        drawGrips(item->sceneBoundingRect());
    }

    // Select document native : entités DXF/DWG en FastCadBatchItem.
    // Ces entités ne sont pas sélectionnables par QGraphicsScene, donc on dessine
    // l'outline et les grips directement depuis CadDocument.
    if (m_document) {
        for (int index : documentSelected) {
            if (index < 0 || itemDrawnIndices.contains(index)) continue;
            const CadEntity* entity = m_document->entityAt(index);
            if (!entity) continue;

            QPainterPath path = entitySelectionPath(entity);
            if (!path.isEmpty()) painter->drawPath(path);

            QRectF r = entitySnapBounds(entity).normalized();
            if ((!r.isValid() || r.isNull()) && !path.isEmpty()) r = path.boundingRect().normalized();
            if (r.isValid() && !r.isNull()) drawGrips(r);
        }
    }

    painter->restore();
}


void GraphicsView::drawDimensionPickMarkers(QPainter* painter) const
{
    if (m_dimensionPickMarkers.isEmpty() && !m_hasDimensionReferencePreview) return;

    painter->save();
    painter->resetTransform();

    const double s = 9.0;

    if (m_hasDimensionReferencePreview) {
        const QPointF viewPoint = mapFromScene(m_dimensionReferencePreviewPoint);
        QPen previewPen(QColor(255, 230, 0));
        previewPen.setWidthF(2.2);
        painter->setPen(previewPen);
        painter->setBrush(QBrush(QColor(255, 230, 0, 35)));
        painter->drawRect(QRectF(viewPoint.x() - s / 2.0,
                                 viewPoint.y() - s / 2.0,
                                 s,
                                 s));
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(QPointF(viewPoint.x() - s, viewPoint.y()), QPointF(viewPoint.x() + s, viewPoint.y()));
        painter->drawLine(QPointF(viewPoint.x(), viewPoint.y() - s), QPointF(viewPoint.x(), viewPoint.y() + s));
        if (!m_dimensionReferencePreviewLabel.isEmpty()) {
            painter->drawText(QPointF(viewPoint.x() + 11.0, viewPoint.y() - 11.0),
                              QStringLiteral("DIM %1").arg(m_dimensionReferencePreviewLabel));
        }
    }

    QPen pen(QColor(255, 205, 0));
    pen.setWidthF(2.0);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    for (const QPointF& scenePoint : m_dimensionPickMarkers) {
        const QPointF viewPoint = mapFromScene(scenePoint);
        painter->drawRect(QRectF(viewPoint.x() - s / 2.0,
                                 viewPoint.y() - s / 2.0,
                                 s,
                                 s));
    }

    painter->restore();
}

void GraphicsView::addDimensionPickMarker(const QPointF& point)
{
    m_dimensionPickMarkers.append(point);
    constexpr int kMaxMarkers = 12;
    while (m_dimensionPickMarkers.size() > kMaxMarkers) {
        m_dimensionPickMarkers.removeFirst();
    }
    viewport()->update();
}

void GraphicsView::clearDimensionPickMarkers()
{
    if (m_dimensionPickMarkers.isEmpty() && !m_hasDimensionReferencePreview) return;
    m_dimensionPickMarkers.clear();
    m_hasDimensionReferencePreview = false;
    m_dimensionReferencePreviewLabel.clear();
    viewport()->update();
}

QGraphicsItem* GraphicsView::cadItemAt(const QPoint& viewPos) const
{
    if (!scene()) return nullptr;

    const QList<QGraphicsItem*> hitItems = items(viewPos);
    for (QGraphicsItem* item : hitItems) {
        if (item && item->data(1).isValid() && item->data(1).toInt() >= 0 && !item->data(2).toBool()) {
            return item;
        }
    }
    return nullptr;
}

bool GraphicsView::entityAcceptedForInteractiveCommand(const QString& command, const CadEntity* entity) const
{
    if (!entity) return false;
    const QString cmd = command.trimmed().toUpper();

    // Les cotations cercle/rayon/diamètre doivent détecter l'entité réelle sous
    // le curseur. Elles ne doivent pas dépendre du snap ni accepter par erreur
    // une ligne proche du cercle. Les arcs pourront recevoir une vraie cotation
    // dédiée quand CadRadialDimension/CadDiametricDimension seront natifs.
    if (cmd == QStringLiteral("DIMCENTER") ||
        cmd == QStringLiteral("DIMDIAMETER") ||
        cmd == QStringLiteral("DIMRADIUS")) {
        return dynamic_cast<const CadCircle*>(entity) != nullptr;
    }

    return true;
}


bool GraphicsView::dimensionReferencePointAt(const QPoint& viewPos,
                                             const QPointF& rawScenePoint,
                                             QPointF& outPoint,
                                             QString* outLabel,
                                             int* outEntityIndex) const
{
    if (!m_document) return false;

    // Cotation: utiliser le même moteur de recherche que l'OSNAP, mais sans
    // dépendre de l'état F3 Object Snap ni de F9 Grid Snap. Les deux premiers
    // points de DIMLINEAR doivent donc s'accrocher automatiquement aux vraies
    // références géométriques proches du curseur: extrémité, sommet, milieu,
    // centre, quadrant, intersection, tangente.
    QVector<SnapCandidate> candidates;
    collectSnapCandidates(candidates, rawScenePoint, nullptr);
    if (candidates.isEmpty()) return false;

    const QPoint rawView = viewPos;
    auto pixelDistance = [&](const SnapCandidate& c) {
        const QPoint candidateView = mapFromScene(c.point);
        const double dx = static_cast<double>(candidateView.x() - rawView.x());
        const double dy = static_cast<double>(candidateView.y() - rawView.y());
        return std::sqrt(dx * dx + dy * dy);
    };

    auto priority = [](ObjectSnapMode mode) {
        // Priorité de cotation: l'origine de cote doit préférer les références
        // structurelles avant les points auxiliaires quand la distance pixel est
        // pratiquement identique.
        switch (mode) {
        case ObjectSnapMode::Endpoint:     return 0;
        case ObjectSnapMode::Vertex:       return 1;
        case ObjectSnapMode::Intersection: return 2;
        case ObjectSnapMode::Center:       return 3;
        case ObjectSnapMode::Quadrant:     return 4;
        case ObjectSnapMode::Midpoint:     return 5;
        case ObjectSnapMode::Tangent:      return 6;
        }
        return 99;
    };

    std::sort(candidates.begin(), candidates.end(), [&](const SnapCandidate& a, const SnapCandidate& b) {
        const double da = pixelDistance(a);
        const double db = pixelDistance(b);
        if (std::abs(da - db) <= 2.0) {
            const int pa = priority(a.mode);
            const int pb = priority(b.mode);
            if (pa != pb) return pa < pb;
            return a.entityIndex < b.entityIndex;
        }
        return da < db;
    });

    const SnapCandidate& best = candidates.first();
    outPoint = best.point;
    if (outLabel) *outLabel = objectSnapModeName(best.mode);
    if (outEntityIndex) *outEntityIndex = best.entityIndex;
    return true;
}

void GraphicsView::updateDimensionReferencePreview(const QPoint& viewPos, const QPointF& rawScenePoint)
{
    if (m_drawingTool != DrawingTool::LinearDimension) {
        clearDimensionReferencePreview();
        return;
    }

    const int nextPointIndex = m_drawing ? m_points.size() : 0;
    if (nextPointIndex >= 2) {
        clearDimensionReferencePreview();
        return;
    }

    QPointF detectedPoint;
    QString detectedLabel;
    int detectedEntityIndex = -1;
    if (dimensionReferencePointAt(viewPos, rawScenePoint, detectedPoint, &detectedLabel, &detectedEntityIndex)) {
        m_dimensionReferencePreviewPoint = detectedPoint;
        m_dimensionReferencePreviewLabel = detectedEntityIndex >= 0
            ? QStringLiteral("%1 #%2").arg(detectedLabel).arg(detectedEntityIndex)
            : detectedLabel;
        m_hasDimensionReferencePreview = true;
        setCurrentSnapMarker(nullptr);
    } else {
        m_hasDimensionReferencePreview = false;
        m_dimensionReferencePreviewLabel.clear();
    }
    viewport()->update();
}

void GraphicsView::clearDimensionReferencePreview()
{
    if (!m_hasDimensionReferencePreview && m_dimensionReferencePreviewLabel.isEmpty()) return;
    m_hasDimensionReferencePreview = false;
    m_dimensionReferencePreviewLabel.clear();
    viewport()->update();
}

int GraphicsView::cadEntityIndexAt(const QPoint& viewPos, QGraphicsItem** hitItem, const QString& commandFilter) const
{
    if (hitItem) *hitItem = nullptr;
    if (!m_document) return -1;

    // 1) Chemin normal: vrais QGraphicsItem individuels sous la souris.
    // On filtre quand la commande demande un type précis, par exemple DIMRADIUS
    // doit choisir un cercle et non une polyligne proche.
    if (scene()) {
        const QList<QGraphicsItem*> hitItems = items(viewPos);
        for (QGraphicsItem* item : hitItems) {
            if (!item || !item->data(1).isValid() || item->data(2).toBool()) continue;
            bool ok = false;
            const int index = item->data(1).toInt(&ok);
            if (!ok || index < 0 || index >= m_document->entityCount()) continue;
            const CadEntity* entity = m_document->entityAt(index);
            if (!entityAcceptedForInteractiveCommand(commandFilter, entity)) continue;
            if (hitItem) *hitItem = item;
            return index;
        }
    }

    // 2) Fallback géométrique natif: indépendant de snapInput/object snap.
    // Utile pour les gros fichiers où renderToScene() regroupe les objets en
    // batch visuel sans item éditable individuel.
    const QPointF scenePoint = mapToScene(viewPos);
    const double tol = sceneToleranceFromView(this, 12.0);
    const QRectF query(scenePoint.x() - tol, scenePoint.y() - tol, tol * 2.0, tol * 2.0);

    int bestIndex = -1;
    double bestDistance = std::numeric_limits<double>::max();
    const auto& allEntities = m_document->entities();
    const auto& layers = m_document->layers();

    for (int i = 0; i < static_cast<int>(allEntities.size()); ++i) {
        const auto& entityPtr = allEntities[static_cast<size_t>(i)];
        if (!entityPtr) continue;
        const CadEntity* entity = entityPtr.get();
        if (!entityAcceptedForInteractiveCommand(commandFilter, entity)) continue;

        const CadLayer layer = layers.value(entity->layer(), layers.value(QStringLiteral("0")));
        if (!layer.visible || layer.locked) continue;

        QRectF bounds = entitySnapBounds(entity);
        if (bounds.isNull()) continue;
        bounds = bounds.normalized().adjusted(-tol, -tol, tol, tol);
        if (!bounds.intersects(query) && !bounds.contains(scenePoint)) continue;

        const double d = entityPickDistance(entity, scenePoint, tol);
        if (d <= tol && d < bestDistance) {
            bestDistance = d;
            bestIndex = i;
        }
    }

    return bestIndex;
}

QVector<int> GraphicsView::operationSelectionIndices() const
{
    if (!m_validatedModifySelection.isEmpty()) return m_validatedModifySelection;
    if (!m_interactiveModifyCurrentSelection.isEmpty()) return m_interactiveModifyCurrentSelection;
    return selectedEntityIndices();
}

void GraphicsView::clearValidatedModifySelection()
{
    m_validatedModifySelection.clear();
}

int GraphicsView::textEntityIndexAt(const QPoint& viewPos) const
{
    if (!m_document || !scene()) return -1;

    auto isEditableTextIndex = [this](int index) -> bool {
        const auto* text = dynamic_cast<const CadText*>(m_document->entityAt(index));
        if (!text) return false;
        const CadLayer layer = m_document->layers().value(text->layer(), m_document->layers().value("0"));
        return layer.visible && !layer.locked;
    };

    // Premier passage: items graphiques directement sous le double-clic.
    const QRect hitRect(viewPos.x() - 8, viewPos.y() - 8, 16, 16);
    const QList<QGraphicsItem*> hitItems = items(hitRect, Qt::IntersectsItemShape);
    for (QGraphicsItem* item : hitItems) {
        if (!item || !item->data(1).isValid()) continue;
        bool ok = false;
        const int index = item->data(1).toInt(&ok);
        if (ok && index >= 0 && isEditableTextIndex(index)) return index;
    }

    // Fallback document: utile pour les gros fichiers ou textes vectoriels fins.
    const QPointF scenePoint = mapToScene(viewPos);
    const double tol = sceneToleranceFromView(this, 18.0);
    int bestIndex = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < m_document->entityCount(); ++i) {
        const auto* text = dynamic_cast<const CadText*>(m_document->entityAt(i));
        if (!text || !isEditableTextIndex(i)) continue;
        const QRectF r = textEditorSceneRect(text).adjusted(-tol, -tol, tol, tol);
        if (!r.contains(scenePoint)) continue;
        const double d = distanceBetween(scenePoint, text->position());
        if (d < bestDistance) {
            bestDistance = d;
            bestIndex = i;
        }
    }
    return bestIndex;
}

QRectF GraphicsView::textEditorSceneRect(const CadText* text) const
{
    if (!text) return QRectF();
    const double h = std::max(1.0e-6, text->height());
    const QString content = text->text().isEmpty() ? QStringLiteral("Text") : text->text();
    const QStringList lines = content.split(QLatin1Char('\n'));
    int maxChars = 1;
    for (const QString& line : lines) maxChars = std::max<int>(maxChars, static_cast<int>(line.size()));
    const double width = std::max(h * 4.0, maxChars * h * 0.68 * std::max(0.01, text->widthFactor()));
    const double height = std::max(h * 1.4, lines.size() * h * 1.35);
    return QRectF(text->position().x(), text->position().y() - height, width, height).normalized();
}

void GraphicsView::startTextInlineEdit(int entityIndex, QGraphicsItem* sourceItem)
{
    if (!m_document) return;
    auto* text = dynamic_cast<CadText*>(m_document->entityAt(entityIndex));
    if (!text) return;

    if (m_textInlineEditor) finishTextInlineEdit(true);
    cancelDrawing();

    m_editingTextIndex = entityIndex;
    m_editingTextItem = sourceItem;
    m_editingTextOriginalOpacity = sourceItem ? sourceItem->opacity() : 1.0;
    m_editingTextBlinkVisible = true;

    m_textInlineEditor = new QLineEdit(viewport());
    m_textInlineEditor->setText(text->text());
    m_textInlineEditor->selectAll();
    m_textInlineEditor->installEventFilter(this);
    m_textInlineEditor->setFrame(true);
    m_textInlineEditor->setStyleSheet(QStringLiteral(
        "QLineEdit { background: rgba(255,255,210,245); color: #111; "
        "border: 2px solid #ffaa00; padding: 2px; selection-background-color: #3399ff; }"));

    updateTextInlineEditorGeometry();
    m_textInlineEditor->show();
    m_textInlineEditor->setFocus(Qt::MouseFocusReason);

    connect(m_textInlineEditor, &QLineEdit::returnPressed, this, [this]() { finishTextInlineEdit(true); });
    connect(m_textInlineEditor, &QLineEdit::editingFinished, this, [this]() {
        if (m_textInlineEditor) finishTextInlineEdit(true);
    });

    if (m_editingTextItem) {
        m_editingTextItem->setSelected(true);
        m_textEditBlinkTimer->start();
    }

    logDrawEvent(QStringLiteral("TEXT EDIT: double-click text #%1. Enter=accept, Esc=cancel.").arg(entityIndex));
    emitDrawPrompt();
}

void GraphicsView::finishTextInlineEdit(bool acceptChanges)
{
    if (!m_textInlineEditor) return;

    const int index = m_editingTextIndex;
    const QString newText = m_textInlineEditor->text();

    QLineEdit* editor = m_textInlineEditor.data();
    m_textInlineEditor = nullptr;
    editor->removeEventFilter(this);
    editor->deleteLater();

    if (m_textEditBlinkTimer) m_textEditBlinkTimer->stop();
    if (m_editingTextItem) {
        m_editingTextItem->setOpacity(m_editingTextOriginalOpacity);
        m_editingTextItem->setVisible(true);
    }

    m_editingTextItem = nullptr;
    m_editingTextIndex = -1;
    m_editingTextOriginalOpacity = 1.0;
    m_editingTextBlinkVisible = true;

    if (acceptChanges && m_document) {
        if (auto* text = dynamic_cast<CadText*>(m_document->entityAt(index))) {
            const QString clean = newText.trimmed().isEmpty() ? QStringLiteral("Text") : newText;
            if (text->text() != clean) {
                text->setText(clean);
                m_document->setModified(true);
                refreshFromDocumentPreservingSelection({index});
                logDrawEvent(QStringLiteral("TEXT EDIT: text #%1 modified -> \"%2\"").arg(index).arg(clean));
                return;
            }
        }
    }

    viewport()->update();
    logDrawEvent(acceptChanges ? QStringLiteral("TEXT EDIT: aucun changement.")
                                : QStringLiteral("TEXT EDIT: canceled."));
}

void GraphicsView::cancelTextInlineEdit()
{
    finishTextInlineEdit(false);
}

void GraphicsView::updateTextInlineEditorGeometry()
{
    if (!m_textInlineEditor || !m_document || m_editingTextIndex < 0) return;
    const auto* text = dynamic_cast<const CadText*>(m_document->entityAt(m_editingTextIndex));
    if (!text) return;

    QRectF sceneRect = textEditorSceneRect(text);
    const QPoint topLeft = mapFromScene(sceneRect.topLeft());
    const QPoint bottomRight = mapFromScene(sceneRect.bottomRight());
    QRect viewRect(topLeft, bottomRight);
    viewRect = viewRect.normalized().adjusted(-4, -4, 12, 8);
    viewRect.setWidth(std::max(viewRect.width(), 140));
    viewRect.setHeight(std::max(viewRect.height(), 28));
    m_textInlineEditor->setGeometry(viewRect);
}

QPointF GraphicsView::selectionMovePoint(const QPoint& viewPos) const
{
    QPointF p = mapToScene(viewPos);
    if (m_snapEnabled) {
        p = gridSnappedPoint(p);
    }
    return p;
}

QRectF GraphicsView::normalizedRect(const QPointF& a, const QPointF& b) const
{
    return QRectF(a, b).normalized();
}

QRectF GraphicsView::circleRect(const QPointF& center, const QPointF& edge) const
{
    const double r = distanceBetween(center, edge);
    return QRectF(center.x() - r, center.y() - r, 2.0 * r, 2.0 * r);
}

QRectF GraphicsView::circleDiameterRect(const QPointF& a, const QPointF& b) const
{
    const QPointF center((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0);
    const double r = distanceBetween(a, b) / 2.0;
    return QRectF(center.x() - r, center.y() - r, 2.0 * r, 2.0 * r);
}

QVector<QPointF> GraphicsView::regularPolygonPoints(const QPointF& center, const QPointF& edge) const
{
    QVector<QPointF> points;
    const double radius = distanceBetween(center, edge);
    const double baseAngle = std::atan2(edge.y() - center.y(), edge.x() - center.x());
    for (int i = 0; i < m_polygonSides; ++i) {
        const double a = baseAngle + (2.0 * M_PI * i / m_polygonSides);
        points.append(QPointF(center.x() + radius * std::cos(a), center.y() + radius * std::sin(a)));
    }
    return points;
}


QVector<int> GraphicsView::selectedEntityIndices() const
{
    QVector<int> indices;

    // 1) Select native document : indispensable pour les DXF/DWG externes
    // en rendu batch, où les entités visibles n'ont pas toutes un item Qt.
    for (int index : m_documentSelectionIndices) {
        if (index >= 0 && !indices.contains(index)) indices.append(index);
    }

    // 2) Select Qt classique : petits fichiers et annotations/items non batchés.
    if (scene()) {
        for (QGraphicsItem* item : scene()->selectedItems()) {
            if (!item || item->data(2).toBool()) continue;
            const QVariant v = item->data(1);
            bool ok = false;
            const int index = v.toInt(&ok);
            if (!ok || index < 0) continue;
            if (!indices.contains(index)) indices.append(index);
        }
    }

    std::sort(indices.begin(), indices.end());
    return indices;
}

bool GraphicsView::hasCadSelection() const
{
    return !selectedEntityIndices().isEmpty();
}


QString GraphicsView::interactiveModifyStagePrompt() const
{
    if (m_interactiveModifyCommand.isEmpty()) return QString();
    const QString cmd = m_interactiveModifyCommand;
    if (m_interactiveModifyStage == 0) {
        if (cmd == QStringLiteral("MATCHPROP"))
            return QStringLiteral("MATCHPROP: select the source object, then press Enter.");
        if (cmd == QStringLiteral("EXTEND"))
            return QStringLiteral("EXTEND: select exactly one open entity with two endpoints, then press Enter.");
        if (cmd == QStringLiteral("TRIM") || cmd == QStringLiteral("CHAMFER") || cmd == QStringLiteral("FILLET"))
            return QStringLiteral("%1: select the first object / boundary, then press Enter.").arg(cmd);
        if (cmd == QStringLiteral("HATCHSELECT"))
            return QStringLiteral("HATCH: select closed boundaries (circle, rectangle, closed polyline, or connected lines), then press Enter to create the hatch.");
        if (cmd == QStringLiteral("DIMCENTER") || cmd == QStringLiteral("DIMDIAMETER") || cmd == QStringLiteral("DIMRADIUS"))
            return QStringLiteral("%1: click circles directly to dimension them, small square=entity detected, then press Enter. Detection is independent of snap.").arg(cmd);
        return QStringLiteral("%1: select the objects, then press Enter.").arg(cmd);
    }
    if (cmd == QStringLiteral("MATCHPROP"))
        return QStringLiteral("MATCHPROP: select target objects, then press Enter to apply properties.");
    if (cmd == QStringLiteral("EXTEND"))
        return QStringLiteral("EXTEND: select only the target boundary entity, then press Enter to extend the source.");
    if (cmd == QStringLiteral("TRIM") || cmd == QStringLiteral("CHAMFER") || cmd == QStringLiteral("FILLET"))
        return QStringLiteral("%1: select the second object, then press Enter to confirm.").arg(cmd);
    return QStringLiteral("%1: select the second step, then press Enter.").arg(cmd);
}

void GraphicsView::logInteractiveModifyPrompt()
{
    const QString prompt = interactiveModifyStagePrompt();
    if (!prompt.isEmpty()) logDrawEvent(prompt);
    emitDrawPrompt();
}

bool GraphicsView::startInteractiveModifyCommand(const QString& command)
{
    const QString cmd = command.trimmed().toUpper();
    static const QSet<QString> supported = {
        QStringLiteral("MATCHPROP"), QStringLiteral("TRIM"), QStringLiteral("EXTEND"),
        QStringLiteral("CHAMFER"), QStringLiteral("FILLET"),
        QStringLiteral("MOVE"), QStringLiteral("M"), QStringLiteral("TRANSLATE"), QStringLiteral("STRETCH"),
        QStringLiteral("COPY"), QStringLiteral("CO"), QStringLiteral("CP"), QStringLiteral("COPYBASE"), QStringLiteral("COPYCLIP"),
        QStringLiteral("ROTATE"), QStringLiteral("RO"), QStringLiteral("ROTATE2"),
        QStringLiteral("SCALE"), QStringLiteral("SC"), QStringLiteral("LENGTHEN"),
        QStringLiteral("MIRROR"), QStringLiteral("MI"), QStringLiteral("MIRRORH"), QStringLiteral("MIRRORV"), QStringLiteral("FLIPHORIZONTAL"), QStringLiteral("FLIPVERTICAL"),
        QStringLiteral("OFFSET"), QStringLiteral("O"), QStringLiteral("ARRAY"),
        QStringLiteral("ERASE"), QStringLiteral("E"), QStringLiteral("DELETE"), QStringLiteral("CUTCLIP"),
        QStringLiteral("PEDIT"), QStringLiteral("HATCHEDIT"), QStringLiteral("HATCHSELECT"), QStringLiteral("DDEDIT"), QStringLiteral("TEXTEDIT"), QStringLiteral("DIMCENTER"),
        QStringLiteral("DIMDIAMETER"), QStringLiteral("DIMRADIUS")
    };
    if (!supported.contains(cmd)) return false;

    if (m_textInlineEditor) finishTextInlineEdit(true);
    cancelDrawing();
    clearCadSelection();
    m_interactiveModifyCommand = cmd;
    m_interactiveModifyStage = 0;
    m_interactiveModifyFirstSelection.clear();
    m_interactiveModifyCurrentSelection.clear();
    m_validatedModifySelection.clear();
    m_pendingExtendSourceSelection.clear();
    m_pendingExtendBoundarySelection.clear();
    m_pendingMatchPropSourceSelection.clear();
    m_pendingMatchPropTargetSelection.clear();
    m_pendingSelectionClick = false;
    m_pendingSelectionWasSelected = false;
    m_pendingSelectionIndex = -1;
    m_pendingSelectionItem = nullptr;
    clearDimensionPickMarkers();
    setDrawingTool(DrawingTool::Noe);
    setCursor(Qt::ArrowCursor);
    logDrawEvent(QStringLiteral("MODIFY %1: AutoCAD selection mode active. Object click=select/deselect, Enter=next step, Esc=cancel.").arg(cmd));
    logInteractiveModifyPrompt();
    viewport()->update();
    return true;
}

void GraphicsView::cancelInteractiveModifyCommand()
{
    if (m_interactiveModifyCommand.isEmpty()) return;
    logDrawEvent(QStringLiteral("MODIFY %1: canceled.").arg(m_interactiveModifyCommand));
    m_interactiveModifyCommand.clear();
    m_interactiveModifyStage = 0;
    m_interactiveModifyFirstSelection.clear();
    m_interactiveModifyCurrentSelection.clear();
    m_validatedModifySelection.clear();
    m_pendingExtendSourceSelection.clear();
    m_pendingExtendBoundarySelection.clear();
    m_pendingMatchPropSourceSelection.clear();
    m_pendingMatchPropTargetSelection.clear();
    m_pendingSelectionClick = false;
    m_pendingSelectionWasSelected = false;
    m_pendingSelectionIndex = -1;
    m_pendingSelectionItem = nullptr;
    clearCadSelection();
    clearDimensionPickMarkers();
    viewport()->update();
    emitDrawPrompt();
}

void GraphicsView::cancelActiveAction()
{
    // ESC global type AutoCAD : annule toute action en cours et revient au mode sélection.
    // Couvre les commandes interactives (EXTEND/TRIM/etc.), les outils de dessin,
    // l'édition texte, le déplacement de sélection, le buffer clavier et les aperçus.
    if (m_textInlineEditor) {
        cancelTextInlineEdit();
    }

    const bool cancelledZoomWindow = m_zoomWindowActive;
    const QString cancelledModifyCommand = m_interactiveModifyCommand;
    m_interactiveModifyCommand.clear();
    m_interactiveModifyStage = 0;
    m_interactiveModifyFirstSelection.clear();
    m_interactiveModifyCurrentSelection.clear();
    m_validatedModifySelection.clear();
    m_pendingExtendSourceSelection.clear();
    m_pendingExtendBoundarySelection.clear();
    m_pendingMatchPropSourceSelection.clear();
    m_pendingMatchPropTargetSelection.clear();

    m_drawing = false;
    m_points.clear();
    m_keyboardInputBuffer.clear();
    clearPreviewItem();
    clearFixedAngleConstraint();

    m_movingSelection = false;
    m_selectionDragMoved = false;
    m_movingSelectionIndices.clear();

    m_pendingSelectionClick = false;
    m_pendingSelectionWasSelected = false;
    m_pendingSelectionIndex = -1;
    m_pendingSelectionItem = nullptr;
    clearDimensionPickMarkers();

    m_panning = false;
    m_zoomWindowActive = false;
    m_zoomWindowDragging = false;
    m_drawingTool = DrawingTool::Noe;
    setCursor(Qt::ArrowCursor);
    setDragMode(QGraphicsView::RubberBandDrag);

    clearCadSelection();

    if (cancelledZoomWindow) {
        logDrawEvent(QStringLiteral("ESC: Zoom window canceled. Returning to selection mode."));
    } else if (cancelledModifyCommand.isEmpty()) {
        logDrawEvent(QStringLiteral("ESC: action canceled. Returning to selection mode."));
    } else {
        logDrawEvent(QStringLiteral("ESC: command %1 canceled. Returning to selection mode.").arg(cancelledModifyCommand));
    }
    viewport()->update();
    emitDrawPrompt();
}

void GraphicsView::clearCadSelection()
{
    m_documentSelectionIndices.clear();
    if (scene()) scene()->clearSelection();
    viewport()->update();
}

void GraphicsView::selectEntityIndicesInScene(const QVector<int>& indices)
{
    QSet<int> wanted;
    for (int index : indices) if (index >= 0) wanted.insert(index);

    m_documentSelectionIndices.clear();
    m_documentSelectionIndices.reserve(wanted.size());
    for (int index : wanted) m_documentSelectionIndices.append(index);
    std::sort(m_documentSelectionIndices.begin(), m_documentSelectionIndices.end());

    if (scene()) {
        scene()->clearSelection();
        for (QGraphicsItem* item : scene()->items()) {
            if (!item || !item->data(1).isValid() || item->data(2).toBool()) continue;
            bool ok = false;
            const int index = item->data(1).toInt(&ok);
            if (ok && wanted.contains(index)) item->setSelected(true);
        }
    }
    viewport()->update();
}

bool GraphicsView::commitInteractiveModifyStage()
{
    if (m_interactiveModifyCommand.isEmpty()) return false;
    const QVector<int> current = operationSelectionIndices();
    const QString cmd = m_interactiveModifyCommand;

    if (m_interactiveModifyStage == 0) {
        if (cmd == QStringLiteral("EXTEND")) {
            if (current.size() != 1) {
                logDrawEvent(QStringLiteral("EXTEND: select exactly one open source entity with two endpoints, then press Enter."));
                return true;
            }
            CadEntity* sourceEntity = m_document ? m_document->entityAt(current.first()) : nullptr;
            if (!isExtendableSourceEntityForExtend(sourceEntity)) {
                logDrawEvent(QStringLiteral("EXTEND: the source must be open and have two endpoints. Closed entities cannot be extended."));
                return true;
            }
            m_interactiveModifyFirstSelection = current;
            m_interactiveModifyCurrentSelection.clear();
            clearCadSelection();
            m_interactiveModifyStage = 1;
            logDrawEvent(QStringLiteral("EXTEND: source #%1 validated. Now select the target boundary entity.").arg(current.first()));
            logInteractiveModifyPrompt();
            viewport()->update();
            return true;
        }

        if (current.isEmpty()) {
            logDrawEvent(QStringLiteral("%1: no selection. Select at least one object, then press Enter.").arg(cmd));
            return true;
        }
        if (cmd == QStringLiteral("MATCHPROP") || cmd == QStringLiteral("TRIM") || cmd == QStringLiteral("CHAMFER") || cmd == QStringLiteral("FILLET")) {
            if (current.size() != 1) {
                logDrawEvent(QStringLiteral("%1: select exactly one object for the first step, then press Enter.").arg(cmd));
                return true;
            }
            m_interactiveModifyFirstSelection = current;
            m_interactiveModifyCurrentSelection.clear();
            clearCadSelection();
            m_interactiveModifyStage = 1;
            logDrawEvent(QStringLiteral("%1: first selection validated (#%2).").arg(cmd).arg(current.first()));
            logInteractiveModifyPrompt();
            viewport()->update();
            return true;
        }
        m_interactiveModifyFirstSelection = current;
        m_validatedModifySelection = current;

        // Commandes Modify à sélection simple: sélection -> Entrée -> exécution.
        m_interactiveModifyCommand.clear();
        m_interactiveModifyStage = 0;
        m_interactiveModifyFirstSelection.clear();
        m_interactiveModifyCurrentSelection.clear();
        logDrawEvent(QStringLiteral("%1: selection validated (%2 object(s)). Executing...").arg(cmd).arg(current.size()));
        emit interactiveModifyCommandReady(cmd);
        emitDrawPrompt();
        return true;
    }

    if (m_interactiveModifyStage == 1) {
        if (cmd == QStringLiteral("EXTEND")) {
            if (current.size() != 1) {
                logDrawEvent(QStringLiteral("EXTEND: select exactly one target boundary entity, then press Enter."));
                return true;
            }
            if (m_interactiveModifyFirstSelection.isEmpty() || current.first() == m_interactiveModifyFirstSelection.first()) {
                logDrawEvent(QStringLiteral("EXTEND: the target boundary must be different from the source."));
                return true;
            }
            if (!m_document || !m_document->entityAt(current.first())) {
                logDrawEvent(QStringLiteral("EXTEND: invalid target boundary entity."));
                return true;
            }

            m_pendingExtendSourceSelection = m_interactiveModifyFirstSelection;
            m_pendingExtendBoundarySelection = current;

            QVector<int> finalSelection = m_pendingExtendSourceSelection;
            for (int index : m_pendingExtendBoundarySelection) if (!finalSelection.contains(index)) finalSelection.append(index);
            selectEntityIndicesInScene(finalSelection);

            m_interactiveModifyCommand.clear();
            m_interactiveModifyStage = 0;
            m_interactiveModifyFirstSelection.clear();
            m_interactiveModifyCurrentSelection.clear();
            logDrawEvent(QStringLiteral("EXTEND: source #%1 + boundary #%2 validated. Executing...")
                         .arg(m_pendingExtendSourceSelection.first())
                         .arg(m_pendingExtendBoundarySelection.first()));
            emit interactiveModifyCommandReady(cmd);
            emitDrawPrompt();
            return true;
        }

        if (current.isEmpty()) {
            logDrawEvent(QStringLiteral("%1: second selection is empty. Select the target object(s), then press Enter.").arg(cmd));
            return true;
        }
        if ((cmd == QStringLiteral("TRIM") || cmd == QStringLiteral("CHAMFER") || cmd == QStringLiteral("FILLET")) && current.size() != 1) {
            logDrawEvent(QStringLiteral("%1: select exactly one object for the second step, then press Enter.").arg(cmd));
            return true;
        }
        QVector<int> combined = m_interactiveModifyFirstSelection;
        for (int index : current) if (!combined.contains(index)) combined.append(index);
        if (combined.size() < 2) {
            logDrawEvent(QStringLiteral("%1: at least two objects are required to finish.").arg(cmd));
            return true;
        }
        if (cmd == QStringLiteral("MATCHPROP")) {
            m_pendingMatchPropSourceSelection = m_interactiveModifyFirstSelection;
            m_pendingMatchPropTargetSelection = current;
        }
        selectEntityIndicesInScene(combined);
        m_validatedModifySelection = combined;
        m_interactiveModifyCommand.clear();
        m_interactiveModifyStage = 0;
        m_interactiveModifyFirstSelection.clear();
        m_interactiveModifyCurrentSelection.clear();
        logDrawEvent(QStringLiteral("%1: final selection validated (%2 object(s)). Executing...").arg(cmd).arg(combined.size()));
        emit interactiveModifyCommandReady(cmd);
        emitDrawPrompt();
        return true;
    }

    return true;
}

QPointF GraphicsView::selectedItemsCenter() const
{
    QRectF bounds;
    bool first = true;

    if (m_document) {
        for (int index : selectedEntityIndices()) {
            const CadEntity* entity = m_document->entityAt(index);
            if (!entity) continue;
            QRectF b = entitySnapBounds(entity).normalized();
            if (!b.isValid() || b.isNull()) {
                const QPainterPath path = entitySelectionPath(entity);
                if (!path.isEmpty()) b = path.boundingRect().normalized();
            }
            if (!b.isValid() || b.isNull()) continue;
            bounds = first ? b : bounds.united(b);
            first = false;
        }
    } else if (scene()) {
        for (QGraphicsItem* item : scene()->selectedItems()) {
            if (!item || !item->data(1).isValid()) continue;
            const QRectF b = item->sceneBoundingRect();
            bounds = first ? b : bounds.united(b);
            first = false;
        }
    }

    return first ? QPointF() : bounds.center();
}

void GraphicsView::prepareForSceneReset()
{
    if (m_textInlineEditor) finishTextInlineEdit(true);
    // QGraphicsScene::clear() deletes items immediately. Any cached pointer to
    // preview/transient items must be invalidated before the scene is cleared.
    m_previewItem = nullptr;
    m_hasCurrentSnapMarker = false;
    m_drawing = false;
    m_points.clear();
    clearDimensionPickMarkers();
    m_movingSelection = false;
    m_selectionDragMoved = false;
    m_movingSelectionIndices.clear();
}

void GraphicsView::refreshFromDocumentPreservingSelection(const QVector<int>& indices)
{
    if (!m_document || !scene()) return;
    const QTransform oldTransform = transform();
    const QPointF oldCenter = mapToScene(viewport()->rect().center());

    prepareForSceneReset();

    DebugLogger::logDebug(QString("Refresh document scene, preserve selection count=%1").arg(indices.size()));
    const bool oldUpdates = viewport()->updatesEnabled();
    viewport()->setUpdatesEnabled(false);
    scene()->setItemIndexMethod(QGraphicsScene::NoIndex);
    m_document->renderToScene(scene());
    scene()->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
    updateSceneRectToContent();
    updateLevelOfDetail();

    setTransform(oldTransform);
    centerOn(oldCenter);

    selectEntityIndicesInScene(indices);
    viewport()->setUpdatesEnabled(oldUpdates);
    viewport()->update();
}

void GraphicsView::deleteSelectedEntities()
{
    if (!m_document) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    m_document->removeEntities(indices);
    refreshFromDocumentPreservingSelection();
}

void GraphicsView::copySelectedEntities(const QPointF& offset)
{
    if (!m_document) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    const int oldCount = m_document->entityCount();
    m_document->duplicateEntities(indices, offset);
    QVector<int> newSelection;
    for (int i = oldCount; i < m_document->entityCount(); ++i) newSelection.append(i);
    refreshFromDocumentPreservingSelection(newSelection);
}

void GraphicsView::moveSelectedEntities(const QPointF& offset)
{
    if (!m_document) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    m_document->translateEntities(indices, offset);
    refreshFromDocumentPreservingSelection(indices);
}

void GraphicsView::rotateSelectedEntities(double angleDeg)
{
    if (!m_document) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    m_document->rotateEntities(indices, selectedItemsCenter(), angleDeg);
    refreshFromDocumentPreservingSelection(indices);
}

void GraphicsView::scaleSelectedEntities(double factor)
{
    if (!m_document || std::abs(factor) < 1e-9) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    m_document->scaleEntities(indices, selectedItemsCenter(), factor);
    refreshFromDocumentPreservingSelection(indices);
}

void GraphicsView::mirrorSelectedEntitiesHorizontal()
{
    if (!m_document) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    const QPointF c = selectedItemsCenter();
    m_document->mirrorEntities(indices, QPointF(c.x() - 1.0, c.y()), QPointF(c.x() + 1.0, c.y()));
    refreshFromDocumentPreservingSelection(indices);
}

void GraphicsView::mirrorSelectedEntitiesVertical()
{
    if (!m_document) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    const QPointF c = selectedItemsCenter();
    m_document->mirrorEntities(indices, QPointF(c.x(), c.y() - 1.0), QPointF(c.x(), c.y() + 1.0));
    refreshFromDocumentPreservingSelection(indices);
}

void GraphicsView::moveSelectedEntitiesToLayer(const QString& layerName)
{
    if (!m_document) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;
    m_document->moveEntitiesToLayer(indices, layerName);
    refreshFromDocumentPreservingSelection(indices);
}

void GraphicsView::wheelEvent(QWheelEvent* event)
{
    if (!event) return;

    // CAD navigation policy:
    //   wheel              = smooth zoom, anchored on the exact mouse cursor
    //   Ctrl + wheel       = precision zoom for small details
    //   Shift + wheel      = horizontal pan
    //   Alt + wheel        = vertical pan
    //
    // The previous implementation used the raw wheel delta directly. On many
    // mice/touchpads that made a single wheel tick too aggressive and the
    // visible point under the cursor drifted. Here we normalize the wheel to
    // "notches", clamp large bursts, then correct the camera after scaling so
    // the scene point under the cursor remains fixed.
    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();

    double primaryDelta = 0.0;
    bool pixelBased = false;
    if (!pixelDelta.isNull()) {
        primaryDelta = static_cast<double>(pixelDelta.y());
        pixelBased = true;
    } else if (!angleDelta.isNull()) {
        primaryDelta = static_cast<double>(angleDelta.y());
    }

    if (event->inverted()) primaryDelta = -primaryDelta;
    if (!std::isfinite(primaryDelta) || std::abs(primaryDelta) < 1.0e-9) {
        event->ignore();
        return;
    }

    beginInteractiveNavigation();

    const bool shiftPan = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool altPan = event->modifiers().testFlag(Qt::AltModifier);
    if (shiftPan || altPan) {
        const double rawPan = pixelBased
            ? (shiftPan ? static_cast<double>(pixelDelta.x() != 0 ? pixelDelta.x() : pixelDelta.y())
                        : static_cast<double>(pixelDelta.y()))
            : (shiftPan ? static_cast<double>(angleDelta.x() != 0 ? angleDelta.x() : angleDelta.y()) * 0.28
                        : static_cast<double>(angleDelta.y()) * 0.28);
        const int pixels = qBound(-180, static_cast<int>(std::round(rawPan)), 180);
        if (shiftPan) panByViewportDelta(QPoint(pixels, 0));
        else panByViewportDelta(QPoint(0, pixels));
        if (m_navigationRestoreTimer) m_navigationRestoreTimer->start(140);
        event->accept();
        return;
    }

    const bool fineZoom = event->modifiers().testFlag(Qt::ControlModifier);

    // One normal wheel notch is 120 units. Pixel deltas come from touchpads;
    // divide by a larger value to avoid sudden jumps.
    double notches = pixelBased ? (primaryDelta / 96.0) : (primaryDelta / 120.0);
    notches = std::clamp(notches, -2.0, 2.0);

    // 7% per notch feels close to desktop CAD zooming; Ctrl gives precise 2%.
    const double step = fineZoom ? 1.020 : 1.070;
    double factor = std::pow(step, notches);
    factor = std::clamp(factor, fineZoom ? 0.94 : 0.86, fineZoom ? 1.065 : 1.16);

    const QPoint anchor = event->position().toPoint();
    zoomByAt(factor, anchor);
    if (m_navigationRestoreTimer) m_navigationRestoreTimer->start(140);
    event->accept();
}

QRectF GraphicsView::safeRectForView(const QRectF& rect) const
{
    QRectF r = rect.normalized();
    if (!r.isValid() || r.isNull()) {
        return QRectF(-100.0, -100.0, 200.0, 200.0);
    }

    constexpr double minSize = 1.0;
    if (r.width() < minSize) {
        const double dx = (minSize - r.width()) * 0.5;
        r.adjust(-dx, 0.0, dx, 0.0);
    }
    if (r.height() < minSize) {
        const double dy = (minSize - r.height()) * 0.5;
        r.adjust(0.0, -dy, 0.0, dy);
    }
    return r;
}

QRectF GraphicsView::contentBoundingRect() const
{
    if (!scene()) {
        return QRectF(-100.0, -100.0, 200.0, 200.0);
    }

    QRectF bounds;
    if (m_document && m_document->entityCount() > 0) {
        bounds = m_document->limits();
    }
    if ((!bounds.isValid() || bounds.isNull()) && m_cachedContentBounds.isValid() && !m_cachedContentBounds.isNull()) {
        bounds = m_cachedContentBounds;
    }
    if ((!bounds.isValid() || bounds.isNull()) && !m_largeDocumentMode) {
        bounds = scene()->itemsBoundingRect();
    }
    if (!bounds.isValid() || bounds.isNull()) {
        bounds = scene()->sceneRect();
    }
    if (!bounds.isValid() || bounds.isNull()) {
        bounds = QRectF(-100.0, -100.0, 200.0, 200.0);
    }
    return safeRectForView(bounds);
}


QRectF GraphicsView::smartImportBoundingRect() const
{
    if (!scene()) return contentBoundingRect();

    QVector<QRectF> rects;
    rects.reserve(scene()->items().size());

    QRectF all;
    bool first = true;
    for (QGraphicsItem* item : scene()->items()) {
        if (!item || !item->isVisible()) continue;
        QRectF r = item->sceneBoundingRect().normalized();
        if (!r.isValid() || r.isNull()) continue;
        if (!std::isfinite(r.left()) || !std::isfinite(r.right()) ||
            !std::isfinite(r.top()) || !std::isfinite(r.bottom())) {
            continue;
        }
        // Ignore pathological bounds produced by malformed/recovered DWG objects.
        if (r.width() > 1.0e12 || r.height() > 1.0e12) continue;
        rects.append(r);
        all = first ? r : all.united(r);
        first = false;
    }

    if (rects.isEmpty()) return contentBoundingRect();
    if (rects.size() < 8) return safeRectForView(all);

    QVector<double> lefts, rights, tops, bottoms, widths, heights;
    lefts.reserve(rects.size()); rights.reserve(rects.size());
    tops.reserve(rects.size()); bottoms.reserve(rects.size());
    widths.reserve(rects.size()); heights.reserve(rects.size());
    for (const QRectF& r : rects) {
        lefts.append(r.left()); rights.append(r.right());
        tops.append(r.top()); bottoms.append(r.bottom());
        widths.append(r.width()); heights.append(r.height());
    }
    auto percentile = [](QVector<double> v, double p) -> double {
        if (v.isEmpty()) return 0.0;
        std::sort(v.begin(), v.end());
        const double pos = std::clamp(p, 0.0, 1.0) * static_cast<double>(v.size() - 1);
        const int lo = static_cast<int>(std::floor(pos));
        const int hi = static_cast<int>(std::ceil(pos));
        if (lo == hi) return v.at(lo);
        const double t = pos - lo;
        return v.at(lo) * (1.0 - t) + v.at(hi) * t;
    };

    QRectF trimmed(QPointF(percentile(lefts, 0.03), percentile(tops, 0.03)),
                   QPointF(percentile(rights, 0.97), percentile(bottoms, 0.97)));
    trimmed = trimmed.normalized();

    const double medW = std::max(1.0, percentile(widths, 0.50));
    const double medH = std::max(1.0, percentile(heights, 0.50));
    const double pad = std::max({trimmed.width(), trimmed.height(), medW, medH}) * 0.08 + std::max(medW, medH) * 2.0 + 5.0;
    trimmed.adjust(-pad, -pad, pad, pad);
    trimmed = safeRectForView(trimmed);

    int inside = 0;
    for (const QRectF& r : rects) {
        if (trimmed.intersects(r) || trimmed.contains(r.center())) ++inside;
    }
    const double insideRatio = static_cast<double>(inside) / static_cast<double>(rects.size());
    const double allArea = std::max(1.0, all.width() * all.height());
    const double trimmedArea = std::max(1.0, trimmed.width() * trimmed.height());

    // Use the trimmed bounds only when they clearly remove distant outliers.
    // This avoids the common DWG case where one invalid/recovered object makes
    // the whole drawing appear extremely small after opening.
    if (insideRatio >= 0.70 && allArea > trimmedArea * 6.0) {
        DebugLogger::logInfo(QString("Zoom import intelligent: limites globales tres grandes, utilisation limites filtrees. all=%1x%2 trimmed=%3x%4 inside=%5/%6")
                             .arg(all.width()).arg(all.height())
                             .arg(trimmed.width()).arg(trimmed.height())
                             .arg(inside).arg(rects.size()));
        return trimmed;
    }

    return safeRectForView(all);
}


void GraphicsView::updateSceneRectToContent()
{
    if (!scene()) return;

    QRectF bounds = contentBoundingRect();
    const double margin = std::max(bounds.width(), bounds.height()) * 0.05 + 20.0;
    bounds.adjust(-margin, -margin, margin, margin);
    m_cachedContentBounds = safeRectForView(bounds);
    scene()->setSceneRect(m_cachedContentBounds);
    DebugLogger::logDebug(QString("SceneRect ajustee au contenu: x=%1 y=%2 w=%3 h=%4")
                          .arg(scene()->sceneRect().x())
                          .arg(scene()->sceneRect().y())
                          .arg(scene()->sceneRect().width())
                          .arg(scene()->sceneRect().height()));
}

void GraphicsView::zoomBy(double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0) return;
    if (viewport()) {
        zoomByAt(factor, viewport()->rect().center());
        return;
    }

    const double currentScale = transform().m11();
    constexpr double minScale = 1.0e-7;
    constexpr double maxScale = 1.0e7;
    double targetScale = currentScale * factor;

    if (targetScale < minScale) factor = minScale / currentScale;
    if (targetScale > maxScale) factor = maxScale / currentScale;

    if (!std::isfinite(factor) || factor <= 0.0) return;
    scale(factor, factor);
    updateLevelOfDetail();
}

void GraphicsView::zoomByAt(double factor, const QPoint& viewportAnchor)
{
    if (!std::isfinite(factor) || factor <= 0.0 || !viewport()) return;

    const QPoint anchor(
        qBound(0, viewportAnchor.x(), qMax(0, viewport()->width() - 1)),
        qBound(0, viewportAnchor.y(), qMax(0, viewport()->height() - 1))
    );

    const QPointF sceneAnchorBefore = viewportPointToScenePrecise(anchor);
    if (!std::isfinite(sceneAnchorBefore.x()) || !std::isfinite(sceneAnchorBefore.y())) return;

    const QTransform oldT = transform();
    const double sx = oldT.m11();
    const double sy = oldT.m22();
    if (!std::isfinite(sx) || !std::isfinite(sy) ||
        std::abs(sx) < 1.0e-20 || std::abs(sy) < 1.0e-20) {
        return;
    }

    // Clamp by absolute view scale. This prevents unstable transforms when the
    // user zooms very far in/out on large engineering drawings.
    constexpr double minAbsScale = 1.0e-8;
    constexpr double maxAbsScale = 1.0e8;
    const double requestedScale = std::abs(sx * factor);
    if (requestedScale < minAbsScale) {
        factor = minAbsScale / std::max(std::abs(sx), 1.0e-20);
    } else if (requestedScale > maxAbsScale) {
        factor = maxAbsScale / std::max(std::abs(sx), 1.0e-20);
    }
    if (!std::isfinite(factor) || factor <= 0.0) return;

    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);

    QTransform t = oldT;
    t.scale(factor, factor);
    if (!std::isfinite(t.m11()) || !std::isfinite(t.m22()) ||
        !std::isfinite(t.dx()) || !std::isfinite(t.dy())) {
        return;
    }
    setTransform(t);
    resetScrollbarsForStableCamera();

    // Correct the residual error after scaling. This makes the zoom pointy:
    // the exact scene coordinate under the mouse remains under the mouse.
    for (int i = 0; i < 3; ++i) {
        const QPointF sceneAfter = viewportPointToScenePrecise(anchor);
        const QPointF err = sceneAfter - sceneAnchorBefore;
        if (!std::isfinite(err.x()) || !std::isfinite(err.y())) break;

        const double sceneTol = std::max(1.0e-9, 0.25 / std::max(std::abs(transform().m11()), 1.0e-12));
        if (std::hypot(err.x(), err.y()) <= sceneTol) break;

        QTransform corrected = transform();
        corrected.translate(err.x(), err.y());
        if (!std::isfinite(corrected.dx()) || !std::isfinite(corrected.dy())) break;
        setTransform(corrected);
        resetScrollbarsForStableCamera();
    }

    if (m_largeDocumentMode && m_interactiveNavigation) m_lastLodScale = -1.0;
    else updateLevelOfDetail();

    updateTextInlineEditorGeometry();
    const QPointF sceneAnchorAfter = viewportPointToScenePrecise(anchor);
    m_lastCursorScenePos = sceneAnchorAfter;
    m_hasLastCursorScenePos = true;
    emit mouseScenePositionChanged(sceneAnchorAfter);
    viewport()->update();
}

void GraphicsView::zoomIn()
{
    zoomBy(1.10);
}

void GraphicsView::zoomOut()
{
    zoomBy(1.0 / 1.10);
}

void GraphicsView::zoomToExtents()
{
    if (!scene()) return;

    updateSceneRectToContent();
    const QRectF bounds = contentBoundingRect();
    if (!bounds.isValid() || bounds.isNull()) return;

    fitSceneRectStable(bounds, 0.05);
    DebugLogger::logInfo(QString("Zoom etendu stable applique: x=%1 y=%2 w=%3 h=%4 scale=%5")
                         .arg(bounds.x()).arg(bounds.y())
                         .arg(bounds.width()).arg(bounds.height())
                         .arg(transform().m11()));
}


void GraphicsView::zoomToImportExtents()
{
    if (!scene()) return;

    QRectF bounds = smartImportBoundingRect();
    if (!bounds.isValid() || bounds.isNull()) bounds = contentBoundingRect();
    if (!bounds.isValid() || bounds.isNull()) return;

    m_cachedContentBounds = safeRectForView(bounds.adjusted(-20.0, -20.0, 20.0, 20.0));
    scene()->setSceneRect(m_cachedContentBounds);

    fitSceneRectStable(bounds, 0.06);

    DebugLogger::logInfo(QString("Zoom automatique stable apres ouverture: x=%1 y=%2 w=%3 h=%4 scale=%5")
                         .arg(bounds.x()).arg(bounds.y())
                         .arg(bounds.width()).arg(bounds.height())
                         .arg(transform().m11()));
}

void GraphicsView::resetZoom()
{
    fitSceneRectStable(contentBoundingRect(), 0.05);
}

void GraphicsView::startZoomWindowMode()
{
    if (m_textInlineEditor) finishTextInlineEdit(true);

    // La commande ZOOM WINDOW doit prendre la main sur les modes dessin/modifier
    // sans déclencher de change de géométrie ni de sélection.
    m_interactiveModifyCommand.clear();
    m_interactiveModifyStage = 0;
    m_interactiveModifyFirstSelection.clear();
    m_interactiveModifyCurrentSelection.clear();
    m_validatedModifySelection.clear();
    m_pendingExtendSourceSelection.clear();
    m_pendingExtendBoundarySelection.clear();
    m_pendingMatchPropSourceSelection.clear();
    m_pendingMatchPropTargetSelection.clear();

    m_drawing = false;
    m_points.clear();
    clearPreviewItem();
    clearDimensionPickMarkers();
    m_keyboardInputBuffer.clear();
    clearFixedAngleConstraint();
    m_drawingTool = DrawingTool::Noe;

    m_movingSelection = false;
    m_selectionDragMoved = false;
    m_movingSelectionIndices.clear();
    m_pendingSelectionClick = false;
    m_pendingSelectionWasSelected = false;
    m_pendingSelectionIndex = -1;
    m_pendingSelectionItem = nullptr;

    m_zoomWindowActive = true;
    m_zoomWindowDragging = false;
    m_zoomWindowStart = QPoint();
    m_zoomWindowEnd = QPoint();

    setCursor(Qt::CrossCursor);
    setDragMode(QGraphicsView::NoDrag);
    logDrawEvent(QStringLiteral("ZOOM WINDOW: click-drag a window; release to zoom; Esc cancels."));
    viewport()->update();
    emitDrawPrompt();
}


QPointF GraphicsView::viewportPointToScenePrecise(const QPoint& viewportPoint) const
{
    // Prefer QGraphicsView::mapToScene because it includes Qt's viewport,
    // alignment and scrollbar internals. This is essential immediately after
    // fitInView()/Zoom Extents; using only transform().inverted() can ignore
    // hidden scrollbar offsets and makes cursor-centered zoom drift.
    const QPointF mapped = mapToScene(viewportPoint);
    if (std::isfinite(mapped.x()) && std::isfinite(mapped.y())) return mapped;

    bool inverseOk = false;
    const QTransform inv = viewportTransform().inverted(&inverseOk);
    if (inverseOk) {
        const QPointF p = inv.map(QPointF(viewportPoint));
        if (std::isfinite(p.x()) && std::isfinite(p.y())) return p;
    }
    return QPointF();
}

void GraphicsView::setCameraTransformAt(const QPointF& sceneAnchor, const QPoint& viewportAnchor, double scaleX, double scaleY)
{
    if (!std::isfinite(sceneAnchor.x()) || !std::isfinite(sceneAnchor.y()) ||
        !std::isfinite(scaleX) || !std::isfinite(scaleY) ||
        std::abs(scaleX) < 1.0e-20 || std::abs(scaleY) < 1.0e-20) {
        return;
    }

    const double tx = static_cast<double>(viewportAnchor.x()) - scaleX * sceneAnchor.x();
    const double ty = static_cast<double>(viewportAnchor.y()) - scaleY * sceneAnchor.y();
    if (!std::isfinite(tx) || !std::isfinite(ty)) return;

    QTransform t(scaleX, 0.0, 0.0, scaleY, tx, ty);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setTransform(t);
    resetScrollbarsForStableCamera();
}

void GraphicsView::panByViewportDelta(const QPoint& delta)
{
    if (delta.isNull()) return;
    const QTransform oldT = transform();
    if (!std::isfinite(oldT.m11()) || !std::isfinite(oldT.m22()) ||
        !std::isfinite(oldT.dx()) || !std::isfinite(oldT.dy())) {
        return;
    }

    QTransform t(oldT.m11(), oldT.m12(), oldT.m21(), oldT.m22(),
                 oldT.dx() + static_cast<double>(delta.x()),
                 oldT.dy() + static_cast<double>(delta.y()));
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setTransform(t);
    resetScrollbarsForStableCamera();
    updateTextInlineEditorGeometry();
    viewport()->update();
}

void GraphicsView::resetScrollbarsForStableCamera()
{
    // Les scrollbars sont masquées, mais QGraphicsView les utilise quand même
    // dans mapToScene()/centerOn(). Les garder à zéro stabilise la précision
    // de la caméra sur des coordonnées CAD très grandes.
    QSignalBlocker hBlocker(horizontalScrollBar());
    QSignalBlocker vBlocker(verticalScrollBar());
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void GraphicsView::fitSceneRectStable(const QRectF& rect, double marginRatio)
{
    if (!viewport()) return;
    QRectF r = safeRectForView(rect);
    const double maxDim = std::max(r.width(), r.height());
    const double margin = maxDim * std::max(0.0, marginRatio) + 1.0;
    r.adjust(-margin, -margin, margin, margin);
    r = safeRectForView(r);

    const QSize vpSize = viewport()->size();
    if (vpSize.width() < 2 || vpSize.height() < 2) return;

    const double sx = static_cast<double>(vpSize.width()) / r.width();
    const double sy = static_cast<double>(vpSize.height()) / r.height();
    double s = std::min(sx, sy);
    if (!std::isfinite(s) || s <= 0.0) return;
    s = qBound(1.0e-9, s, 1.0e9);

    const double tx = static_cast<double>(vpSize.width()) * 0.5 - s * r.center().x();
    const double ty = static_cast<double>(vpSize.height()) * 0.5 - s * r.center().y();
    QTransform t(s, 0.0, 0.0, s, tx, ty);

    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setTransform(t);
    resetScrollbarsForStableCamera();
    updateLevelOfDetail();
    updateTextInlineEditorGeometry();
    viewport()->update();
}

void GraphicsView::setPerformanceModeEnabled(bool enabled)
{
    m_performanceModeEnabled = enabled;
    configureForDocumentSize(m_document ? m_document->entityCount() : 0);
}

void GraphicsView::beginInteractiveNavigation()
{
    if (!m_performanceModeEnabled || !m_largeDocumentMode || m_interactiveNavigation) return;
    m_interactiveNavigation = true;

    // Retour au comportement de l'ancienne version plus fluide: pendant la
    // molette/pan, on rend uniquement la géométrie principale. Les détails
    // lourds (textes, hachures, dimensions complexes, blocs proxy) reviennent
    // automatiquement dès que la navigation s'arrête.
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setRenderHints(QPainter::TextAntialiasing);
    if (scene()) {
        scene()->setMinimumRenderSize(7.0);
        const QList<QGraphicsItem*> allItems = scene()->items();
        for (QGraphicsItem* item : allItems) {
            if (!item) continue;
            const int detailClass = item->data(3).toInt();
            if (detailClass > 0) item->setVisible(false);
        }
    }
}

void GraphicsView::endInteractiveNavigation()
{
    if (!m_interactiveNavigation) return;
    m_interactiveNavigation = false;
    setViewportUpdateMode(m_largeDocumentMode ? QGraphicsView::SmartViewportUpdate : QGraphicsView::MinimalViewportUpdate);
    if (m_largeDocumentMode) setRenderHints(QPainter::TextAntialiasing);
    else setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    updateLevelOfDetail();
    viewport()->update();
}

void GraphicsView::configureForDocumentSize(int entityCount)
{
    m_largeDocumentMode = m_performanceModeEnabled && entityCount > 10000;
    m_lastLodScale = -1.0;
    if (scene()) {
        scene()->setMinimumRenderSize(m_largeDocumentMode ? 1.0 : 0.0);
        scene()->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
        if (entityCount > 50000) scene()->setBspTreeDepth(16);
        else if (entityCount > 10000) scene()->setBspTreeDepth(12);
        else scene()->setBspTreeDepth(8);
    }

    if (m_largeDocumentMode) {
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
        setRenderHints(QPainter::TextAntialiasing);
        setObjectSnapEnabled(true);
        DebugLogger::logInfo(QString("Large DWG mode active: %1 entities, BSP + smooth LOD + stable mouse zoom")
                             .arg(entityCount));
    } else {
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
        setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, false);
        setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        setObjectSnapEnabled(true);
    }
    updateLevelOfDetail();
}

void GraphicsView::updateLevelOfDetail()
{
    if (!scene() || !m_largeDocumentMode || m_interactiveNavigation) return;

    const double scaleValue = std::abs(transform().m11());
    if (m_lastLodScale > 0.0) {
        const double ratio = scaleValue / m_lastLodScale;
        if (ratio > 0.90 && ratio < 1.12) return;
    }
    m_lastLodScale = scaleValue;

    // Culling global: plus on est loin, plus on ignore automatiquement les
    // primitives qui feraient moins de quelques pixels. Les textes/hachures
    // ne sont plus masqués par boucle O(n): leurs items LOD se dessinent en
    // proxy carré ou contour vide selon le zoom.
    double minRenderSize = 0.12;
    if (scaleValue < 0.0015) minRenderSize = 1.20;
    else if (scaleValue < 0.004) minRenderSize = 0.95;
    else if (scaleValue < 0.012) minRenderSize = 0.60;
    else if (scaleValue < 0.05) minRenderSize = 0.35;
    else if (scaleValue < 0.20) minRenderSize = 0.16;
    scene()->setMinimumRenderSize(minRenderSize);

    // Sur fichiers moyens, on garde encore le masquage des blocs proxy. Sur
    // très gros fichiers, éviter scene()->items() ici est essentiel pour que
    // la molette et le pan restent fluides.
    if (m_document && m_document->entityCount() > 20000) return;

    const bool showBlocks = scaleValue >= 0.012;
    const QList<QGraphicsItem*> allItems = scene()->items();
    for (QGraphicsItem* item : allItems) {
        if (!item) continue;
        const int detailClass = item->data(3).toInt();
        if (detailClass == 3) item->setVisible(showBlocks);
        else item->setVisible(true);
    }
}

void GraphicsView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        zoomToExtents();
        logDrawEvent(QStringLiteral("ZOOM EXTENTS: middle-button double click."));
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* item = cadItemAt(event->pos());
        int index = -1;
        if (item && item->data(1).isValid()) {
            bool ok = false;
            const int itemIndex = item->data(1).toInt(&ok);
            if (ok && itemIndex >= 0 && m_document && dynamic_cast<CadText*>(m_document->entityAt(itemIndex))) {
                index = itemIndex;
            }
        }
        if (index < 0) index = textEntityIndexAt(event->pos());
        if (index >= 0) {
            // Recherche de l'item correspondant pour le clignotement.
            QGraphicsItem* sourceItem = item;
            if (!sourceItem || sourceItem->data(1).toInt() != index) {
                for (QGraphicsItem* candidate : items(event->pos())) {
                    if (candidate && candidate->data(1).isValid() && candidate->data(1).toInt() == index) {
                        sourceItem = candidate;
                        break;
                    }
                }
            }
            startTextInlineEdit(index, sourceItem);
            event->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void GraphicsView::mousePressEvent(QMouseEvent* event)
{
    if (m_textInlineEditor) {
        finishTextInlineEdit(true);
    }

    if (m_zoomWindowActive) {
        if (event->button() == Qt::LeftButton) {
            m_zoomWindowDragging = true;
            m_zoomWindowStart = event->pos();
            m_zoomWindowEnd = event->pos();
            viewport()->update();
            event->accept();
            emitDrawPrompt();
            return;
        }
        if (event->button() == Qt::RightButton) {
            cancelActiveAction();
            event->accept();
            return;
        }
    }

    const bool panButton =
        event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton &&
         (event->modifiers() & Qt::AltModifier || m_spacePanActive));

    if (panButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    const QPointF rawScenePos = mapToScene(event->pos());
    QPointF scenePos = effectiveScenePoint(rawScenePos);
    m_lastCursorScenePos = scenePos;
    m_hasLastCursorScenePos = true;

    auto isPointListTool = [this]() {
        return m_drawingTool == DrawingTool::Polyline ||
               m_drawingTool == DrawingTool::Spline ||
               m_drawingTool == DrawingTool::Hatch;
    };

    auto isTwoPointTool = [this]() {
        return m_drawingTool == DrawingTool::Line ||
               m_drawingTool == DrawingTool::Circle ||
               m_drawingTool == DrawingTool::Rectangle ||
               m_drawingTool == DrawingTool::Ellipse ||
               m_drawingTool == DrawingTool::Polygon;
    };

    auto isClickSequenceTool = [this]() {
        return m_drawingTool == DrawingTool::CircleDiameter ||
               m_drawingTool == DrawingTool::Circle3Points ||
               m_drawingTool == DrawingTool::Arc3Points ||
               m_drawingTool == DrawingTool::LinearDimension ||
               m_drawingTool == DrawingTool::Leader;
    };

    // Mode Modifier: clic droit = Entrée / validation de l'étape courante.
    if (!m_interactiveModifyCommand.isEmpty() && event->button() == Qt::RightButton) {
        commitInteractiveModifyStage();
        event->accept();
        return;
    }

    // Comportement AutoCAD classique: clic droit rapide = Entrée pendant une
    // commande; sans commande active il répète la dernière commande. Le guide
    // AutoCAD 2008 décrit explicitement ce dialogue souris/clavier.
    if (event->button() == Qt::RightButton) {
        if (isPointListTool() && m_drawing) {
            finishPolyline(false);
            logDrawEvent(QStringLiteral("Clic droit = ENTER: fin de %1").arg(drawingToolName(m_drawingTool)));
            emitDrawPrompt();
            event->accept();
            return;
        }
        if (m_drawingTool != DrawingTool::Noe) {
            if (m_drawing) {
                logDrawEvent(QStringLiteral("Right click = ENTER: validating command %1").arg(drawingToolName(m_drawingTool)));
                if (isClickSequenceTool()) finishClickSequenceIfReady();
            } else {
                cancelDrawing();
                logDrawEvent(QStringLiteral("Right click = ESC: command canceled."));
            }
            emitDrawPrompt();
            event->accept();
            return;
        }
        repeatLastDrawCommand();
        event->accept();
        return;
    }

    if (!m_interactiveModifyCommand.isEmpty() && event->button() == Qt::LeftButton) {
        QGraphicsItem* hit = nullptr;
        const int index = cadEntityIndexAt(event->pos(), &hit, m_interactiveModifyCommand);
        if (index >= 0) {
            const bool singlePickStage =
                m_interactiveModifyCommand == QStringLiteral("EXTEND") ||
                ((m_interactiveModifyCommand == QStringLiteral("MATCHPROP") ||
                  m_interactiveModifyCommand == QStringLiteral("TRIM") ||
                  m_interactiveModifyCommand == QStringLiteral("CHAMFER") ||
                  m_interactiveModifyCommand == QStringLiteral("FILLET")) && m_interactiveModifyStage == 0);

            bool selectedNow = false;
            if (singlePickStage) {
                const bool sameAlreadySelected = m_interactiveModifyCurrentSelection.size() == 1 &&
                                                 m_interactiveModifyCurrentSelection.first() == index;
                m_interactiveModifyCurrentSelection.clear();
                if (!sameAlreadySelected) {
                    m_interactiveModifyCurrentSelection.append(index);
                    selectedNow = true;
                }
            } else {
                if (m_interactiveModifyCurrentSelection.contains(index)) {
                    m_interactiveModifyCurrentSelection.removeAll(index);
                    selectedNow = false;
                } else {
                    m_interactiveModifyCurrentSelection.append(index);
                    selectedNow = true;
                }
            }

            selectEntityIndicesInScene(m_interactiveModifyCurrentSelection);
            if (hit && !hit->data(2).toBool()) hit->setSelected(selectedNow);

            if (m_interactiveModifyCommand == QStringLiteral("DIMCENTER") ||
                m_interactiveModifyCommand == QStringLiteral("DIMDIAMETER") ||
                m_interactiveModifyCommand == QStringLiteral("DIMRADIUS")) {
                if (selectedNow) {
                    if (const auto* circle = dynamic_cast<const CadCircle*>(m_document ? m_document->entityAt(index) : nullptr)) {
                        clearDimensionPickMarkers();
                        addDimensionPickMarker(circle->center());
                        logDrawEvent(QStringLiteral("%1: circle #%2 detected without snap; small square placed at center.")
                                         .arg(m_interactiveModifyCommand)
                                         .arg(index));
                    }
                } else {
                    clearDimensionPickMarkers();
                }
            }

            logDrawEvent(QStringLiteral("%1: object #%2 %3 by geometry hit-test independent of snap.")
                             .arg(m_interactiveModifyCommand)
                             .arg(index)
                             .arg(selectedNow ? QStringLiteral("selected") : QStringLiteral("deselected")));
            viewport()->update();
        } else {
            logDrawEvent(QStringLiteral("%1: no compatible entity under the cursor. Detection without snap.").arg(m_interactiveModifyCommand));
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_drawingTool != DrawingTool::Noe) {
        logDrawEvent(QStringLiteral("Clic gauche %1 X=%2 Y=%3").arg(drawingToolName(m_drawingTool)).arg(scenePos.x(), 0, 'f', 4).arg(scenePos.y(), 0, 'f', 4));
        if (isPointListTool()) {
            if (!m_drawing) {
                m_drawing = true;
                m_points.clear();
            } else if (!m_points.isEmpty()) {
                const QPointF anchor = m_points.last();
                scenePos = effectiveScenePoint(rawScenePos, &anchor);
            }
            m_points.append(scenePos);
            updatePreviewItem(scenePos);
            event->accept();
            return;
        }

        if (m_drawingTool == DrawingTool::Text) {
            addCadEntity(makeCurrentTextEntity(scenePos));
            event->accept();
            return;
        }

        if (isClickSequenceTool()) {
            const int pointIndexBeforeAppend = m_drawing ? m_points.size() : 0;
            const bool dimensionOriginPoint =
                m_drawingTool == DrawingTool::LinearDimension && pointIndexBeforeAppend < 2;

            if (!m_drawing) {
                m_drawing = true;
                m_points.clear();
                if (m_drawingTool == DrawingTool::LinearDimension) {
                    clearDimensionPickMarkers();
                }
            } else if (!m_points.isEmpty() && !dimensionOriginPoint) {
                const QPointF anchor = m_points.last();
                scenePos = effectiveScenePoint(rawScenePos, &anchor);
            }

            if (dimensionOriginPoint) {
                QPointF detectedPoint;
                QString detectedLabel;
                int detectedEntityIndex = -1;
                if (dimensionReferencePointAt(event->pos(), rawScenePos, detectedPoint, &detectedLabel, &detectedEntityIndex)) {
                    scenePos = detectedPoint;
                    setCurrentSnapMarker(nullptr);
                    m_hasDimensionReferencePreview = false;
                    m_dimensionReferencePreviewLabel.clear();
                    addDimensionPickMarker(scenePos);
                    logDrawEvent(QStringLiteral("DIMENSION: %1 detected on entity #%2 by internal OSNAP auto-reference; reference square validated.")
                                     .arg(detectedLabel)
                                     .arg(detectedEntityIndex));
                } else {
                    scenePos = rawScenePos;
                    setCurrentSnapMarker(nullptr);
                    clearDimensionReferencePreview();
                    logDrawEvent(QStringLiteral("DIMENSION: no nearby reference; free point used."));
                }
            }

            m_points.append(scenePos);
            finishClickSequenceIfReady();
            if (m_drawing) updatePreviewItem(scenePos);
            event->accept();
            return;
        }

        if (isTwoPointTool()) {
            if (!m_drawing) {
                m_drawing = true;
                m_drawStart = scenePos;
                updatePreviewItem(m_drawStart);
            } else {
                // CAD-style two-click completion. This makes LINE/CIRCLE/RECTANGLE/
                // ELLIPSE/POLYGON work even without click-and-drag.
                QPointF endPoint = scenePos;
                if (m_drawingTool == DrawingTool::Line) {
                    endPoint = effectiveScenePoint(rawScenePos, &m_drawStart);
                }
                finishDragDrawing(endPoint);
            }
            event->accept();
            return;
        }

        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_drawingTool == DrawingTool::Noe) {
        QGraphicsItem* hit = nullptr;
        const int hitIndex = cadEntityIndexAt(event->pos(), &hit);
        const bool multiModifier = event->modifiers().testFlag(Qt::ShiftModifier) ||
                                   event->modifiers().testFlag(Qt::ControlModifier);

        if (hitIndex >= 0) {
            const bool alreadySelected = m_documentSelectionIndices.contains(hitIndex) ||
                                         (hit && hit->isSelected());
            m_pendingSelectionClick = true;
            m_pendingSelectionWasSelected = alreadySelected;
            m_pendingSelectionIndex = hitIndex;
            m_pendingSelectionItem = hit;
            m_pendingSelectionPressPos = event->pos();

            // Logique CAD: clic sur objet = sélection; deuxième clic sans déplacement = désélection.
            // Pour les DXF externes en rendu batch, hit peut être nul: la sélection reste alors
            // stockée dans m_documentSelectionIndices et dessinée en overlay.
            if (!alreadySelected) {
                if (!multiModifier) {
                    // Select additive par défaut pour travailler comme dans CAD:
                    // plusieurs objets peuvent être choisis successivement avant une commande Modify.
                }
                if (!m_documentSelectionIndices.contains(hitIndex)) {
                    m_documentSelectionIndices.append(hitIndex);
                    std::sort(m_documentSelectionIndices.begin(), m_documentSelectionIndices.end());
                }
                if (hit && !hit->data(2).toBool()) hit->setSelected(true);
                logDrawEvent(QStringLiteral("SELECTION: object #%1 selected by document hit-test.").arg(hitIndex));
            } else {
                logDrawEvent(QStringLiteral("SELECTION: object #%1 already selected; release without moving to deselect.").arg(hitIndex));
            }

            m_movingSelectionIndices = selectedEntityIndices();
            if (!m_movingSelectionIndices.isEmpty()) {
                m_movingSelection = true;
                m_selectionDragMoved = false;
                m_lastSelectionMovePoint = selectionMovePoint(event->pos());
                setCursor(Qt::ClosedHandCursor);
                viewport()->update();
                event->accept();
                return;
            }
        } else {
            m_pendingSelectionClick = false;
            m_pendingSelectionItem = nullptr;
            m_pendingSelectionIndex = -1;
            if (!multiModifier) {
                clearCadSelection();
                logDrawEvent(QStringLiteral("SELECTION: empty click, selection cleared."));
            }
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void GraphicsView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_zoomWindowActive) {
        if (m_zoomWindowDragging) {
            m_zoomWindowEnd = event->pos();
            viewport()->update();
        }
        const QPointF p = mapToScene(event->pos());
        m_lastCursorScenePos = p;
        m_hasLastCursorScenePos = true;
        emit mouseScenePositionChanged(p);
        event->accept();
        return;
    }

    if (m_panning) {
        beginInteractiveNavigation();
        const QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();
        // Pan CAD haute précision: déplacement direct de la caméra en pixels.
        // On ne dépend pas des scrollbars Qt, donc la navigation reste stable
        // même sur de très grands fichiers DXF/DWG.
        panByViewportDelta(delta);
        if (m_navigationRestoreTimer) m_navigationRestoreTimer->start(90);
        event->accept();
        return;
    }

    const QPointF rawScenePos = mapToScene(event->pos());

    // Gros DWG: le calcul d'accrochage objet complet peut devenir tres lourd
    // a chaque pixel de deplacement quand l'utilisateur zoome sur des micro-details.
    // Pour garder le dessin fluide, la preview utilise un point leger pendant le
    // mouvement souris; les clics/release restent precis et refont l'accrochage.
    auto lightweightDrawingPoint = [&](const QPointF& raw, const QPointF* anchor) {
        QPointF p = raw;
        if (m_snapEnabled) p = gridSnappedPoint(p);
        if (m_fixedAngleConstraintEnabled && anchor) {
            p = fixedAngleConstrainedPoint(*anchor, p);
            if (m_snapEnabled) p = gridSnappedPoint(p);
        } else if (m_orthoEnabled && anchor) {
            p = orthoConstrainedPoint(*anchor, p);
            if (m_snapEnabled) p = gridSnappedPoint(p);
        }
        setCurrentSnapMarker(nullptr);
        return p;
    };

    const bool forcePreciseOsnapOnMove = event->modifiers().testFlag(Qt::ControlModifier);
    QPointF scenePos = (m_largeDocumentMode && !forcePreciseOsnapOnMove && !m_movingSelection && !m_drawing)
                            ? lightweightDrawingPoint(rawScenePos, nullptr)
                            : effectiveScenePoint(rawScenePos);

    if (m_drawing && (m_drawingTool == DrawingTool::Line || m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch)) {
        const QPointF anchor = ((m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) && !m_points.isEmpty()) ? m_points.last() : m_drawStart;
        scenePos = (m_largeDocumentMode && !forcePreciseOsnapOnMove)
                       ? lightweightDrawingPoint(rawScenePos, &anchor)
                       : effectiveScenePoint(rawScenePos, &anchor);
    }

    if (m_drawingTool == DrawingTool::LinearDimension) {
        const int nextPointIndex = m_drawing ? m_points.size() : 0;
        if (nextPointIndex < 2) {
            QPointF detectedPoint;
            QString detectedLabel;
            int detectedEntityIndex = -1;
            if (dimensionReferencePointAt(event->pos(), rawScenePos, detectedPoint, &detectedLabel, &detectedEntityIndex)) {
                scenePos = detectedPoint;
                m_dimensionReferencePreviewPoint = detectedPoint;
                m_dimensionReferencePreviewLabel = detectedEntityIndex >= 0
                    ? QStringLiteral("%1 #%2").arg(detectedLabel).arg(detectedEntityIndex)
                    : detectedLabel;
                m_hasDimensionReferencePreview = true;
                setCurrentSnapMarker(nullptr);
            } else {
                scenePos = rawScenePos;
                clearDimensionReferencePreview();
                setCurrentSnapMarker(nullptr);
            }
        } else {
            clearDimensionReferencePreview();
            if (m_drawing && !m_points.isEmpty()) {
                scenePos = effectiveScenePoint(rawScenePos, &m_points.last());
            }
        }
    } else {
        clearDimensionReferencePreview();
    }

    m_lastCursorScenePos = scenePos;
    m_hasLastCursorScenePos = true;
    emit mouseScenePositionChanged(scenePos);

    if (m_movingSelection && m_document) {
        const QPointF currentPoint = selectionMovePoint(event->pos());
        const QPointF delta = currentPoint - m_lastSelectionMovePoint;
        if (std::abs(delta.x()) > 1e-9 || std::abs(delta.y()) > 1e-9) {
            m_document->translateEntities(m_movingSelectionIndices, delta);
            for (QGraphicsItem* item : scene()->selectedItems()) {
                if (item && item->data(1).isValid()) item->moveBy(delta.x(), delta.y());
            }
            m_lastSelectionMovePoint = currentPoint;
            m_selectionDragMoved = true;
            updateLevelOfDetail();
            viewport()->update();
        }
        event->accept();
        return;
    }

    if (m_drawing) {
        updatePreviewItem(scenePos);
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void GraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_zoomWindowActive && event->button() == Qt::LeftButton) {
        m_zoomWindowEnd = event->pos();
        finishZoomWindow(QRect(m_zoomWindowStart, m_zoomWindowEnd));
        event->accept();
        return;
    }

    if (m_panning) {
        m_panning = false;
        endInteractiveNavigation();
        setCursor(m_spacePanActive ? Qt::OpenHandCursor : (m_drawingTool == DrawingTool::Noe ? Qt::ArrowCursor : Qt::CrossCursor));
        event->accept();
        return;
    }

    if (m_movingSelection && event->button() == Qt::LeftButton) {
        m_movingSelection = false;
        if (m_selectionDragMoved) {
            refreshFromDocumentPreservingSelection(m_movingSelectionIndices);
        } else if (m_pendingSelectionClick && m_pendingSelectionWasSelected) {
            // Deuxième clic sur le même objet selected: désélection.
            const int currentIndex = m_pendingSelectionIndex;
            if (currentIndex >= 0) {
                m_documentSelectionIndices.removeAll(currentIndex);
                if (m_pendingSelectionItem) {
                    bool ok = false;
                    const int itemIndex = m_pendingSelectionItem->data(1).toInt(&ok);
                    if (ok && itemIndex == currentIndex) m_pendingSelectionItem->setSelected(false);
                }
                logDrawEvent(QStringLiteral("SELECTION: object #%1 deselected by second click.").arg(currentIndex));
            }
        }
        m_selectionDragMoved = false;
        m_movingSelectionIndices.clear();
        m_pendingSelectionClick = false;
        m_pendingSelectionWasSelected = false;
        m_pendingSelectionIndex = -1;
        m_pendingSelectionItem = nullptr;
        setCursor(m_drawingTool == DrawingTool::Noe ? Qt::ArrowCursor : Qt::CrossCursor);
        viewport()->update();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_drawing) {
        const bool pointListTool =
            m_drawingTool == DrawingTool::Polyline ||
            m_drawingTool == DrawingTool::Spline ||
            m_drawingTool == DrawingTool::Hatch;
        const bool clickSequenceTool =
            m_drawingTool == DrawingTool::CircleDiameter ||
            m_drawingTool == DrawingTool::Circle3Points ||
            m_drawingTool == DrawingTool::Arc3Points ||
            m_drawingTool == DrawingTool::LinearDimension ||
            m_drawingTool == DrawingTool::Leader ||
            m_drawingTool == DrawingTool::Text;

        if (pointListTool || clickSequenceTool) {
            event->accept();
            return;
        }

        QPointF endPoint = effectiveScenePoint(mapToScene(event->pos()));
        if (m_drawingTool == DrawingTool::Line) {
            endPoint = effectiveScenePoint(mapToScene(event->pos()), &m_drawStart);
        }

        // Support both CAD-style two-click drawing and classic drag drawing.
        // A tiny release after the first click keeps the command active; a real
        // drag finishes the entity immediately.
        const double minDragDistance = sceneToleranceFromView(this, 3.0);
        if (distanceBetween(m_drawStart, endPoint) > minDragDistance) {
            finishDragDrawing(endPoint);
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void GraphicsView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    updateTextInlineEditorGeometry();
}

bool GraphicsView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_textInlineEditor && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            cancelTextInlineEdit();
            return true;
        }
    }
    return QGraphicsView::eventFilter(watched, event);
}

void GraphicsView::keyPressEvent(QKeyEvent* event)
{
    if (m_textInlineEditor) {
        if (event->key() == Qt::Key_Escape) {
            cancelTextInlineEdit();
            event->accept();
            return;
        }
        QGraphicsView::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() &&
        m_drawingTool == DrawingTool::Noe && !m_drawing &&
        m_interactiveModifyCommand.isEmpty() && m_keyboardInputBuffer.isEmpty()) {
        m_spacePanActive = true;
        if (!m_panning) setCursor(Qt::OpenHandCursor);
        logDrawEvent(QStringLiteral("PAN: hold Space and drag with the left mouse button, or drag with the middle mouse button."));
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        cancelActiveAction();
        event->accept();
        return;
    }

    if (!m_interactiveModifyCommand.isEmpty()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            commitInteractiveModifyStage();
            event->accept();
            return;
        }
    }

    const bool pointListToolActive =
        (m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) && m_drawing;

    if (pointListToolActive && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (event->key() == Qt::Key_C) {
            m_keyboardInputBuffer.clear();
            finishPolyline(true);
            logDrawEvent(QStringLiteral("CLAVIER C: fermeture de %1").arg(drawingToolName(m_drawingTool)));
            emitDrawPrompt();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_U) {
            m_keyboardInputBuffer.clear();
            undoLastCommandPoint();
            event->accept();
            return;
        }
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_keyboardInputBuffer.trimmed().isEmpty()) {
            finishPolyline(false);
            logDrawEvent(QStringLiteral("CLAVIER ENTER: fin de %1").arg(drawingToolName(m_drawingTool)));
            event->accept();
            return;
        }
        if ((event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) && m_keyboardInputBuffer.isEmpty()) {
            undoLastCommandPoint();
            event->accept();
            return;
        }
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        m_keyboardInputBuffer.trimmed().isEmpty() && m_drawingTool == DrawingTool::Noe) {
        repeatLastDrawCommand();
        event->accept();
        return;
    }

    const bool hasPrintableText = !event->text().isEmpty() && event->text().at(0).unicode() >= 0x20;
    const bool commandBufferCandidate = hasPrintableText &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !m_keyboardInputBuffer.trimmed().isEmpty()) {
        const QString bufferedCommand = m_keyboardInputBuffer.trimmed();
        m_keyboardInputBuffer.clear();
        logDrawEvent(QStringLiteral("CLAVIER ENTER: %1").arg(bufferedCommand));
        processDrawConsoleCommand(bufferedCommand);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Backspace && !m_keyboardInputBuffer.isEmpty()) {
        m_keyboardInputBuffer.chop(1);
        logDrawEvent(QStringLiteral("CLAVIER buffer: %1").arg(m_keyboardInputBuffer));
        event->accept();
        return;
    }

    if (commandBufferCandidate && m_drawingTool != DrawingTool::Noe) {
        const QString allowed = QStringLiteral("0123456789.,;@<>+- ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_");
        const QString text = event->text();
        bool ok = true;
        for (const QChar ch : text) {
            if (!allowed.contains(ch)) { ok = false; break; }
        }
        if (ok) {
            m_keyboardInputBuffer += text;
            logDrawEvent(QStringLiteral("CLAVIER buffer: %1").arg(m_keyboardInputBuffer));
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_F7) {
        setGridEnabled(!m_gridEnabled);
        logDrawEvent(QStringLiteral("F7 GRID %1").arg(m_gridEnabled ? QStringLiteral("ON") : QStringLiteral("OFF")));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F8) {
        setOrthoEnabled(!m_orthoEnabled);
        logDrawEvent(QStringLiteral("F8 ORTHO %1").arg(m_orthoEnabled ? QStringLiteral("ON") : QStringLiteral("OFF")));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F9) {
        setSnapEnabled(!m_snapEnabled);
        logDrawEvent(QStringLiteral("F9 SNAP %1").arg(m_snapEnabled ? QStringLiteral("ON") : QStringLiteral("OFF")));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F3) {
        setObjectSnapEnabled(!m_objectSnapEnabled);
        logDrawEvent(QStringLiteral("F3 OSNAP %1").arg(m_objectSnapEnabled ? QStringLiteral("ON") : QStringLiteral("OFF")));
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) &&
        (m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) &&
        m_drawing && !m_points.isEmpty()) {
        m_points.removeLast();
        if (m_points.isEmpty()) {
            cancelDrawing();
        } else {
            updatePreviewItem(m_points.last());
        }
        emitDrawPrompt();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        m_keyboardInputBuffer.isEmpty() && m_drawingTool == DrawingTool::Noe) {
        deleteSelectedEntities();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy) && m_drawingTool == DrawingTool::Noe) {
        copySelectedEntities(QPointF(m_gridSpacing, m_gridSpacing));
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) && m_drawing) {
        finishPolyline(false);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_C &&
        (m_drawingTool == DrawingTool::Polyline || m_drawingTool == DrawingTool::Spline || m_drawingTool == DrawingTool::Hatch) && m_drawing) {
        finishPolyline(true);
        logDrawEvent(QStringLiteral("C: fermeture de %1").arg(drawingToolName(m_drawingTool)));
        emitDrawPrompt();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void GraphicsView::keyReleaseEvent(QKeyEvent* event)
{
    if (event && event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePanActive = false;
        if (!m_panning) setCursor(m_drawingTool == DrawingTool::Noe ? Qt::ArrowCursor : Qt::CrossCursor);
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void GraphicsView::updatePreviewItem(const QPointF& scenePos)
{
    if (!scene()) return;

    clearPreviewItem();

    switch (m_drawingTool) {
    case DrawingTool::Line:
        m_previewItem = scene()->addLine(QLineF(m_drawStart, scenePos), drawingPen(Qt::DashLine));
        break;
    case DrawingTool::Polyline:
    case DrawingTool::Spline:
        if (!m_points.isEmpty()) {
            auto* item = scene()->addPath(polylinePath(m_points, scenePos, true), drawingPen(Qt::DashLine));
            m_previewItem = item;
        }
        break;
    case DrawingTool::Hatch:
        if (!m_points.isEmpty()) {
            QPainterPath path = polylinePath(m_points, scenePos, true);
            if (m_points.size() >= 2) path.lineTo(m_points.first());
            auto* item = scene()->addPath(path, drawingPen(Qt::DashLine));
            QColor previewFill(255, 255, 255, 55);
            const QString previewPattern = m_hatchPattern.trimmed().toUpper();
            const bool previewSolid = previewPattern == QStringLiteral("SOLID") || previewPattern.startsWith(QStringLiteral("SOLID_"));
            Qt::BrushStyle previewStyle = Qt::FDiagPattern;
            if (previewSolid) previewStyle = Qt::SolidPattern;
            else if (previewPattern.contains(QStringLiteral("CROSS")) || previewPattern == QStringLiteral("BRICK") || previewPattern == QStringLiteral("GRID")) previewStyle = Qt::CrossPattern;
            else if (previewPattern == QStringLiteral("SAND") || previewPattern == QStringLiteral("SABLE") || previewPattern == QStringLiteral("CONCRETE") || previewPattern == QStringLiteral("BETON")) previewStyle = Qt::Dense6Pattern;
            else if (previewPattern == QStringLiteral("WOOD") || previewPattern == QStringLiteral("BOIS") || previewPattern == QStringLiteral("HORIZONTAL")) previewStyle = Qt::HorPattern;
            else if (previewPattern == QStringLiteral("VERTICAL")) previewStyle = Qt::VerPattern;
            item->setBrush(QBrush(previewFill, previewStyle));
            m_previewItem = item;
        }
        break;
    case DrawingTool::Circle: {
        auto* item = scene()->addEllipse(circleRect(m_drawStart, scenePos), drawingPen(Qt::DashLine));
        item->setBrush(Qt::NoBrush);
        m_previewItem = item;
        break;
    }
    case DrawingTool::CircleDiameter:
        if (!m_points.isEmpty()) {
            auto* item = scene()->addEllipse(circleDiameterRect(m_points.first(), scenePos), drawingPen(Qt::DashLine));
            item->setBrush(Qt::NoBrush);
            m_previewItem = item;
        }
        break;
    case DrawingTool::Circle3Points:
        if (m_points.size() == 1) {
            m_previewItem = scene()->addLine(QLineF(m_points.first(), scenePos), drawingPen(Qt::DashLine));
        } else if (m_points.size() == 2) {
            QPointF center; double radius = 0.0;
            if (circleThroughThreePoints(m_points.at(0), m_points.at(1), scenePos, center, radius)) {
                auto* item = scene()->addEllipse(QRectF(center.x() - radius, center.y() - radius,
                                                       2.0 * radius, 2.0 * radius), drawingPen(Qt::DashLine));
                item->setBrush(Qt::NoBrush);
                m_previewItem = item;
            }
        }
        break;
    case DrawingTool::Arc3Points:
        if (m_points.size() == 1) {
            m_previewItem = scene()->addLine(QLineF(m_points.first(), scenePos), drawingPen(Qt::DashLine));
        } else if (m_points.size() == 2) {
            QPointF center; double radius = 0.0; double startAngle = 0.0; double spanAngle = 0.0;
            if (arcFromThreePoints(m_points.at(0), m_points.at(1), scenePos, center, radius, startAngle, spanAngle)) {
                QRectF r(center.x() - radius, center.y() - radius, 2.0 * radius, 2.0 * radius);
                QPainterPath path;
                path.arcMoveTo(r, startAngle);
                path.arcTo(r, startAngle, spanAngle);
                m_previewItem = scene()->addPath(path, drawingPen(Qt::DashLine));
            }
        }
        break;
    case DrawingTool::Rectangle: {
        auto* item = scene()->addRect(normalizedRect(m_drawStart, scenePos), drawingPen(Qt::DashLine));
        item->setBrush(Qt::NoBrush);
        m_previewItem = item;
        break;
    }
    case DrawingTool::Ellipse: {
        auto* item = scene()->addEllipse(normalizedRect(m_drawStart, scenePos), drawingPen(Qt::DashLine));
        item->setBrush(Qt::NoBrush);
        m_previewItem = item;
        break;
    }
    case DrawingTool::Polygon: {
        QPolygonF polygon;
        const QVector<QPointF> pts = regularPolygonPoints(m_drawStart, scenePos);
        for (const QPointF& p : pts) polygon << p;
        auto* item = scene()->addPolygon(polygon, drawingPen(Qt::DashLine));
        item->setBrush(Qt::NoBrush);
        m_previewItem = item;
        break;
    }
    case DrawingTool::LinearDimension:
        if (m_points.size() == 1) {
            m_previewItem = scene()->addLine(QLineF(m_points.first(), scenePos), drawingPen(Qt::DashLine));
        } else if (m_points.size() == 2) {
            auto temp = std::make_unique<CadLinearDimension>(m_points.at(0), m_points.at(1), scenePos, m_annotationTextHeight);
            m_previewItem = temp->createGraphicsItem();
            scene()->addItem(m_previewItem);
        }
        break;
    case DrawingTool::Leader:
        if (m_points.size() == 1) {
            auto temp = std::make_unique<CadLeader>(m_points.at(0), scenePos, m_annotationText, m_annotationTextHeight);
            m_previewItem = temp->createGraphicsItem();
            scene()->addItem(m_previewItem);
        }
        break;
    case DrawingTool::Text: {
        auto temp = makeCurrentTextEntity(scenePos);
        m_previewItem = temp->createGraphicsItem();
        scene()->addItem(m_previewItem);
        break;
    }
    case DrawingTool::Noe:
        break;
    }
}

void GraphicsView::finishDragDrawing(const QPointF& scenePos)
{
    if (!scene()) return;

    const QRectF bounds = normalizedRect(m_drawStart, scenePos);
    const bool tooSmall = bounds.width() < 0.001 && bounds.height() < 0.001;
    clearPreviewItem();

    if (!tooSmall) {
        std::unique_ptr<CadEntity> cadEntity;

        switch (m_drawingTool) {
        case DrawingTool::Line:
            cadEntity = std::make_unique<CadLine>(m_drawStart, scenePos);
            break;
        case DrawingTool::Circle:
            cadEntity = std::make_unique<CadCircle>(m_drawStart, distanceBetween(m_drawStart, scenePos));
            break;
        case DrawingTool::Rectangle:
            cadEntity = std::make_unique<CadRectangle>(bounds);
            break;
        case DrawingTool::Ellipse:
            cadEntity = std::make_unique<CadEllipse>(bounds);
            break;
        case DrawingTool::Polygon:
            cadEntity = std::make_unique<CadPolygon>(m_drawStart, distanceBetween(m_drawStart, scenePos),
                                                     m_polygonSides,
                                                     qRadiansToDegrees(std::atan2(scenePos.y() - m_drawStart.y(), scenePos.x() - m_drawStart.x())));
            break;
        default:
            break;
        }

        addCadEntity(std::move(cadEntity));
    }

    m_drawing = false;
}

void GraphicsView::finishPolyline(bool closed)
{
    clearPreviewItem();
    if (m_drawingTool == DrawingTool::Hatch) {
        if (m_points.size() >= 3) {
            addCadEntity(std::make_unique<CadHatch>(m_points, m_hatchPattern, m_hatchScale, m_hatchAngleDeg));
        }
    } else if (m_points.size() >= 2) {
        QVector<QPointF> pts = m_points;
        if (m_drawingTool == DrawingTool::Spline && pts.size() >= 3) {
            QVector<QPointF> smoothed;
            smoothed.reserve((pts.size() - 1) * 8 + 1);
            for (int i = 0; i < pts.size() - 1; ++i) {
                const QPointF p0 = (i == 0) ? pts.at(i) : pts.at(i - 1);
                const QPointF p1 = pts.at(i);
                const QPointF p2 = pts.at(i + 1);
                const QPointF p3 = (i + 2 < pts.size()) ? pts.at(i + 2) : p2;
                for (int j = 0; j < 8; ++j) {
                    const double t = static_cast<double>(j) / 8.0;
                    const double t2 = t * t;
                    const double t3 = t2 * t;
                    const double x = 0.5 * ((2.0 * p1.x()) + (-p0.x() + p2.x()) * t + (2.0*p0.x() - 5.0*p1.x() + 4.0*p2.x() - p3.x()) * t2 + (-p0.x() + 3.0*p1.x() - 3.0*p2.x() + p3.x()) * t3);
                    const double y = 0.5 * ((2.0 * p1.y()) + (-p0.y() + p2.y()) * t + (2.0*p0.y() - 5.0*p1.y() + 4.0*p2.y() - p3.y()) * t2 + (-p0.y() + 3.0*p1.y() - 3.0*p2.y() + p3.y()) * t3);
                    smoothed.append(QPointF(x, y));
                }
            }
            smoothed.append(pts.last());
            pts = smoothed;
        }
        addCadEntity(std::make_unique<CadPolyline>(pts, closed && pts.size() >= 3));
    }
    m_points.clear();
    m_drawing = false;
}

void GraphicsView::finishClickSequenceIfReady()
{
    if (m_drawingTool == DrawingTool::CircleDiameter && m_points.size() >= 2) {
        const QPointF a = m_points.at(0);
        const QPointF b = m_points.at(1);
        if (distanceBetween(a, b) > 1e-6) {
            const QPointF center((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0);
            addCadEntity(std::make_unique<CadCircle>(center, distanceBetween(a, b) / 2.0));
        }
        clearPreviewItem();
        m_points.clear();
        m_drawing = false;
        return;
    }

    if (m_drawingTool == DrawingTool::Circle3Points && m_points.size() >= 3) {
        QPointF center; double radius = 0.0;
        if (circleThroughThreePoints(m_points.at(0), m_points.at(1), m_points.at(2), center, radius)) {
            addCadEntity(std::make_unique<CadCircle>(center, radius));
        }
        clearPreviewItem();
        m_points.clear();
        m_drawing = false;
        return;
    }

    if (m_drawingTool == DrawingTool::Arc3Points && m_points.size() >= 3) {
        QPointF center; double radius = 0.0; double startAngle = 0.0; double spanAngle = 0.0;
        if (arcFromThreePoints(m_points.at(0), m_points.at(1), m_points.at(2), center, radius, startAngle, spanAngle)) {
            addCadEntity(std::make_unique<CadArc>(center, radius, startAngle, spanAngle));
        }
        clearPreviewItem();
        m_points.clear();
        m_drawing = false;
        return;
    }

    if (m_drawingTool == DrawingTool::LinearDimension && m_points.size() >= 3) {
        addCadEntity(std::make_unique<CadLinearDimension>(m_points.at(0), m_points.at(1), m_points.at(2), m_annotationTextHeight));
        clearPreviewItem();
        clearDimensionPickMarkers();
        m_points.clear();
        m_drawing = false;
        return;
    }

    if (m_drawingTool == DrawingTool::Leader && m_points.size() >= 2) {
        addCadEntity(std::make_unique<CadLeader>(m_points.at(0), m_points.at(1), m_annotationText, m_annotationTextHeight));
        clearPreviewItem();
        m_points.clear();
        m_drawing = false;
        return;
    }
}

std::unique_ptr<CadText> GraphicsView::makeCurrentTextEntity(const QPointF& scenePos) const
{
    auto text = std::make_unique<CadText>(scenePos, m_annotationText, m_annotationTextHeight);
    text->setWidthFactor(m_annotationTextWidthFactor);
    text->setObliqueAngleDeg(m_annotationTextObliqueAngleDeg);
    text->setFontName(m_annotationFontName);
    return text;
}

void GraphicsView::addCadEntity(std::unique_ptr<CadEntity> cadEntity)
{
    if (!cadEntity) return;

    if (m_document) {
        const CadLayer layer = m_document->currentLayer();
        cadEntity->setLayer(layer.name);
        cadEntity->setColor(layer.color);
        cadEntity->setLineWeight(layer.lineWeight);
        cadEntity->setLineType(layer.lineType);
        const QString entityType = QString::fromLatin1(typeid(*cadEntity).name());
        m_document->addEntity(std::move(cadEntity));
        const int newIndex = m_document->entityCount() - 1;
        logDrawEvent(QStringLiteral("Entity added: %1  total=%2").arg(entityType).arg(m_document->entityCount()));

        // Sur un DWG/DXF externe massif, renderToScene() regroupe les entités
        // anciennes dans des chemins rapides. Si on reconstruit toute la scène
        // après chaque trait, la nouvelle entité peut être regroupée aussi et
        // devient impossible à sélectionner/snapper immédiatement. On ajoute
        // donc l'entité dessinée comme item CAD individuel, éditable et selected.
        if (m_largeDocumentMode && scene()) {
            const CadEntity* inserted = m_document->entityAt(newIndex);
            QGraphicsItem* item = inserted ? inserted->createGraphicsItem() : nullptr;
            if (item) {
                clearCadSelection();
                item->setData(1, newIndex);
                item->setData(2, layer.locked);
                item->setData(3, 0);
                if (layer.locked) {
                    item->setFlag(QGraphicsItem::ItemIsSelectable, false);
                    item->setOpacity(0.65);
                } else {
                    item->setSelected(true);
                    m_documentSelectionIndices.clear();
                    m_documentSelectionIndices.append(newIndex);
                }
                scene()->addItem(item);
                scene()->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
                const QRectF itemBounds = item->sceneBoundingRect().adjusted(-20.0, -20.0, 20.0, 20.0);
                if (itemBounds.isValid() && !itemBounds.isNull()) {
                    const QRectF united = scene()->sceneRect().united(itemBounds);
                    scene()->setSceneRect(safeRectForView(united));
                    m_cachedContentBounds = scene()->sceneRect();
                }
                updateLevelOfDetail();
                viewport()->update();
                return;
            }
        }

        refreshFromDocumentPreservingSelection({newIndex});
    } else {
        scene()->addItem(cadEntity->createGraphicsItem());
    }
}

void GraphicsView::clearPreviewItem()
{
    QGraphicsItem* item = m_previewItem;
    m_previewItem = nullptr;

    if (!item) return;

    // The item may already have been deleted by QGraphicsScene::clear() during
    // a document re-render. For valid live items, remove it from the scene that
    // actually owns it, not necessarily from this view's current scene.
    QGraphicsScene* ownerScene = item->scene();
    if (ownerScene) {
        ownerScene->removeItem(item);
    }
    delete item;
}

void GraphicsView::cancelDrawing()
{
    m_drawing = false;
    m_points.clear();
    m_movingSelection = false;
    m_selectionDragMoved = false;
    m_movingSelectionIndices.clear();
    m_keyboardInputBuffer.clear();
    clearDimensionPickMarkers();
    setCursor(m_drawingTool == DrawingTool::Noe ? Qt::ArrowCursor : Qt::CrossCursor);
    clearPreviewItem();
}

namespace {
bool selectedTwoLines(CadDocument* doc, const QVector<int>& indices, CadLine*& a, CadLine*& b, int& ia, int& ib)
{
    a = nullptr;
    b = nullptr;
    ia = -1;
    ib = -1;
    if (!doc || indices.size() != 2) return false;
    ia = indices.at(0);
    ib = indices.at(1);
    a = dynamic_cast<CadLine*>(doc->entityAt(ia));
    b = dynamic_cast<CadLine*>(doc->entityAt(ib));
    return a && b;
}

bool lineIntersectionPoint(const CadLine* a, const CadLine* b, QPointF& intersection)
{
    if (!a || !b) return false;
    const auto hit = CadGeometry::intersectInfiniteLines(a->start(), a->end(), b->start(), b->end());
    if (!hit.hasPoint()) return false;
    intersection = hit.firstPoint();
    return true;
}

QPointF endpointAwayFrom(const CadLine* line, const QPointF& pivot)
{
    return QLineF(pivot, line->start()).length() > QLineF(pivot, line->end()).length()
        ? line->start() : line->end();
}

void replaceNearestEndpoint(CadLine* line, const QPointF& pivot, const QPointF& replacement)
{
    if (!line) return;
    if (QLineF(line->start(), pivot).length() <= QLineF(line->end(), pivot).length())
        line->setStart(replacement);
    else
        line->setEnd(replacement);
}

QPointF pointFromPivotAlongLine(const CadLine* line, const QPointF& pivot, double distance)
{
    if (!line) return pivot;
    return CadGeometry::pointFromPivotAlongSegment(line->start(), line->end(), pivot, distance);
}

constexpr double kExtendTol = 1.0e-7;
constexpr double kExtendAngleTolDeg = 1.0e-6;

struct ExtendRay
{
    QPointF anchor;
    QPointF direction;
    bool replaceStart = false;
};

struct ExtendRayCandidate
{
    QPointF point;
    bool replaceStart = false;
    double score = 0.0;
};

struct ExtendArcCandidate
{
    double angleDeg = 0.0;
    bool replaceStart = false;
    double score = 0.0;
};

double extendDot(const QPointF& a, const QPointF& b)
{
    return a.x() * b.x() + a.y() * b.y();
}

double extendCross(const QPointF& a, const QPointF& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

QPointF extendPointOnLine(const QPointF& a, const QPointF& direction, double t)
{
    return QPointF(a.x() + direction.x() * t, a.y() + direction.y() * t);
}

bool extendEnditePoint(const QPointF& p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}

bool extendValidSegment(const QPointF& a, const QPointF& b)
{
    return extendEnditePoint(a) && extendEnditePoint(b) && distanceBetween(a, b) > kExtendTol;
}

void appendUniqueExtendPoint(QVector<QPointF>& points, const QPointF& p)
{
    if (!extendEnditePoint(p)) return;
    for (const QPointF& existing : points) {
        if (distanceBetween(existing, p) <= kExtendTol) return;
    }
    points.append(p);
}

void appendUniqueRayCandidate(QVector<ExtendRayCandidate>& candidates, const QPointF& p, bool replaceStart, double score)
{
    if (!extendEnditePoint(p) || score <= kExtendTol) return;
    for (const ExtendRayCandidate& existing : candidates) {
        if (existing.replaceStart == replaceStart && distanceBetween(existing.point, p) <= kExtendTol) return;
    }
    candidates.append({p, replaceStart, score});
}

void appendUniqueArcCandidate(QVector<ExtendArcCandidate>& candidates, double angleDeg, bool replaceStart, double score)
{
    if (!std::isfinite(angleDeg) || score <= kExtendAngleTolDeg) return;
    const double normalized = std::fmod(std::fmod(angleDeg, 360.0) + 360.0, 360.0);
    for (const ExtendArcCandidate& existing : candidates) {
        if (existing.replaceStart == replaceStart && std::abs(existing.angleDeg - normalized) <= kExtendAngleTolDeg) return;
    }
    candidates.append({normalized, replaceStart, score});
}

bool extendInfiniteLineIntersection(const QPointF& a, const QPointF& b,
                                    const QPointF& c, const QPointF& d,
                                    QPointF& out)
{
    const QPointF r = b - a;
    const QPointF s = d - c;
    const double den = extendCross(r, s);
    if (std::abs(den) <= kExtendTol) return false;

    const double t = extendCross(c - a, s) / den;
    out = extendPointOnLine(a, r, t);
    return extendEnditePoint(out);
}

bool extendInfiniteLineToSegmentIntersection(const QPointF& a, const QPointF& b,
                                             const QPointF& c, const QPointF& d,
                                             QPointF& out)
{
    const QPointF r = b - a;
    const QPointF s = d - c;
    const double den = extendCross(r, s);
    if (std::abs(den) <= kExtendTol) return false;

    const double t = extendCross(c - a, s) / den;
    const double u = extendCross(c - a, r) / den;
    if (u < -kExtendTol || u > 1.0 + kExtendTol) return false;

    out = extendPointOnLine(a, r, t);
    return extendEnditePoint(out);
}

void addSegmentBoundaryIntersection(const CadLine* source, const QPointF& a, const QPointF& b, QVector<QPointF>& candidates)
{
    if (!source || !extendValidSegment(a, b)) return;
    QPointF p;
    if (extendInfiniteLineToSegmentIntersection(source->start(), source->end(), a, b, p)) {
        appendUniqueExtendPoint(candidates, p);
    }
}

void addPolylineBoundaryIntersections(const CadLine* source, const QVector<QPointF>& points, bool closed, QVector<QPointF>& candidates)
{
    if (!source || points.size() < 2) return;
    for (int i = 0; i + 1 < points.size(); ++i) {
        addSegmentBoundaryIntersection(source, points.at(i), points.at(i + 1), candidates);
    }
    if (closed && points.size() > 2) {
        addSegmentBoundaryIntersection(source, points.last(), points.first(), candidates);
    }
}

void addRectangleBoundaryIntersections(const CadLine* source, const QRectF& rect, QVector<QPointF>& candidates)
{
    const QRectF r = rect.normalized();
    if (!r.isValid() || r.isNull()) return;

    QVector<QPointF> points;
    points << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
    addPolylineBoundaryIntersections(source, points, true, candidates);
}

void addCircleBoundaryIntersections(const CadLine* source, const QPointF& center, double radius, QVector<QPointF>& candidates)
{
    if (!source || radius <= kExtendTol) return;
    const QPointF a = source->start();
    const QPointF d = source->end() - source->start();
    const double aa = extendDot(d, d);
    if (aa <= kExtendTol * kExtendTol) return;

    const QPointF f = a - center;
    const double bb = 2.0 * extendDot(f, d);
    const double cc = extendDot(f, f) - radius * radius;
    const double disc = bb * bb - 4.0 * aa * cc;
    if (disc < -kExtendTol) return;

    if (std::abs(disc) <= kExtendTol) {
        const double t = -bb / (2.0 * aa);
        appendUniqueExtendPoint(candidates, extendPointOnLine(a, d, t));
        return;
    }

    const double root = std::sqrt(std::max(0.0, disc));
    appendUniqueExtendPoint(candidates, extendPointOnLine(a, d, (-bb - root) / (2.0 * aa)));
    appendUniqueExtendPoint(candidates, extendPointOnLine(a, d, (-bb + root) / (2.0 * aa)));
}

double normalizeExtendAngleDeg(double angle)
{
    double value = std::fmod(angle, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

double pointAngleOnCadCircleDeg(const QPointF& center, const QPointF& p)
{
    return normalizeExtendAngleDeg(qRadiansToDegrees(std::atan2(center.y() - p.y(), p.x() - center.x())));
}

QPointF pointOnCadCircle(const QPointF& center, double radius, double angleDeg)
{
    const double a = qDegreesToRadians(angleDeg);
    return QPointF(center.x() + radius * std::cos(a), center.y() - radius * std::sin(a));
}

double orientedAngleDelta(double fromDeg, double toDeg, double sign)
{
    if (sign >= 0.0) return normalizeExtendAngleDeg(toDeg - fromDeg);
    return normalizeExtendAngleDeg(fromDeg - toDeg);
}

bool angleOnCadArc(const QPointF& center, const QPointF& p, double startDeg, double spanDeg)
{
    if (std::abs(spanDeg) >= 360.0 - 1.0e-6) return true;

    const double angle = pointAngleOnCadCircleDeg(center, p);
    const double sign = spanDeg >= 0.0 ? 1.0 : -1.0;
    const double delta = orientedAngleDelta(startDeg, angle, sign);
    return delta <= std::abs(spanDeg) + 1.0e-5;
}

void addArcBoundaryIntersections(const CadLine* source, const CadArc* arc, QVector<QPointF>& candidates)
{
    if (!source || !arc || arc->radius() <= kExtendTol) return;

    QVector<QPointF> circleHits;
    addCircleBoundaryIntersections(source, arc->center(), arc->radius(), circleHits);
    for (const QPointF& p : circleHits) {
        if (angleOnCadArc(arc->center(), p, arc->startAngleDeg(), arc->spanAngleDeg())) {
            appendUniqueExtendPoint(candidates, p);
        }
    }
}

void addEllipseBoundaryIntersections(const CadLine* source, const QRectF& rect, QVector<QPointF>& candidates)
{
    if (!source) return;
    const QRectF r = rect.normalized();
    const double rx = r.width() * 0.5;
    const double ry = r.height() * 0.5;
    if (rx <= kExtendTol || ry <= kExtendTol) return;

    const QPointF center = r.center();
    const QPointF a = source->start();
    const QPointF d = source->end() - source->start();

    const double ox = (a.x() - center.x()) / rx;
    const double oy = (a.y() - center.y()) / ry;
    const double dx = d.x() / rx;
    const double dy = d.y() / ry;

    const double aa = dx * dx + dy * dy;
    if (aa <= kExtendTol * kExtendTol) return;

    const double bb = 2.0 * (ox * dx + oy * dy);
    const double cc = ox * ox + oy * oy - 1.0;
    const double disc = bb * bb - 4.0 * aa * cc;
    if (disc < -kExtendTol) return;

    if (std::abs(disc) <= kExtendTol) {
        appendUniqueExtendPoint(candidates, extendPointOnLine(a, d, -bb / (2.0 * aa)));
        return;
    }

    const double root = std::sqrt(std::max(0.0, disc));
    appendUniqueExtendPoint(candidates, extendPointOnLine(a, d, (-bb - root) / (2.0 * aa)));
    appendUniqueExtendPoint(candidates, extendPointOnLine(a, d, (-bb + root) / (2.0 * aa)));
}

QVector<QPointF> extendPolygonPoints(const CadPolygon* polygon)
{
    QVector<QPointF> points;
    if (!polygon || polygon->radius() <= kExtendTol || polygon->sides() < 3) return points;

    const int n = std::max(3, polygon->sides());
    const double rotationRad = qDegreesToRadians(polygon->rotationDeg());
    for (int i = 0; i < n; ++i) {
        const double a = rotationRad + (2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n));
        points.append(QPointF(polygon->center().x() + polygon->radius() * std::cos(a),
                              polygon->center().y() + polygon->radius() * std::sin(a)));
    }
    return points;
}

QVector<QPointF> approximateEllipsePolyline(const QRectF& rect, int steps = 96)
{
    QVector<QPointF> points;
    const QRectF r = rect.normalized();
    const double rx = r.width() * 0.5;
    const double ry = r.height() * 0.5;
    if (rx <= kExtendTol || ry <= kExtendTol) return points;
    const QPointF center = r.center();
    for (int i = 0; i < steps; ++i) {
        const double a = (2.0 * M_PI * static_cast<double>(i)) / static_cast<double>(steps);
        points.append(QPointF(center.x() + rx * std::cos(a), center.y() + ry * std::sin(a)));
    }
    return points;
}

void collectExtendBoundaryIntersections(const CadLine* source, const CadEntity* boundary, QVector<QPointF>& candidates)
{
    if (!source || !boundary) return;

    if (const auto* line = dynamic_cast<const CadLine*>(boundary)) {
        QPointF p;
        if (extendInfiniteLineIntersection(source->start(), source->end(), line->start(), line->end(), p)) {
            appendUniqueExtendPoint(candidates, p);
        }
        return;
    }

    if (const auto* rect = dynamic_cast<const CadRectangle*>(boundary)) {
        addRectangleBoundaryIntersections(source, rect->rect(), candidates);
        return;
    }

    if (const auto* polyline = dynamic_cast<const CadPolyline*>(boundary)) {
        addPolylineBoundaryIntersections(source, polyline->points(), polyline->closed(), candidates);
        return;
    }

    if (const auto* circle = dynamic_cast<const CadCircle*>(boundary)) {
        addCircleBoundaryIntersections(source, circle->center(), circle->radius(), candidates);
        return;
    }

    if (const auto* arc = dynamic_cast<const CadArc*>(boundary)) {
        addArcBoundaryIntersections(source, arc, candidates);
        return;
    }

    if (const auto* ellipse = dynamic_cast<const CadEllipse*>(boundary)) {
        addEllipseBoundaryIntersections(source, ellipse->rect(), candidates);
        return;
    }

    if (const auto* polygon = dynamic_cast<const CadPolygon*>(boundary)) {
        addPolylineBoundaryIntersections(source, extendPolygonPoints(polygon), true, candidates);
        return;
    }

    if (const auto* hatch = dynamic_cast<const CadHatch*>(boundary)) {
        if (!hatch->loops().isEmpty()) {
            for (const QVector<QPointF>& loop : hatch->loops()) {
                addPolylineBoundaryIntersections(source, loop, true, candidates);
            }
        } else {
            addPolylineBoundaryIntersections(source, hatch->boundary(), true, candidates);
        }
        return;
    }

    if (const auto* dimension = dynamic_cast<const CadLinearDimension*>(boundary)) {
        addSegmentBoundaryIntersection(source, dimension->first(), dimension->second(), candidates);
        return;
    }

    if (const auto* leader = dynamic_cast<const CadLeader*>(boundary)) {
        addSegmentBoundaryIntersection(source, leader->arrowPoint(), leader->textPoint(), candidates);
        return;
    }
}

bool sourceExtendRays(const CadEntity* source, QVector<ExtendRay>& rays)
{
    rays.clear();
    if (!source) return false;

    if (const auto* line = dynamic_cast<const CadLine*>(source)) {
        if (!extendValidSegment(line->start(), line->end())) return false;
        rays.append({line->start(), line->start() - line->end(), true});
        rays.append({line->end(), line->end() - line->start(), false});
        return true;
    }

    if (const auto* polyline = dynamic_cast<const CadPolyline*>(source)) {
        const QVector<QPointF>& pts = polyline->points();
        if (polyline->closed() || pts.size() < 2) return false;
        if (!extendValidSegment(pts.first(), pts.at(1))) return false;
        if (!extendValidSegment(pts.at(pts.size() - 2), pts.last())) return false;
        rays.append({pts.first(), pts.first() - pts.at(1), true});
        rays.append({pts.last(), pts.last() - pts.at(pts.size() - 2), false});
        return true;
    }

    if (const auto* dimension = dynamic_cast<const CadLinearDimension*>(source)) {
        if (!extendValidSegment(dimension->first(), dimension->second())) return false;
        rays.append({dimension->first(), dimension->first() - dimension->second(), true});
        rays.append({dimension->second(), dimension->second() - dimension->first(), false});
        return true;
    }

    if (const auto* leader = dynamic_cast<const CadLeader*>(source)) {
        if (!extendValidSegment(leader->arrowPoint(), leader->textPoint())) return false;
        rays.append({leader->arrowPoint(), leader->arrowPoint() - leader->textPoint(), true});
        rays.append({leader->textPoint(), leader->textPoint() - leader->arrowPoint(), false});
        return true;
    }

    return false;
}

bool isExtendableSourceEntityForExtend(const CadEntity* entity)
{
    if (!entity) return false;
    if (const auto* arc = dynamic_cast<const CadArc*>(entity)) {
        return arc->radius() > kExtendTol
            && std::abs(arc->spanAngleDeg()) > kExtendAngleTolDeg
            && std::abs(arc->spanAngleDeg()) < 360.0 - kExtendAngleTolDeg;
    }
    QVector<ExtendRay> rays;
    return sourceExtendRays(entity, rays);
}

void collectRayBoundaryCandidates(const ExtendRay& ray, const CadEntity* boundary, QVector<ExtendRayCandidate>& candidates)
{
    if (!boundary || extendDot(ray.direction, ray.direction) <= kExtendTol * kExtendTol) return;

    CadLine virtualSource(ray.anchor, ray.anchor + ray.direction);
    QVector<QPointF> hits;
    collectExtendBoundaryIntersections(&virtualSource, boundary, hits);

    const double len2 = extendDot(ray.direction, ray.direction);
    for (const QPointF& p : hits) {
        const double t = extendDot(p - ray.anchor, ray.direction) / len2;
        if (t <= kExtendTol) continue;
        appendUniqueRayCandidate(candidates, p, ray.replaceStart, distanceBetween(ray.anchor, p));
    }
}

bool bestRayReplacement(const QVector<ExtendRayCandidate>& candidates, QPointF& replacement, bool& replaceStart)
{
    if (candidates.isEmpty()) return false;
    bool found = false;
    double bestScore = std::numeric_limits<double>::max();
    for (const ExtendRayCandidate& c : candidates) {
        if (c.score <= kExtendTol) continue;
        if (!found || c.score < bestScore) {
            found = true;
            bestScore = c.score;
            replacement = c.point;
            replaceStart = c.replaceStart;
        }
    }
    return found;
}

void addCircleInfiniteLineHits(const QPointF& center, double radius, const QPointF& a, const QPointF& b, QVector<QPointF>& hits)
{
    if (radius <= kExtendTol || !extendValidSegment(a, b)) return;
    const QPointF d = b - a;
    const double aa = extendDot(d, d);
    if (aa <= kExtendTol * kExtendTol) return;

    const QPointF f = a - center;
    const double bb = 2.0 * extendDot(f, d);
    const double cc = extendDot(f, f) - radius * radius;
    const double disc = bb * bb - 4.0 * aa * cc;
    if (disc < -kExtendTol) return;

    if (std::abs(disc) <= kExtendTol) {
        appendUniqueExtendPoint(hits, extendPointOnLine(a, d, -bb / (2.0 * aa)));
        return;
    }

    const double root = std::sqrt(std::max(0.0, disc));
    appendUniqueExtendPoint(hits, extendPointOnLine(a, d, (-bb - root) / (2.0 * aa)));
    appendUniqueExtendPoint(hits, extendPointOnLine(a, d, (-bb + root) / (2.0 * aa)));
}

void addCircleSegmentHits(const QPointF& center, double radius, const QPointF& a, const QPointF& b, QVector<QPointF>& hits)
{
    QVector<QPointF> infiniteHits;
    addCircleInfiniteLineHits(center, radius, a, b, infiniteHits);
    const QPointF d = b - a;
    const double len2 = extendDot(d, d);
    if (len2 <= kExtendTol * kExtendTol) return;
    for (const QPointF& p : infiniteHits) {
        const double t = extendDot(p - a, d) / len2;
        if (t >= -kExtendTol && t <= 1.0 + kExtendTol) appendUniqueExtendPoint(hits, p);
    }
}

void addCircleCircleHits(const QPointF& c0, double r0, const QPointF& c1, double r1, QVector<QPointF>& hits)
{
    if (r0 <= kExtendTol || r1 <= kExtendTol) return;
    const double dx = c1.x() - c0.x();
    const double dy = c1.y() - c0.y();
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d <= kExtendTol) return;
    if (d > r0 + r1 + kExtendTol) return;
    if (d < std::abs(r0 - r1) - kExtendTol) return;

    const double a = (r0 * r0 - r1 * r1 + d * d) / (2.0 * d);
    const double h2 = r0 * r0 - a * a;
    if (h2 < -kExtendTol) return;

    const QPointF base(c0.x() + a * dx / d, c0.y() + a * dy / d);
    if (std::abs(h2) <= kExtendTol) {
        appendUniqueExtendPoint(hits, base);
        return;
    }

    const double h = std::sqrt(std::max(0.0, h2));
    const double rx = -dy * (h / d);
    const double ry = dx * (h / d);
    appendUniqueExtendPoint(hits, QPointF(base.x() + rx, base.y() + ry));
    appendUniqueExtendPoint(hits, QPointF(base.x() - rx, base.y() - ry));
}

void addCirclePolylineHits(const QPointF& center, double radius, const QVector<QPointF>& points, bool closed, QVector<QPointF>& hits)
{
    if (points.size() < 2) return;
    for (int i = 0; i + 1 < points.size(); ++i) {
        addCircleSegmentHits(center, radius, points.at(i), points.at(i + 1), hits);
    }
    if (closed && points.size() > 2) {
        addCircleSegmentHits(center, radius, points.last(), points.first(), hits);
    }
}

void collectArcBoundaryPoints(const CadArc* source, const CadEntity* boundary, QVector<QPointF>& hits)
{
    if (!source || !boundary || source->radius() <= kExtendTol) return;
    const QPointF center = source->center();
    const double radius = source->radius();

    if (const auto* line = dynamic_cast<const CadLine*>(boundary)) {
        addCircleInfiniteLineHits(center, radius, line->start(), line->end(), hits);
        return;
    }

    if (const auto* rect = dynamic_cast<const CadRectangle*>(boundary)) {
        const QRectF r = rect->rect().normalized();
        QVector<QPointF> pts;
        pts << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
        addCirclePolylineHits(center, radius, pts, true, hits);
        return;
    }

    if (const auto* polyline = dynamic_cast<const CadPolyline*>(boundary)) {
        addCirclePolylineHits(center, radius, polyline->points(), polyline->closed(), hits);
        return;
    }

    if (const auto* circle = dynamic_cast<const CadCircle*>(boundary)) {
        addCircleCircleHits(center, radius, circle->center(), circle->radius(), hits);
        return;
    }

    if (const auto* arc = dynamic_cast<const CadArc*>(boundary)) {
        QVector<QPointF> circleHits;
        addCircleCircleHits(center, radius, arc->center(), arc->radius(), circleHits);
        for (const QPointF& p : circleHits) {
            if (angleOnCadArc(arc->center(), p, arc->startAngleDeg(), arc->spanAngleDeg())) appendUniqueExtendPoint(hits, p);
        }
        return;
    }

    if (const auto* ellipse = dynamic_cast<const CadEllipse*>(boundary)) {
        addCirclePolylineHits(center, radius, approximateEllipsePolyline(ellipse->rect()), true, hits);
        return;
    }

    if (const auto* polygon = dynamic_cast<const CadPolygon*>(boundary)) {
        addCirclePolylineHits(center, radius, extendPolygonPoints(polygon), true, hits);
        return;
    }

    if (const auto* hatch = dynamic_cast<const CadHatch*>(boundary)) {
        if (!hatch->loops().isEmpty()) {
            for (const QVector<QPointF>& loop : hatch->loops()) addCirclePolylineHits(center, radius, loop, true, hits);
        } else {
            addCirclePolylineHits(center, radius, hatch->boundary(), true, hits);
        }
        return;
    }

    if (const auto* dimension = dynamic_cast<const CadLinearDimension*>(boundary)) {
        addCircleSegmentHits(center, radius, dimension->first(), dimension->second(), hits);
        return;
    }

    if (const auto* leader = dynamic_cast<const CadLeader*>(boundary)) {
        addCircleSegmentHits(center, radius, leader->arrowPoint(), leader->textPoint(), hits);
        return;
    }
}

void collectArcCandidates(const CadArc* arc, const CadEntity* boundary, QVector<ExtendArcCandidate>& candidates)
{
    if (!arc || !boundary || arc->radius() <= kExtendTol) return;
    const double span = arc->spanAngleDeg();
    const double absSpan = std::abs(span);
    if (absSpan <= kExtendAngleTolDeg || absSpan >= 360.0 - kExtendAngleTolDeg) return;

    QVector<QPointF> hits;
    collectArcBoundaryPoints(arc, boundary, hits);

    const double sign = span >= 0.0 ? 1.0 : -1.0;
    const double start = normalizeExtendAngleDeg(arc->startAngleDeg());
    const double end = normalizeExtendAngleDeg(start + span);

    for (const QPointF& p : hits) {
        const double angle = pointAngleOnCadCircleDeg(arc->center(), p);
        const double onCurrentDelta = orientedAngleDelta(start, angle, sign);
        if (onCurrentDelta <= absSpan + kExtendAngleTolDeg) continue;

        const double beforeStart = orientedAngleDelta(angle, start, sign);
        const double afterEnd = orientedAngleDelta(end, angle, sign);
        if (absSpan + beforeStart < 360.0 - kExtendAngleTolDeg) {
            appendUniqueArcCandidate(candidates, angle, true, beforeStart * arc->radius());
        }
        if (absSpan + afterEnd < 360.0 - kExtendAngleTolDeg) {
            appendUniqueArcCandidate(candidates, angle, false, afterEnd * arc->radius());
        }
    }
}

bool bestArcReplacement(const CadArc* arc, const QVector<ExtendArcCandidate>& candidates,
                        double& newStartAngleDeg, double& newSpanAngleDeg)
{
    if (!arc || candidates.isEmpty()) return false;
    bool found = false;
    ExtendArcCandidate best;
    for (const ExtendArcCandidate& c : candidates) {
        if (c.score <= kExtendAngleTolDeg) continue;
        if (!found || c.score < best.score) {
            found = true;
            best = c;
        }
    }
    if (!found) return false;

    const double span = arc->spanAngleDeg();
    const double sign = span >= 0.0 ? 1.0 : -1.0;
    const double start = normalizeExtendAngleDeg(arc->startAngleDeg());
    const double end = normalizeExtendAngleDeg(start + span);

    if (best.replaceStart) {
        const double beforeStart = orientedAngleDelta(best.angleDeg, start, sign);
        newStartAngleDeg = best.angleDeg;
        newSpanAngleDeg = span + sign * beforeStart;
    } else {
        const double afterEnd = orientedAngleDelta(end, best.angleDeg, sign);
        newStartAngleDeg = arc->startAngleDeg();
        newSpanAngleDeg = span + sign * afterEnd;
    }

    return std::abs(newSpanAngleDeg) > std::abs(span) + kExtendAngleTolDeg
        && std::abs(newSpanAngleDeg) < 360.0 - kExtendAngleTolDeg;
}

bool applyRayReplacement(CadEntity* source, const QPointF& replacement, bool replaceStart)
{
    if (!source || !extendEnditePoint(replacement)) return false;

    if (auto* line = dynamic_cast<CadLine*>(source)) {
        if (replaceStart) line->setStart(replacement);
        else line->setEnd(replacement);
        return true;
    }

    if (auto* polyline = dynamic_cast<CadPolyline*>(source)) {
        if (polyline->closed()) return false;
        QVector<QPointF> pts = polyline->points();
        if (pts.size() < 2) return false;
        if (replaceStart) pts[0] = replacement;
        else pts[pts.size() - 1] = replacement;
        polyline->setPoints(pts);
        return true;
    }

    if (auto* dimension = dynamic_cast<CadLinearDimension*>(source)) {
        if (replaceStart) dimension->setFirst(replacement);
        else dimension->setSecond(replacement);
        return true;
    }

    if (auto* leader = dynamic_cast<CadLeader*>(source)) {
        if (replaceStart) leader->setArrowPoint(replacement);
        else leader->setTextPoint(replacement);
        return true;
    }

    return false;
}
}
void GraphicsView::offsetSelectedEntities(double distance)
{
    if (!m_document || std::abs(distance) <= 1e-12) return;
    const QVector<int> indices = selectedEntityIndices();
    if (indices.isEmpty()) return;

    QVector<int> newSelection;
    for (int index : indices) {
        const CadEntity* entity = m_document->entityAt(index);
        if (!entity) continue;

        std::unique_ptr<CadEntity> copy;

        if (const auto* line = dynamic_cast<const CadLine*>(entity)) {
            const QVector<QPointF> points = CadGeometry::offsetSegment(line->start(), line->end(), distance);
            if (points.size() == 2) copy = std::make_unique<CadLine>(points.at(0), points.at(1));
        } else if (const auto* polyline = dynamic_cast<const CadPolyline*>(entity)) {
            const QVector<QPointF> points = CadGeometry::offsetPolyline(polyline->points(), polyline->closed(), distance);
            if (points.size() >= 2) copy = std::make_unique<CadPolyline>(points, polyline->closed());
        } else if (const auto* circle = dynamic_cast<const CadCircle*>(entity)) {
            const double radius = circle->radius() + distance;
            if (radius > 1.0e-9) copy = std::make_unique<CadCircle>(circle->center(), radius);
        } else if (const auto* arc = dynamic_cast<const CadArc*>(entity)) {
            const double radius = arc->radius() + distance;
            if (radius > 1.0e-9) copy = std::make_unique<CadArc>(arc->center(), radius, arc->startAngleDeg(), arc->spanAngleDeg());
        } else if (const auto* rect = dynamic_cast<const CadRectangle*>(entity)) {
            QRectF r = rect->rect().normalized().adjusted(-distance, -distance, distance, distance);
            if (r.width() > 1.0e-9 && r.height() > 1.0e-9) copy = std::make_unique<CadRectangle>(r.normalized());
        } else if (const auto* ellipse = dynamic_cast<const CadEllipse*>(entity)) {
            QRectF r = ellipse->rect().normalized().adjusted(-distance, -distance, distance, distance);
            if (r.width() > 1.0e-9 && r.height() > 1.0e-9) copy = std::make_unique<CadEllipse>(r.normalized());
        } else if (const auto* polygon = dynamic_cast<const CadPolygon*>(entity)) {
            const QVector<QPointF> base = CadGeometry::regularPolygonPoints(polygon->center(), polygon->radius(), polygon->sides(), polygon->rotationDeg());
            const QVector<QPointF> points = CadGeometry::offsetPolyline(base, true, distance);
            if (points.size() >= 3) copy = std::make_unique<CadPolyline>(points, true);
        } else if (const auto* hatch = dynamic_cast<const CadHatch*>(entity)) {
            QVector<QVector<QPointF>> loops;
            if (!hatch->loops().isEmpty()) {
                for (const QVector<QPointF>& loop : hatch->loops()) {
                    const QVector<QPointF> offsetLoop = CadGeometry::offsetPolyline(loop, true, distance);
                    if (offsetLoop.size() >= 3) loops.append(offsetLoop);
                }
            } else {
                const QVector<QPointF> loop = CadGeometry::offsetPolyline(hatch->boundary(), true, distance);
                if (loop.size() >= 3) loops.append(loop);
            }
            if (!loops.isEmpty()) copy = std::make_unique<CadHatch>(loops, hatch->pattern(), hatch->hatchScale(), hatch->angleDeg());
        }

        if (!copy) continue;
        entity->copyStyleTo(*copy);
        m_document->addEntity(std::move(copy));
        newSelection.append(m_document->entityCount() - 1);
    }

    if (!newSelection.isEmpty()) refreshFromDocumentPreservingSelection(newSelection);
}

bool GraphicsView::trimSelectedLinesToIntersection()
{
    if (!m_document) return false;
    CadLine* a = nullptr;
    CadLine* b = nullptr;
    int ia = -1;
    int ib = -1;
    const QVector<int> indices = selectedEntityIndices();
    if (!selectedTwoLines(m_document, indices, a, b, ia, ib)) return false;
    QPointF ip;
    if (!lineIntersectionPoint(a, b, ip)) return false;
    replaceNearestEndpoint(a, ip, ip);
    replaceNearestEndpoint(b, ip, ip);
    refreshFromDocumentPreservingSelection(indices);
    return true;
}

bool GraphicsView::extendSelectedLinesToIntersection()
{
    if (!m_document) return false;

    QVector<int> sourceIndices = m_pendingExtendSourceSelection;
    QVector<int> boundaryIndices = m_pendingExtendBoundarySelection;

    // Fallback de compatibilité: si l'ancien appel direct est utilisé avec deux objets selecteds,
    // le premier index devient la source ouverte, les suivants deviennent les limites.
    if (sourceIndices.isEmpty() || boundaryIndices.isEmpty()) {
        const QVector<int> selected = selectedEntityIndices();
        if (selected.size() >= 2) {
            sourceIndices = { selected.first() };
            boundaryIndices.clear();
            for (int i = 1; i < selected.size(); ++i) boundaryIndices.append(selected.at(i));
        }
    }

    auto clearPendingExtend = [this]() {
        m_pendingExtendSourceSelection.clear();
        m_pendingExtendBoundarySelection.clear();
    };

    if (sourceIndices.size() != 1 || boundaryIndices.isEmpty()) {
        clearPendingExtend();
        return false;
    }

    const int sourceIndex = sourceIndices.first();
    CadEntity* source = m_document->entityAt(sourceIndex);
    if (!isExtendableSourceEntityForExtend(source)) {
        clearPendingExtend();
        return false;
    }

    bool changed = false;

    if (auto* arc = dynamic_cast<CadArc*>(source)) {
        QVector<ExtendArcCandidate> candidates;
        for (int boundaryIndex : boundaryIndices) {
            if (boundaryIndex == sourceIndex) continue;
            const CadEntity* boundary = m_document->entityAt(boundaryIndex);
            collectArcCandidates(arc, boundary, candidates);
        }

        double newStartAngleDeg = arc->startAngleDeg();
        double newSpanAngleDeg = arc->spanAngleDeg();
        if (bestArcReplacement(arc, candidates, newStartAngleDeg, newSpanAngleDeg)) {
            arc->setStartAngleDeg(newStartAngleDeg);
            arc->setSpanAngleDeg(newSpanAngleDeg);
            changed = true;
        }
    } else {
        QVector<ExtendRay> rays;
        if (!sourceExtendRays(source, rays)) {
            clearPendingExtend();
            return false;
        }

        QVector<ExtendRayCandidate> candidates;
        for (int boundaryIndex : boundaryIndices) {
            if (boundaryIndex == sourceIndex) continue;
            const CadEntity* boundary = m_document->entityAt(boundaryIndex);
            for (const ExtendRay& ray : rays) {
                collectRayBoundaryCandidates(ray, boundary, candidates);
            }
        }

        QPointF replacement;
        bool replaceStart = false;
        if (bestRayReplacement(candidates, replacement, replaceStart)) {
            changed = applyRayReplacement(source, replacement, replaceStart);
        }
    }

    if (!changed) {
        clearPendingExtend();
        return false;
    }

    QVector<int> selection = sourceIndices;
    for (int index : boundaryIndices) if (!selection.contains(index)) selection.append(index);
    clearPendingExtend();
    refreshFromDocumentPreservingSelection(selection);
    return true;
}

bool GraphicsView::chamferSelectedLines(double distance)
{
    if (!m_document || distance <= 1e-12) return false;
    CadLine* a = nullptr;
    CadLine* b = nullptr;
    int ia = -1;
    int ib = -1;
    const QVector<int> indices = selectedEntityIndices();
    if (!selectedTwoLines(m_document, indices, a, b, ia, ib)) return false;
    QPointF ip;
    if (!lineIntersectionPoint(a, b, ip)) return false;

    const QPointF pa = pointFromPivotAlongLine(a, ip, distance);
    const QPointF pb = pointFromPivotAlongLine(b, ip, distance);
    replaceNearestEndpoint(a, ip, pa);
    replaceNearestEndpoint(b, ip, pb);

    auto chamfer = std::make_unique<CadLine>(pa, pb);
    chamfer->setLayer(m_document->currentLayerName());
    chamfer->setColor(a->color());
    chamfer->setLineWeight(a->lineWeight());
    chamfer->setLineType(a->lineType());
    m_document->addEntity(std::move(chamfer));

    QVector<int> newSelection = indices;
    newSelection.append(m_document->entityCount() - 1);
    refreshFromDocumentPreservingSelection(newSelection);
    return true;
}

bool GraphicsView::filletSelectedLines(double radius)
{
    if (!m_document || radius <= 1e-12) return false;
    CadLine* a = nullptr;
    CadLine* b = nullptr;
    int ia = -1;
    int ib = -1;
    const QVector<int> indices = selectedEntityIndices();
    if (!selectedTwoLines(m_document, indices, a, b, ia, ib)) return false;
    QPointF ip;
    if (!lineIntersectionPoint(a, b, ip)) return false;

    const CadGeometry::FilletResult filletGeometry = CadGeometry::lineLineFillet(
        a->start(), a->end(), b->start(), b->end(), ip, radius);
    if (!filletGeometry.valid) return false;

    replaceNearestEndpoint(a, ip, filletGeometry.tangentA);
    replaceNearestEndpoint(b, ip, filletGeometry.tangentB);

    auto fillet = std::make_unique<CadArc>(filletGeometry.center,
                                           filletGeometry.radius,
                                           filletGeometry.startAngleDeg,
                                           filletGeometry.spanAngleDeg);
    a->copyStyleTo(*fillet);
    m_document->addEntity(std::move(fillet));

    QVector<int> newSelection = indices;
    newSelection.append(m_document->entityCount() - 1);
    refreshFromDocumentPreservingSelection(newSelection);
    return true;
}


bool GraphicsView::closeOrOpenSelectedPolylines(bool closed)
{
    if (!m_document) return false;
    const QVector<int> indices = selectedEntityIndices();
    bool changed = false;
    for (int index : indices) {
        if (auto* pl = dynamic_cast<CadPolyline*>(m_document->entityAt(index))) {
            pl->setClosed(closed);
            changed = true;
        }
    }
    if (changed) refreshFromDocumentPreservingSelection(indices);
    return changed;
}

bool GraphicsView::smoothSelectedPolylines()
{
    if (!m_document) return false;
    const QVector<int> indices = selectedEntityIndices();
    bool changed = false;
    for (int index : indices) {
        auto* pl = dynamic_cast<CadPolyline*>(m_document->entityAt(index));
        if (!pl || pl->points().size() < 3) continue;
        const QVector<QPointF> src = pl->points();
        QVector<QPointF> out;
        out.reserve((src.size() - 1) * 8 + 1);
        for (int i = 0; i < src.size() - 1; ++i) {
            const QPointF p0 = (i == 0) ? src.at(i) : src.at(i - 1);
            const QPointF p1 = src.at(i);
            const QPointF p2 = src.at(i + 1);
            const QPointF p3 = (i + 2 < src.size()) ? src.at(i + 2) : p2;
            for (int j = 0; j < 8; ++j) {
                const double t = static_cast<double>(j) / 8.0;
                const double t2 = t * t;
                const double t3 = t2 * t;
                const double x = 0.5 * ((2.0*p1.x()) + (-p0.x()+p2.x())*t + (2.0*p0.x()-5.0*p1.x()+4.0*p2.x()-p3.x())*t2 + (-p0.x()+3.0*p1.x()-3.0*p2.x()+p3.x())*t3);
                const double y = 0.5 * ((2.0*p1.y()) + (-p0.y()+p2.y())*t + (2.0*p0.y()-5.0*p1.y()+4.0*p2.y()-p3.y())*t2 + (-p0.y()+3.0*p1.y()-3.0*p2.y()+p3.y())*t3);
                out.append(QPointF(x, y));
            }
        }
        out.append(src.last());
        pl->setPoints(out);
        changed = true;
    }
    if (changed) refreshFromDocumentPreservingSelection(indices);
    return changed;
}

bool GraphicsView::editSelectedText(const QString& text, double height, double rotationDeg)
{
    if (!m_document) return false;
    const QVector<int> indices = selectedEntityIndices();
    bool changed = false;
    for (int index : indices) {
        if (auto* t = dynamic_cast<CadText*>(m_document->entityAt(index))) {
            t->setText(text);
            t->setHeight(height);
            t->setRotationDeg(rotationDeg);
            changed = true;
        }
    }
    if (changed) refreshFromDocumentPreservingSelection(indices);
    return changed;
}


bool GraphicsView::createHatchFromSelectedClosedBoundaries()
{
    if (!m_document) return false;

    const QVector<int> indices = operationSelectionIndices();
    if (indices.isEmpty()) return false;

    auto rectLoop = [](const QRectF& rect) {
        const QRectF r = rect.normalized();
        QVector<QPointF> pts;
        pts << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
        return pts;
    };

    auto circleLoop = [](const QPointF& center, double radius, int segments = 96) {
        QVector<QPointF> pts;
        if (radius <= 1.0e-9) return pts;
        pts.reserve(segments);
        for (int i = 0; i < segments; ++i) {
            const double a = (2.0 * M_PI * i) / double(segments);
            pts.append(QPointF(center.x() + radius * std::cos(a),
                               center.y() + radius * std::sin(a)));
        }
        return pts;
    };

    auto ellipseLoop = [](const QRectF& rect, int segments = 96) {
        QVector<QPointF> pts;
        const QRectF r = rect.normalized();
        const QPointF c = r.center();
        const double rx = r.width() / 2.0;
        const double ry = r.height() / 2.0;
        if (rx <= 1.0e-9 || ry <= 1.0e-9) return pts;
        pts.reserve(segments);
        for (int i = 0; i < segments; ++i) {
            const double a = (2.0 * M_PI * i) / double(segments);
            pts.append(QPointF(c.x() + rx * std::cos(a),
                               c.y() + ry * std::sin(a)));
        }
        return pts;
    };

    auto polygonLoop = [](const CadPolygon& polygon) {
        QVector<QPointF> pts;
        const int sides = qMax(3, polygon.sides());
        pts.reserve(sides);
        const double start = qDegreesToRadians(polygon.rotationDeg());
        for (int i = 0; i < sides; ++i) {
            const double a = start + (2.0 * M_PI * i) / double(sides);
            pts.append(QPointF(polygon.center().x() + polygon.radius() * std::cos(a),
                               polygon.center().y() + polygon.radius() * std::sin(a)));
        }
        return pts;
    };

    struct HatchSegment {
        QPointF a;
        QPointF b;
    };

    QVector<QVector<QPointF>> loops;
    QVector<HatchSegment> lineSegments;

    auto samePoint = [](const QPointF& a, const QPointF& b, double tol) {
        const double dx = a.x() - b.x();
        const double dy = a.y() - b.y();
        return (dx * dx + dy * dy) <= (tol * tol);
    };

    QRectF selectedBounds;
    for (int index : indices) {
        const CadEntity* e = m_document->entityAt(index);
        if (!e) continue;
        if (const auto* l = dynamic_cast<const CadLine*>(e)) {
            selectedBounds = selectedBounds.isNull() ? QRectF(l->start(), l->end()).normalized()
                                                    : selectedBounds.united(QRectF(l->start(), l->end()).normalized());
        } else if (const auto* r = dynamic_cast<const CadRectangle*>(e)) {
            selectedBounds = selectedBounds.isNull() ? r->rect().normalized() : selectedBounds.united(r->rect().normalized());
        } else if (const auto* c = dynamic_cast<const CadCircle*>(e)) {
            const double rr = c->radius();
            const QRectF cr(c->center().x() - rr, c->center().y() - rr, 2.0 * rr, 2.0 * rr);
            selectedBounds = selectedBounds.isNull() ? cr : selectedBounds.united(cr);
        } else if (const auto* el = dynamic_cast<const CadEllipse*>(e)) {
            selectedBounds = selectedBounds.isNull() ? el->rect().normalized() : selectedBounds.united(el->rect().normalized());
        } else if (const auto* pl = dynamic_cast<const CadPolyline*>(e)) {
            for (const QPointF& pt : pl->points()) {
                const QRectF pr(pt.x(), pt.y(), 0.0, 0.0);
                selectedBounds = selectedBounds.isNull() ? pr : selectedBounds.united(pr);
            }
        }
    }
    const double span = std::max(selectedBounds.width(), selectedBounds.height());
    const double closeTol = std::max(1.0e-4, span * 1.0e-6);

    for (int index : indices) {
        const CadEntity* entity = m_document->entityAt(index);
        if (!entity) continue;

        if (const auto* line = dynamic_cast<const CadLine*>(entity)) {
            if (!samePoint(line->start(), line->end(), closeTol)) {
                lineSegments.append({line->start(), line->end()});
            }
        } else if (const auto* r = dynamic_cast<const CadRectangle*>(entity)) {
            loops.append(rectLoop(r->rect()));
        } else if (const auto* c = dynamic_cast<const CadCircle*>(entity)) {
            loops.append(circleLoop(c->center(), c->radius()));
        } else if (const auto* e = dynamic_cast<const CadEllipse*>(entity)) {
            loops.append(ellipseLoop(e->rect()));
        } else if (const auto* p = dynamic_cast<const CadPolygon*>(entity)) {
            loops.append(polygonLoop(*p));
        } else if (const auto* pl = dynamic_cast<const CadPolyline*>(entity)) {
            const QVector<QPointF>& pts = pl->points();
            if (pts.size() >= 3 && (pl->closed() || samePoint(pts.first(), pts.last(), closeTol))) {
                QVector<QPointF> loop = pts;
                if (loop.size() > 1 && samePoint(loop.first(), loop.last(), closeTol)) loop.removeLast();
                loops.append(loop);
            } else if (pts.size() >= 2) {
                for (int i = 1; i < pts.size(); ++i) {
                    if (!samePoint(pts.at(i - 1), pts.at(i), closeTol)) lineSegments.append({pts.at(i - 1), pts.at(i)});
                }
            }
        } else if (const auto* h = dynamic_cast<const CadHatch*>(entity)) {
            if (!h->loops().isEmpty()) {
                for (const QVector<QPointF>& loop : h->loops()) {
                    if (loop.size() >= 3) loops.append(loop);
                }
            } else if (h->boundary().size() >= 3) {
                loops.append(h->boundary());
            }
        }
    }

    // If the user selected independent LINE entities, try to assemble them into
    // one or more closed contours. This is the normal AutoCAD HATCH > Select objects
    // use case for four separate lines forming a rectangle.
    QVector<bool> used(lineSegments.size(), false);
    for (int seed = 0; seed < lineSegments.size(); ++seed) {
        if (used.at(seed)) continue;
        QVector<QPointF> chain;
        chain.append(lineSegments.at(seed).a);
        chain.append(lineSegments.at(seed).b);
        used[seed] = true;

        bool extended = true;
        while (extended) {
            extended = false;
            for (int i = 0; i < lineSegments.size(); ++i) {
                if (used.at(i)) continue;
                const QPointF first = chain.first();
                const QPointF last = chain.last();
                const HatchSegment& s = lineSegments.at(i);
                if (samePoint(last, s.a, closeTol)) {
                    chain.append(s.b);
                    used[i] = true;
                    extended = true;
                    break;
                }
                if (samePoint(last, s.b, closeTol)) {
                    chain.append(s.a);
                    used[i] = true;
                    extended = true;
                    break;
                }
                if (samePoint(first, s.b, closeTol)) {
                    chain.prepend(s.a);
                    used[i] = true;
                    extended = true;
                    break;
                }
                if (samePoint(first, s.a, closeTol)) {
                    chain.prepend(s.b);
                    used[i] = true;
                    extended = true;
                    break;
                }
            }
        }

        if (chain.size() >= 4 && samePoint(chain.first(), chain.last(), closeTol)) {
            chain.removeLast();
            if (chain.size() >= 3) loops.append(chain);
        }
    }

    QVector<QVector<QPointF>> validLoops;
    for (const QVector<QPointF>& loop : loops) {
        if (loop.size() >= 3) validLoops.append(loop);
    }
    if (validLoops.isEmpty()) return false;

    addCadEntity(std::make_unique<CadHatch>(validLoops, m_hatchPattern, m_hatchScale, m_hatchAngleDeg));
    return true;
}

bool GraphicsView::editSelectedHatches(const QString& pattern, double scale, double angleDeg)
{
    if (!m_document) return false;
    const QVector<int> indices = selectedEntityIndices();
    bool changed = false;
    for (int index : indices) {
        if (auto* h = dynamic_cast<CadHatch*>(m_document->entityAt(index))) {
            h->setPattern(pattern);
            h->setHatchScale(scale);
            h->setAngleDeg(angleDeg);
            changed = true;
        }
    }
    if (changed) refreshFromDocumentPreservingSelection(indices);
    return changed;
}

bool GraphicsView::matchPropertiesFromFirstSelection()
{
    if (!m_document) return false;

    QVector<int> sourceIndices = m_pendingMatchPropSourceSelection;
    QVector<int> targetIndices = m_pendingMatchPropTargetSelection;

    // Compatibilité avec l'ancien appel direct: si aucune commande interactive
    // n'a préparé source/cibles, on garde l'ancien comportement basé sur la sélection.
    if (sourceIndices.isEmpty() || targetIndices.isEmpty()) {
        const QVector<int> indices = selectedEntityIndices();
        if (indices.size() < 2) return false;
        sourceIndices = { indices.first() };
        targetIndices.clear();
        for (int i = 1; i < indices.size(); ++i) targetIndices.append(indices.at(i));
    }

    auto clearPendingMatchProp = [this]() {
        m_pendingMatchPropSourceSelection.clear();
        m_pendingMatchPropTargetSelection.clear();
    };

    if (sourceIndices.size() != 1 || targetIndices.isEmpty()) {
        clearPendingMatchProp();
        return false;
    }

    const CadEntity* source = m_document->entityAt(sourceIndices.first());
    if (!source) {
        clearPendingMatchProp();
        return false;
    }

    QVector<int> finalSelection = sourceIndices;
    bool changed = false;
    for (int targetIndex : targetIndices) {
        if (targetIndex == sourceIndices.first()) continue;
        if (CadEntity* target = m_document->entityAt(targetIndex)) {
            target->setLayer(source->layer());
            target->setColor(source->color());
            target->setLineWeight(source->lineWeight());
            target->setLineType(source->lineType());
            if (!finalSelection.contains(targetIndex)) finalSelection.append(targetIndex);
            changed = true;
        }
    }

    clearPendingMatchProp();
    if (changed) refreshFromDocumentPreservingSelection(finalSelection);
    return changed;
}

bool GraphicsView::addCenterMarksToSelectedCircles(double size)
{
    if (!m_document || size <= 1e-9) return false;
    const QVector<int> indices = operationSelectionIndices();
    clearValidatedModifySelection();
    clearDimensionPickMarkers();
    QVector<int> newSelection = indices;
    bool changed = false;
    for (int index : indices) {
        const auto* c = dynamic_cast<const CadCircle*>(m_document->entityAt(index));
        if (!c) continue;
        const QPointF cc = c->center();
        auto h = std::make_unique<CadLine>(QPointF(cc.x() - size, cc.y()), QPointF(cc.x() + size, cc.y()));
        auto v = std::make_unique<CadLine>(QPointF(cc.x(), cc.y() - size), QPointF(cc.x(), cc.y() + size));
        h->setLayer(c->layer()); h->setColor(c->color()); h->setLineWeight(c->lineWeight()); h->setLineType(c->lineType());
        v->setLayer(c->layer()); v->setColor(c->color()); v->setLineWeight(c->lineWeight()); v->setLineType(c->lineType());
        m_document->addEntity(std::move(h)); newSelection.append(m_document->entityCount() - 1);
        m_document->addEntity(std::move(v)); newSelection.append(m_document->entityCount() - 1);
        changed = true;
    }
    if (changed) refreshFromDocumentPreservingSelection(newSelection);
    return changed;
}

bool GraphicsView::convertSelectedCirclesToDiameterDimensions(double offset)
{
    if (!m_document) return false;
    const QVector<int> indices = operationSelectionIndices();
    clearValidatedModifySelection();
    clearDimensionPickMarkers();
    QVector<int> newSelection = indices;
    bool changed = false;
    for (int index : indices) {
        const auto* c = dynamic_cast<const CadCircle*>(m_document->entityAt(index));
        if (!c) continue;
        const QPointF a(c->center().x() - c->radius(), c->center().y());
        const QPointF b(c->center().x() + c->radius(), c->center().y());
        auto dim = std::make_unique<CadLinearDimension>(a, b, QPointF(c->center().x(), c->center().y() + offset), m_annotationTextHeight);
        dim->setLayer(c->layer()); dim->setColor(c->color()); dim->setLineWeight(c->lineWeight()); dim->setLineType(c->lineType());
        m_document->addEntity(std::move(dim));
        newSelection.append(m_document->entityCount() - 1);
        changed = true;
    }
    if (changed) refreshFromDocumentPreservingSelection(newSelection);
    return changed;
}

bool GraphicsView::convertSelectedCirclesToRadiusDimensions(double offset)
{
    if (!m_document) return false;
    const QVector<int> indices = operationSelectionIndices();
    clearValidatedModifySelection();
    clearDimensionPickMarkers();
    QVector<int> newSelection = indices;
    bool changed = false;
    for (int index : indices) {
        const auto* c = dynamic_cast<const CadCircle*>(m_document->entityAt(index));
        if (!c) continue;
        const QPointF a = c->center();
        const QPointF b(c->center().x() + c->radius(), c->center().y());
        auto dim = std::make_unique<CadLinearDimension>(a, b, QPointF(c->center().x() + c->radius()/2.0, c->center().y() + offset), m_annotationTextHeight);
        dim->setLayer(c->layer()); dim->setColor(c->color()); dim->setLineWeight(c->lineWeight()); dim->setLineType(c->lineType());
        m_document->addEntity(std::move(dim));
        newSelection.append(m_document->entityCount() - 1);
        changed = true;
    }
    if (changed) refreshFromDocumentPreservingSelection(newSelection);
    return changed;
}
