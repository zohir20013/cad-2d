#pragma once

#include <QString>
#include <QColor>

// Informations communes sur un calque CAD.
// Déplacé hors de DWGLoader pour permettre un mode DWG interne pur sans backend externe DWG.
struct LayerInfo {
    QString name;
    QColor color;
    bool visible = true;
};
