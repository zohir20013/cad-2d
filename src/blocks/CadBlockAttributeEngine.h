#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CadBlocks {

struct AttributeDefinition {
    QString tag;
    QString prompt;
    QString defaultValue;
    QPointF position;
    double height = 6.0;
    double rotationDeg = 0.0;
    bool constant = false;
    bool invisible = false;
    bool required = false;
    QJsonObject toJson() const;
    static AttributeDefinition fromJson(const QJsonObject& obj);
};

struct AttributeValue {
    QString tag;
    QString value;
    QPointF position;
    double height = 6.0;
    double rotationDeg = 0.0;
    bool visible = true;
    QJsonObject toJson() const;
    static AttributeValue fromJson(const QJsonObject& obj);
};

struct BlockDefinitionRecord {
    QString name;
    QPointF basePoint;
    QJsonArray entities;
    QVector<AttributeDefinition> attributes;
    QString description;
    QJsonObject toJson() const;
    static BlockDefinitionRecord fromJson(const QJsonObject& obj);
};

QVector<AttributeValue> instantiateAttributes(const QVector<AttributeDefinition>& definitions,
                                              const QMap<QString, QString>& overrides,
                                              const QPointF& basePoint,
                                              const QPointF& insertionPoint,
                                              double scaleFactor,
                                              double rotationDeg);
QStringList missingRequiredAttributes(const QVector<AttributeDefinition>& definitions,
                                      const QMap<QString, QString>& values);
QString evaluateAttributeField(const QString& value, const QMap<QString, QString>& fields);
QJsonObject attributesToJsonObject(const QVector<AttributeValue>& attributes);
QVector<AttributeValue> attributesFromJsonObject(const QJsonObject& obj);
QRectF attributeBounds(const QVector<AttributeValue>& attributes);
QPointF transformBlockPoint(const QPointF& localPoint, const QPointF& basePoint, const QPointF& insertionPoint,
                            double scaleFactor, double rotationDeg);

} // namespace CadBlocks
