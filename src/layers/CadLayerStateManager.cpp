#include "layers/CadLayerStateManager.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace CadLayers {

QJsonObject LayerState::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["currentLayer"] = currentLayer;
    QJsonArray arr;
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        arr.append(it.value().toJson());
    }
    obj["layers"] = arr;
    return obj;
}

LayerState LayerState::fromJson(const QJsonObject& obj)
{
    LayerState state;
    state.name = obj.value("name").toString();
    state.currentLayer = obj.value("currentLayer").toString(QStringLiteral("0"));
    const QJsonArray arr = obj.value("layers").toArray();
    for (const QJsonValue& value : arr) {
        const CadLayer layer = CadLayer::fromJson(value.toObject());
        state.layers.insert(layer.name, layer);
    }
    return state;
}

bool LayerStateManager::saveState(const CadDocument& document, const QString& stateName)
{
    const QString clean = stateName.trimmed();
    if (clean.isEmpty()) return false;
    LayerState state;
    state.name = clean;
    state.layers = document.layers();
    state.currentLayer = document.currentLayerName();
    m_states.insert(clean, state);
    return true;
}

bool LayerStateManager::restoreState(CadDocument& document, const QString& stateName, QString* error) const
{
    if (!m_states.contains(stateName)) {
        if (error) *error = QStringLiteral("Layer state not found: %1").arg(stateName);
        return false;
    }
    const LayerState state = m_states.value(stateName);
    document.layers() = state.layers;
    if (!state.currentLayer.isEmpty() && document.layers().contains(state.currentLayer)) {
        document.setCurrentLayerName(state.currentLayer);
    }
    document.setModified(true);
    return true;
}

bool LayerStateManager::removeState(const QString& stateName)
{
    return m_states.remove(stateName) > 0;
}

bool LayerStateManager::hasState(const QString& stateName) const
{
    return m_states.contains(stateName);
}

QStringList LayerStateManager::stateNames() const
{
    return m_states.keys();
}

QJsonObject LayerStateManager::toJson() const
{
    QJsonObject obj;
    QJsonArray arr;
    for (auto it = m_states.constBegin(); it != m_states.constEnd(); ++it) {
        arr.append(it.value().toJson());
    }
    obj["layerStates"] = arr;
    return obj;
}

bool LayerStateManager::fromJson(const QJsonObject& obj, QString* error)
{
    m_states.clear();
    const QJsonArray arr = obj.value("layerStates").toArray();
    for (const QJsonValue& value : arr) {
        const LayerState state = LayerState::fromJson(value.toObject());
        if (state.name.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("Invalid layer state with empty name");
            return false;
        }
        m_states.insert(state.name, state);
    }
    return true;
}

QStringList matchingLayers(const CadDocument& document, const LayerFilterRule& rule)
{
    QStringList result;
    QRegularExpression rx;
    if (!rule.namePattern.trimmed().isEmpty()) {
        QString pattern = QRegularExpression::escape(rule.namePattern.trimmed());
        pattern.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
        pattern.replace(QStringLiteral("\\?"), QStringLiteral("."));
        rx = QRegularExpression(QStringLiteral("^") + pattern + QStringLiteral("$"), QRegularExpression::CaseInsensitiveOption);
    }

    for (auto it = document.layers().constBegin(); it != document.layers().constEnd(); ++it) {
        const CadLayer& layer = it.value();
        if (rx.isValid() && !rule.namePattern.trimmed().isEmpty() && !rx.match(layer.name).hasMatch()) continue;
        if (rule.useColor && layer.color != rule.color) continue;
        if (rule.visibleOnly && !layer.visible) continue;
        if (rule.unlockedOnly && layer.locked) continue;
        result.push_back(layer.name);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

void isolateLayers(CadDocument& document, const QStringList& layerNames, bool lockOthers)
{
    QSet<QString> keep;
    for (const QString& name : layerNames) keep.insert(name);
    for (auto it = document.layers().begin(); it != document.layers().end(); ++it) {
        CadLayer& layer = it.value();
        const bool isolated = keep.contains(layer.name);
        layer.visible = isolated;
        if (lockOthers && !isolated) layer.locked = true;
        if (isolated) layer.locked = false;
    }
    document.setModified(true);
}

void showAllLayers(CadDocument& document)
{
    for (auto it = document.layers().begin(); it != document.layers().end(); ++it) {
        it.value().visible = true;
    }
    document.setModified(true);
}

void unlockAllLayers(CadDocument& document)
{
    for (auto it = document.layers().begin(); it != document.layers().end(); ++it) {
        it.value().locked = false;
    }
    document.setModified(true);
}

void freezeEmptyLayers(CadDocument& document)
{
    const QMap<QString, int> counts = entityCountByLayer(document);
    for (auto it = document.layers().begin(); it != document.layers().end(); ++it) {
        if (it.key() != QStringLiteral("0") && counts.value(it.key(), 0) == 0) {
            it.value().visible = false;
        }
    }
    document.setModified(true);
}

bool mergeLayers(CadDocument& document, const QStringList& sourceLayers, const QString& targetLayer, QString* error)
{
    const QString target = targetLayer.trimmed();
    if (target.isEmpty()) {
        if (error) *error = QStringLiteral("Target layer name is empty");
        return false;
    }
    document.ensureLayer(target);
    QSet<QString> sources;
    for (const QString& name : sourceLayers) sources.insert(name);
    sources.remove(target);
    if (sources.isEmpty()) return true;

    QStringList sourceNames;
    for (const QString& name : sources) sourceNames.push_back(name);
    QVector<int> toMove = entitiesOnLayers(document, sourceNames);
    document.moveEntitiesToLayer(toMove, target);
    for (const QString& layer : sources) {
        QString localError;
        document.deleteLayer(layer, &localError);
    }
    document.setModified(true);
    return true;
}

QMap<QString, int> entityCountByLayer(const CadDocument& document)
{
    QMap<QString, int> counts;
    for (auto it = document.layers().constBegin(); it != document.layers().constEnd(); ++it) {
        counts.insert(it.key(), 0);
    }
    for (const auto& entity : document.entities()) {
        if (entity) counts[entity->layer()] = counts.value(entity->layer(), 0) + 1;
    }
    return counts;
}

QVector<int> entitiesOnLayers(const CadDocument& document, const QStringList& layerNames)
{
    QVector<int> result;
    QSet<QString> names;
    for (const QString& name : layerNames) names.insert(name);
    for (int i = 0; i < document.entityCount(); ++i) {
        const CadEntity* entity = document.entityAt(i);
        if (entity && names.contains(entity->layer())) result.push_back(i);
    }
    return result;
}

} // namespace CadLayers
