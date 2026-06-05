#include "CadLayer.h"

QJsonObject CadLayer::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["color"] = color.name(QColor::HexArgb);
    obj["visible"] = visible;
    obj["locked"] = locked;
    obj["lineWeight"] = lineWeight;
    obj["lineType"] = lineType;
    return obj;
}

CadLayer CadLayer::fromJson(const QJsonObject& obj)
{
    CadLayer layer;
    layer.name = obj.value("name").toString("0");
    layer.color = QColor(obj.value("color").toString("#ffffffff"));
    if (!layer.color.isValid()) layer.color = Qt::white;
    layer.visible = obj.value("visible").toBool(true);
    layer.locked = obj.value("locked").toBool(false);
    layer.lineWeight = obj.value("lineWeight").toDouble(0.0);
    layer.lineType = obj.value("lineType").toString("Continuous");
    return layer;
}
