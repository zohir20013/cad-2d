#include "blocks/CadBlockAttributeEngine.h"
#include "geometry/GeometryKernel.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QtMath>

namespace CadBlocks {

QJsonObject AttributeDefinition::toJson() const
{
    QJsonObject obj;
    obj["tag"] = tag.toUpper();
    obj["prompt"] = prompt;
    obj["defaultValue"] = defaultValue;
    obj["x"] = position.x();
    obj["y"] = position.y();
    obj["height"] = height;
    obj["rotationDeg"] = rotationDeg;
    obj["constant"] = constant;
    obj["invisible"] = invisible;
    obj["required"] = required;
    return obj;
}

AttributeDefinition AttributeDefinition::fromJson(const QJsonObject& obj)
{
    AttributeDefinition def;
    def.tag = obj.value("tag").toString().trimmed().toUpper();
    def.prompt = obj.value("prompt").toString();
    def.defaultValue = obj.value("defaultValue").toString();
    def.position = QPointF(obj.value("x").toDouble(), obj.value("y").toDouble());
    def.height = qMax(1.0e-6, obj.value("height").toDouble(6.0));
    def.rotationDeg = obj.value("rotationDeg").toDouble(0.0);
    def.constant = obj.value("constant").toBool(false);
    def.invisible = obj.value("invisible").toBool(false);
    def.required = obj.value("required").toBool(false);
    return def;
}

QJsonObject AttributeValue::toJson() const
{
    QJsonObject obj;
    obj["tag"] = tag.toUpper();
    obj["value"] = value;
    obj["x"] = position.x();
    obj["y"] = position.y();
    obj["height"] = height;
    obj["rotationDeg"] = rotationDeg;
    obj["visible"] = visible;
    return obj;
}

AttributeValue AttributeValue::fromJson(const QJsonObject& obj)
{
    AttributeValue value;
    value.tag = obj.value("tag").toString().trimmed().toUpper();
    value.value = obj.value("value").toString();
    value.position = QPointF(obj.value("x").toDouble(), obj.value("y").toDouble());
    value.height = qMax(1.0e-6, obj.value("height").toDouble(6.0));
    value.rotationDeg = obj.value("rotationDeg").toDouble(0.0);
    value.visible = obj.value("visible").toBool(true);
    return value;
}

QJsonObject BlockDefinitionRecord::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["baseX"] = basePoint.x();
    obj["baseY"] = basePoint.y();
    obj["entities"] = entities;
    obj["description"] = description;
    QJsonArray attrs;
    for (const AttributeDefinition& attr : attributes) attrs.append(attr.toJson());
    obj["attributes"] = attrs;
    return obj;
}

BlockDefinitionRecord BlockDefinitionRecord::fromJson(const QJsonObject& obj)
{
    BlockDefinitionRecord rec;
    rec.name = obj.value("name").toString().trimmed();
    rec.basePoint = QPointF(obj.value("baseX").toDouble(), obj.value("baseY").toDouble());
    rec.entities = obj.value("entities").toArray();
    rec.description = obj.value("description").toString();
    const QJsonArray attrs = obj.value("attributes").toArray();
    for (const QJsonValue& value : attrs) rec.attributes.push_back(AttributeDefinition::fromJson(value.toObject()));
    return rec;
}

QPointF transformBlockPoint(const QPointF& localPoint, const QPointF& basePoint, const QPointF& insertionPoint,
                            double scaleFactor, double rotationDeg)
{
    const QPointF relative = (localPoint - basePoint) * scaleFactor;
    const QPointF scaled = insertionPoint + relative;
    return CadGeometry::rotatePoint(scaled, insertionPoint, rotationDeg);
}

QVector<AttributeValue> instantiateAttributes(const QVector<AttributeDefinition>& definitions,
                                              const QMap<QString, QString>& overrides,
                                              const QPointF& basePoint,
                                              const QPointF& insertionPoint,
                                              double scaleFactor,
                                              double rotationDeg)
{
    QVector<AttributeValue> out;
    out.reserve(definitions.size());
    for (const AttributeDefinition& def : definitions) {
        AttributeValue value;
        value.tag = def.tag.toUpper();
        value.value = def.constant ? def.defaultValue : overrides.value(value.tag, def.defaultValue);
        value.position = transformBlockPoint(def.position, basePoint, insertionPoint, scaleFactor, rotationDeg);
        value.height = def.height * qAbs(scaleFactor);
        value.rotationDeg = def.rotationDeg + rotationDeg;
        value.visible = !def.invisible;
        out.push_back(value);
    }
    return out;
}

QStringList missingRequiredAttributes(const QVector<AttributeDefinition>& definitions,
                                      const QMap<QString, QString>& values)
{
    QStringList missing;
    for (const AttributeDefinition& def : definitions) {
        const QString tag = def.tag.toUpper();
        if (def.required && !def.constant && values.value(tag).trimmed().isEmpty() && def.defaultValue.trimmed().isEmpty()) {
            missing.push_back(tag);
        }
    }
    missing.sort(Qt::CaseInsensitive);
    return missing;
}

QString evaluateAttributeField(const QString& value, const QMap<QString, QString>& fields)
{
    QString result = value;
    QRegularExpression rx(QStringLiteral("%<([^>]+)>%"));
    QRegularExpressionMatchIterator it = rx.globalMatch(value);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString key = match.captured(1).trimmed();
        result.replace(match.captured(0), fields.value(key, match.captured(0)));
    }
    return result;
}

QJsonObject attributesToJsonObject(const QVector<AttributeValue>& attributes)
{
    QJsonObject obj;
    for (const AttributeValue& attr : attributes) obj.insert(attr.tag.toUpper(), attr.toJson());
    return obj;
}

QVector<AttributeValue> attributesFromJsonObject(const QJsonObject& obj)
{
    QVector<AttributeValue> out;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        out.push_back(AttributeValue::fromJson(it.value().toObject()));
    }
    return out;
}

QRectF attributeBounds(const QVector<AttributeValue>& attributes)
{
    QRectF bounds;
    bool first = true;
    for (const AttributeValue& attr : attributes) {
        if (!attr.visible) continue;
        const double w = qMax(1.0, attr.value.size() * attr.height * 0.6);
        QRectF r(attr.position, QSizeF(w, attr.height));
        if (!qFuzzyIsNull(attr.rotationDeg)) {
            QVector<QPointF> pts;
            pts << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
            for (QPointF& p : pts) p = CadGeometry::rotatePoint(p, attr.position, attr.rotationDeg);
            r = CadGeometry::boundingRect(pts);
        }
        bounds = first ? r : bounds.united(r);
        first = false;
    }
    return bounds;
}

} // namespace CadBlocks
