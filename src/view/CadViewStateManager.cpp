#include "CadViewStateManager.h"

#include <QJsonArray>
#include <QMap>
#include <QtMath>
#include <algorithm>

namespace CadView {

namespace {
QJsonObject pointToJson(const QPointF& p)
{
    QJsonObject o;
    o["x"] = p.x();
    o["y"] = p.y();
    return o;
}

QPointF pointFromJson(const QJsonObject& o)
{
    return QPointF(o.value("x").toDouble(), o.value("y").toDouble());
}

QJsonObject rectToJson(const QRectF& r)
{
    QJsonObject o;
    o["x"] = r.x();
    o["y"] = r.y();
    o["width"] = r.width();
    o["height"] = r.height();
    return o;
}

QRectF rectFromJson(const QJsonObject& o)
{
    return QRectF(o.value("x").toDouble(), o.value("y").toDouble(),
                  o.value("width").toDouble(), o.value("height").toDouble());
}
}

QJsonObject CadViewState::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["center"] = pointToJson(center);
    obj["scale"] = scale;
    obj["rotationDeg"] = rotationDeg;
    obj["modelWindow"] = rectToJson(modelWindow);
    obj["hasModelWindow"] = hasModelWindow;
    return obj;
}

CadViewState CadViewState::fromJson(const QJsonObject& obj)
{
    CadViewState state;
    state.name = obj.value("name").toString();
    state.center = pointFromJson(obj.value("center").toObject());
    state.scale = obj.value("scale").toDouble(1.0);
    state.rotationDeg = obj.value("rotationDeg").toDouble(0.0);
    state.modelWindow = rectFromJson(obj.value("modelWindow").toObject());
    state.hasModelWindow = obj.value("hasModelWindow").toBool(false);
    if (state.scale <= 0.0) state.scale = 1.0;
    return state;
}

CadViewStateManager::CadViewStateManager()
{
    reset();
}

void CadViewStateManager::setCurrentView(const CadViewState& view)
{
    if (view.scale <= 0.0) return;
    m_current = view;
}

void CadViewStateManager::reset()
{
    m_current = CadViewState{};
    m_current.name = QStringLiteral("Current");
    m_current.center = QPointF(0.0, 0.0);
    m_current.scale = 1.0;
    m_current.rotationDeg = 0.0;
    m_current.hasModelWindow = false;
}

CadViewState CadViewStateManager::zoomExtents(const QRectF& modelBounds, const QSizeF& viewportPixels, double marginFactor) const
{
    QRectF bounds = normalizedWindow(modelBounds);
    if (bounds.isEmpty() || viewportPixels.width() <= 0.0 || viewportPixels.height() <= 0.0) {
        return m_current;
    }
    CadViewState view;
    view.name = QStringLiteral("Zoom Extents");
    view.center = bounds.center();
    view.scale = scaleToFit(bounds, viewportPixels, marginFactor);
    view.rotationDeg = m_current.rotationDeg;
    view.modelWindow = bounds;
    view.hasModelWindow = true;
    return view;
}

CadViewState CadViewStateManager::zoomWindow(const QRectF& modelWindow, const QSizeF& viewportPixels) const
{
    CadViewState view = makeViewFromWindow(modelWindow, viewportPixels);
    view.rotationDeg = m_current.rotationDeg;
    return view;
}

CadViewState CadViewStateManager::zoomScale(double factor, const QPointF& fixedModelPoint) const
{
    CadViewState view = m_current;
    if (factor <= 0.0) return view;
    view.scale = std::max(1.0e-9, view.scale * factor);
    if (!fixedModelPoint.isNull()) {
        const QPointF oldVector = fixedModelPoint - m_current.center;
        const QPointF newVector = oldVector / factor;
        view.center = fixedModelPoint - newVector;
    }
    view.hasModelWindow = false;
    return view;
}

CadViewState CadViewStateManager::panByModelDelta(const QPointF& delta) const
{
    CadViewState view = m_current;
    view.center += delta;
    view.hasModelWindow = false;
    return view;
}

CadViewState CadViewStateManager::rotateView(double angleDeg, const QPointF& center) const
{
    CadViewState view = m_current;
    view.rotationDeg += angleDeg;
    while (view.rotationDeg >= 360.0) view.rotationDeg -= 360.0;
    while (view.rotationDeg < 0.0) view.rotationDeg += 360.0;
    if (!center.isNull()) view.center = center;
    return view;
}

bool CadViewStateManager::saveNamedView(const QString& name, const CadViewState& view)
{
    const QString key = name.trimmed();
    if (key.isEmpty() || view.scale <= 0.0) return false;
    CadViewState saved = view;
    saved.name = key;
    m_namedViews.insert(key.toUpper(), saved);
    return true;
}

bool CadViewStateManager::removeNamedView(const QString& name)
{
    return m_namedViews.remove(name.trimmed().toUpper()) > 0;
}

bool CadViewStateManager::hasNamedView(const QString& name) const
{
    return m_namedViews.contains(name.trimmed().toUpper());
}

CadViewState CadViewStateManager::namedView(const QString& name, bool* found) const
{
    const QString key = name.trimmed().toUpper();
    const bool ok = m_namedViews.contains(key);
    if (found) *found = ok;
    return ok ? m_namedViews.value(key) : CadViewState{};
}

QStringList CadViewStateManager::namedViewNames() const
{
    QStringList names;
    for (const CadViewState& view : m_namedViews) names << view.name;
    names.sort(Qt::CaseInsensitive);
    return names;
}

void CadViewStateManager::pushPreviousView(const CadViewState& view)
{
    if (view.scale <= 0.0) return;
    m_backStack.push_back(view);
    while (m_backStack.size() > m_maxNavigation) m_backStack.removeFirst();
    m_forwardStack.clear();
}

bool CadViewStateManager::canGoBack() const { return !m_backStack.isEmpty(); }
bool CadViewStateManager::canGoForward() const { return !m_forwardStack.isEmpty(); }

CadViewState CadViewStateManager::previousView(bool* ok)
{
    if (m_backStack.isEmpty()) {
        if (ok) *ok = false;
        return m_current;
    }
    m_forwardStack.push_back(m_current);
    m_current = m_backStack.takeLast();
    if (ok) *ok = true;
    return m_current;
}

CadViewState CadViewStateManager::nextView(bool* ok)
{
    if (m_forwardStack.isEmpty()) {
        if (ok) *ok = false;
        return m_current;
    }
    m_backStack.push_back(m_current);
    m_current = m_forwardStack.takeLast();
    if (ok) *ok = true;
    return m_current;
}

void CadViewStateManager::clearNavigationHistory()
{
    m_backStack.clear();
    m_forwardStack.clear();
}

QTransform CadViewStateManager::modelToViewTransform(const QSizeF& viewportPixels) const
{
    QTransform t;
    t.translate(viewportPixels.width() * 0.5, viewportPixels.height() * 0.5);
    t.scale(m_current.scale, -m_current.scale);
    t.rotate(-m_current.rotationDeg);
    t.translate(-m_current.center.x(), -m_current.center.y());
    return t;
}

QTransform CadViewStateManager::viewToModelTransform(const QSizeF& viewportPixels) const
{
    return modelToViewTransform(viewportPixels).inverted();
}

QPointF CadViewStateManager::modelToView(const QPointF& modelPoint, const QSizeF& viewportPixels) const
{
    return modelToViewTransform(viewportPixels).map(modelPoint);
}

QPointF CadViewStateManager::viewToModel(const QPointF& viewPoint, const QSizeF& viewportPixels) const
{
    return viewToModelTransform(viewportPixels).map(viewPoint);
}

QJsonObject CadViewStateManager::toJson() const
{
    QJsonObject obj;
    obj["current"] = m_current.toJson();
    QJsonArray named;
    for (const CadViewState& view : m_namedViews) named.append(view.toJson());
    obj["namedViews"] = named;
    return obj;
}

bool CadViewStateManager::fromJson(const QJsonObject& obj, QString* error)
{
    const CadViewState current = CadViewState::fromJson(obj.value("current").toObject());
    if (current.scale <= 0.0) {
        if (error) *error = QStringLiteral("Invalid current view scale.");
        return false;
    }
    m_current = current;
    m_namedViews.clear();
    const QJsonArray named = obj.value("namedViews").toArray();
    for (const QJsonValue& v : named) {
        CadViewState view = CadViewState::fromJson(v.toObject());
        if (!view.name.trimmed().isEmpty() && view.scale > 0.0) {
            m_namedViews.insert(view.name.trimmed().toUpper(), view);
        }
    }
    return true;
}

CadViewState CadViewStateManager::makeViewFromWindow(const QRectF& modelWindow, const QSizeF& viewportPixels)
{
    CadViewState view;
    const QRectF window = normalizedWindow(modelWindow);
    view.name = QStringLiteral("Zoom Window");
    view.center = window.center();
    view.scale = scaleToFit(window, viewportPixels, 0.0);
    view.modelWindow = window;
    view.hasModelWindow = true;
    return view;
}

double CadViewStateManager::scaleToFit(const QRectF& modelBounds, const QSizeF& viewportPixels, double marginFactor)
{
    QRectF bounds = normalizedWindow(modelBounds);
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0 || viewportPixels.width() <= 0.0 || viewportPixels.height() <= 0.0) {
        return 1.0;
    }
    const double usableW = viewportPixels.width() * std::max(0.01, 1.0 - 2.0 * marginFactor);
    const double usableH = viewportPixels.height() * std::max(0.01, 1.0 - 2.0 * marginFactor);
    return std::max(1.0e-9, std::min(usableW / bounds.width(), usableH / bounds.height()));
}

QRectF CadViewStateManager::normalizedWindow(const QRectF& rect)
{
    QRectF r = rect.normalized();
    if (r.width() < 1.0e-9) r.adjust(-0.5, 0.0, 0.5, 0.0);
    if (r.height() < 1.0e-9) r.adjust(0.0, -0.5, 0.0, 0.5);
    return r;
}

} // namespace CadView
