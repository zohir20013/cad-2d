#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>

struct CadLayer
{
    QString name = "0";
    QColor color = Qt::white;
    bool visible = true;
    bool locked = false;
    double lineWeight = 0.0;
    QString lineType = "Continuous";

    QJsonObject toJson() const;
    static CadLayer fromJson(const QJsonObject& obj);
};
