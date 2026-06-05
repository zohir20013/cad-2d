#pragma once

#include "cad/CadEntity.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include <memory>
#include <vector>

namespace CadSelection {

enum class WindowMode {
    Crossing,
    Contained
};

struct SelectionFilter {
    QSet<CadEntity::Type> allowedTypes;
    QSet<QString> allowedLayers;
    QSet<QString> rejectedLayers;
    QColor color;
    bool useColor = false;
    bool includeLockedLayers = false;
    bool includeHiddenLayers = false;
};

struct HitResult {
    bool valid = false;
    int entityIndex = -1;
    QPointF nearestPoint;
    double distance = 0.0;
};

class SelectionSet
{
public:
    void clear();
    bool isEmpty() const;
    int size() const;
    QVector<int> indices() const;
    bool contains(int index) const;
    void add(int index);
    void remove(int index);
    void toggle(int index);
    void uniteWith(const QVector<int>& indices);
    void subtract(const QVector<int>& indices);
    void intersectWith(const QVector<int>& indices);

private:
    QSet<int> m_indices;
};

bool entityPassesFilter(const CadEntity& entity, const SelectionFilter& filter);
HitResult pickNearest(const QPointF& point, const std::vector<std::unique_ptr<CadEntity>>& entities,
                      double aperture, const SelectionFilter& filter = SelectionFilter());
QVector<int> selectByWindow(const QRectF& window, const std::vector<std::unique_ptr<CadEntity>>& entities,
                            WindowMode mode, const SelectionFilter& filter = SelectionFilter());
QVector<int> selectByFence(const QVector<QPointF>& fence, const std::vector<std::unique_ptr<CadEntity>>& entities,
                           double tolerance, const SelectionFilter& filter = SelectionFilter());
QVector<int> selectByLayer(const QString& layerName, const std::vector<std::unique_ptr<CadEntity>>& entities);
QVector<int> selectByType(CadEntity::Type type, const std::vector<std::unique_ptr<CadEntity>>& entities);
QVector<int> invertSelection(const QVector<int>& current, int entityCount);
QVector<int> normalizeSelection(const QVector<int>& indices, int entityCount);

} // namespace CadSelection
