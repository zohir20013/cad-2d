#pragma once

#include "CadEntity.h"
#include "CadLayer.h"

#include <QMap>
#include <QRectF>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <memory>
#include <vector>
#include <cstddef>

class QGraphicsScene;

class CadDocument
{
public:
    enum class Unit {
        Millimeter,
        Centimeter,
        Meter,
        Unitless
    };

    CadDocument();

    void clear();
    bool isModified() const { return m_modified; }
    void setModified(bool value) { m_modified = value; }

    Unit unit() const { return m_unit; }
    void setUnit(Unit unit);
    QString unitName() const;
    static QString unitName(Unit unit);
    static Unit unitFromName(const QString& name);

    QRectF limits() const { return m_limits; }
    void setLimits(const QRectF& limits);

    QString currentLayerName() const { return m_currentLayerName; }
    void setCurrentLayerName(const QString& name);

    const QMap<QString, CadLayer>& layers() const { return m_layers; }
    QMap<QString, CadLayer>& layers() { return m_layers; }
    void ensureLayer(const QString& name);
    bool createLayer(const QString& name);
    bool renameLayer(const QString& oldName, const QString& newName);
    bool deleteLayer(const QString& name, QString* error = nullptr);
    void setLayerVisible(const QString& name, bool visible);
    void setLayerLocked(const QString& name, bool locked);
    void setLayerColor(const QString& name, const QColor& color, bool applyToExistingEntities = true);
    void setLayerLineWeight(const QString& name, double lineWeight, bool applyToExistingEntities = true);
    void setLayerLineType(const QString& name, const QString& lineType, bool applyToExistingEntities = true);
    void moveEntitiesToLayer(const QVector<int>& indices, const QString& layerName);
    CadLayer currentLayer() const;

    const std::vector<std::unique_ptr<CadEntity>>& entities() const { return m_entities; }
    int entityCount() const;
    CadEntity* entityAt(int index);
    const CadEntity* entityAt(int index) const;
    void reserveEntities(size_t count);
    void addEntity(std::unique_ptr<CadEntity> entity);
    void removeEntities(const QVector<int>& indices);
    void duplicateEntities(const QVector<int>& indices, const QPointF& offset);
    void translateEntities(const QVector<int>& indices, const QPointF& delta);
    void rotateEntities(const QVector<int>& indices, const QPointF& center, double angleDeg);
    void scaleEntities(const QVector<int>& indices, const QPointF& center, double factor);
    void mirrorEntities(const QVector<int>& indices, const QPointF& axisA, const QPointF& axisB);

    QStringList blockNames() const;
    int blockReferenceCount(const QString& name = QString()) const;
    int blockDefinitionEntityCount(const QString& name) const;
    QString blockDefinitionSummary(const QString& name) const;
    bool createEmptyBlock(const QString& name, const QPointF& basePoint = QPointF(), QString* error = nullptr);
    bool upsertBlockDefinition(const QString& name, const QPointF& basePoint, const QJsonArray& entities, bool replaceExisting = true, QString* error = nullptr);
    bool createBlockFromEntities(const QString& name, const QVector<int>& indices, const QPointF& basePoint, QString* error = nullptr);
    bool insertBlockReference(const QString& name, const QPointF& insertionPoint, double scaleFactor = 1.0, double rotationDeg = 0.0, QString* error = nullptr);
    bool renameBlockDefinition(const QString& oldName, const QString& newName, QString* error = nullptr);
    bool duplicateBlockDefinition(const QString& sourceName, const QString& newName, QString* error = nullptr);
    bool removeBlockDefinition(const QString& name, QString* error = nullptr);
    int purgeUnusedBlockDefinitions(QStringList* purgedNames = nullptr);
    int explodeBlockReferences(const QVector<int>& indices, QString* error = nullptr);

    void renderToScene(QGraphicsScene* scene) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);
    bool saveToFile(const QString& path, QString* error = nullptr) const;
    bool loadFromFile(const QString& path, QString* error = nullptr);
    bool saveAsDxf(const QString& path, QString* error = nullptr) const;

private:
    Unit m_unit = Unit::Millimeter;
    QRectF m_limits = QRectF(0.0, 0.0, 420.0, 297.0);
    QString m_currentLayerName = "0";
    QMap<QString, CadLayer> m_layers;
    std::vector<std::unique_ptr<CadEntity>> m_entities;
    QMap<QString, QJsonObject> m_blockDefinitions;
    bool m_modified = false;
};
