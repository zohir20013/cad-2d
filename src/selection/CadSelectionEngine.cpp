#include "selection/CadSelectionEngine.h"
#include "core/CadEntityUtils.h"
#include "geometry/GeometryKernel.h"

#include <algorithm>
#include <QtGlobal>
#include <limits>

namespace CadSelection {

void SelectionSet::clear() { m_indices.clear(); }
bool SelectionSet::isEmpty() const { return m_indices.isEmpty(); }
int SelectionSet::size() const { return m_indices.size(); }
QVector<int> SelectionSet::indices() const
{
    QVector<int> out;
    out.reserve(m_indices.size());
    for (int index : m_indices) out.push_back(index);
    std::sort(out.begin(), out.end());
    return out;
}
bool SelectionSet::contains(int index) const { return m_indices.contains(index); }
void SelectionSet::add(int index) { if (index >= 0) m_indices.insert(index); }
void SelectionSet::remove(int index) { m_indices.remove(index); }
void SelectionSet::toggle(int index)
{
    if (m_indices.contains(index)) m_indices.remove(index);
    else if (index >= 0) m_indices.insert(index);
}
void SelectionSet::uniteWith(const QVector<int>& indices)
{
    for (int index : indices) add(index);
}
void SelectionSet::subtract(const QVector<int>& indices)
{
    for (int index : indices) remove(index);
}
void SelectionSet::intersectWith(const QVector<int>& indices)
{
    QSet<int> other;
    for (int index : indices) other.insert(index);
    QSet<int> retained;
    for (int index : m_indices) {
        if (other.contains(index)) retained.insert(index);
    }
    m_indices = retained;
}

bool entityPassesFilter(const CadEntity& entity, const SelectionFilter& filter)
{
    if (!filter.allowedTypes.isEmpty() && !filter.allowedTypes.contains(entity.type())) {
        return false;
    }
    if (!filter.allowedLayers.isEmpty() && !filter.allowedLayers.contains(entity.layer())) {
        return false;
    }
    if (filter.rejectedLayers.contains(entity.layer())) {
        return false;
    }
    if (filter.useColor && entity.color() != filter.color) {
        return false;
    }
    return true;
}

HitResult pickNearest(const QPointF& point, const std::vector<std::unique_ptr<CadEntity>>& entities,
                      double aperture, const SelectionFilter& filter)
{
    HitResult best;
    best.distance = std::numeric_limits<double>::infinity();
    const double maxDistance = qMax(0.0, aperture);

    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        const auto& entity = entities[static_cast<size_t>(i)];
        if (!entity || !entityPassesFilter(*entity, filter)) continue;
        QPointF nearest;
        const double d = CadCore::distanceToEntity(point, *entity, &nearest);
        if (d <= maxDistance && d < best.distance) {
            best.valid = true;
            best.entityIndex = i;
            best.nearestPoint = nearest;
            best.distance = d;
        }
    }
    return best;
}

QVector<int> selectByWindow(const QRectF& window, const std::vector<std::unique_ptr<CadEntity>>& entities,
                            WindowMode mode, const SelectionFilter& filter)
{
    QVector<int> out;
    const QRectF query = window.normalized();
    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        const auto& entity = entities[static_cast<size_t>(i)];
        if (!entity || !entityPassesFilter(*entity, filter)) continue;
        const QRectF bounds = CadCore::entityBoundingRect(*entity);
        const bool accepted = (mode == WindowMode::Contained) ? query.contains(bounds) : query.intersects(bounds);
        if (accepted) out.push_back(i);
    }
    return out;
}

QVector<int> selectByFence(const QVector<QPointF>& fence, const std::vector<std::unique_ptr<CadEntity>>& entities,
                           double tolerance, const SelectionFilter& filter)
{
    QVector<int> out;
    if (fence.size() < 2) return out;
    const double tol = qMax(0.0, tolerance);

    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        const auto& entity = entities[static_cast<size_t>(i)];
        if (!entity || !entityPassesFilter(*entity, filter)) continue;
        const QVector<QPointF> pts = CadCore::representativePoints(*entity);
        bool hit = false;
        for (int f = 0; f + 1 < fence.size() && !hit; ++f) {
            for (int p = 0; p + 1 < pts.size() && !hit; ++p) {
                const auto ix = CadGeometry::intersectLineSegments(fence[f], fence[f + 1], pts[p], pts[p + 1], tol);
                if (ix.hasPoint() || ix.kind == CadGeometry::IntersectionKind::Overlap) hit = true;
            }
            if (!hit) {
                QPointF dummy;
                const double d0 = CadCore::distanceToEntity(fence[f], *entity, &dummy);
                const double d1 = CadCore::distanceToEntity(fence[f + 1], *entity, &dummy);
                hit = d0 <= tol || d1 <= tol;
            }
        }
        if (hit) out.push_back(i);
    }
    return out;
}

QVector<int> selectByLayer(const QString& layerName, const std::vector<std::unique_ptr<CadEntity>>& entities)
{
    QVector<int> out;
    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        if (entities[static_cast<size_t>(i)] && entities[static_cast<size_t>(i)]->layer() == layerName) {
            out.push_back(i);
        }
    }
    return out;
}

QVector<int> selectByType(CadEntity::Type type, const std::vector<std::unique_ptr<CadEntity>>& entities)
{
    QVector<int> out;
    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        if (entities[static_cast<size_t>(i)] && entities[static_cast<size_t>(i)]->type() == type) {
            out.push_back(i);
        }
    }
    return out;
}

QVector<int> invertSelection(const QVector<int>& current, int entityCount)
{
    QSet<int> selected;
    for (int index : current) {
        if (index >= 0 && index < entityCount) selected.insert(index);
    }
    QVector<int> out;
    for (int i = 0; i < entityCount; ++i) {
        if (!selected.contains(i)) out.push_back(i);
    }
    return out;
}

QVector<int> normalizeSelection(const QVector<int>& indices, int entityCount)
{
    QSet<int> unique;
    for (int index : indices) {
        if (index >= 0 && index < entityCount) unique.insert(index);
    }
    QVector<int> out;
    out.reserve(unique.size());
    for (int index : unique) out.push_back(index);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace CadSelection
