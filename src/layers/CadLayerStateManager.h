#pragma once

#include "cad/CadDocument.h"
#include "cad/CadLayer.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CadLayers {

struct LayerState {
    QString name;
    QMap<QString, CadLayer> layers;
    QString currentLayer;
    QJsonObject toJson() const;
    static LayerState fromJson(const QJsonObject& obj);
};

struct LayerFilterRule {
    QString namePattern;
    QColor color;
    bool useColor = false;
    bool visibleOnly = false;
    bool unlockedOnly = false;
};

class LayerStateManager
{
public:
    bool saveState(const CadDocument& document, const QString& stateName);
    bool restoreState(CadDocument& document, const QString& stateName, QString* error = nullptr) const;
    bool removeState(const QString& stateName);
    bool hasState(const QString& stateName) const;
    QStringList stateNames() const;
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

private:
    QMap<QString, LayerState> m_states;
};

QStringList matchingLayers(const CadDocument& document, const LayerFilterRule& rule);
void isolateLayers(CadDocument& document, const QStringList& layerNames, bool lockOthers = false);
void showAllLayers(CadDocument& document);
void unlockAllLayers(CadDocument& document);
void freezeEmptyLayers(CadDocument& document);
bool mergeLayers(CadDocument& document, const QStringList& sourceLayers, const QString& targetLayer, QString* error = nullptr);
QMap<QString, int> entityCountByLayer(const CadDocument& document);
QVector<int> entitiesOnLayers(const CadDocument& document, const QStringList& layerNames);

} // namespace CadLayers
