#pragma once

#include <QColor>
#include <QtGlobal>

namespace CadRenderStyle {

inline QColor backgroundColor()
{
    // Fond CAD très sombre, légèrement bleuté: plus de contraste perçu sans
    // transformer le viewport en noir pur qui écrase les gris et les hachures.
    return QColor(7, 11, 16);
}

inline QColor minorGridColor()
{
    return QColor(58, 72, 88, 70);
}

inline QColor axisGridColor()
{
    return QColor(120, 152, 184, 135);
}

inline QColor deepCadColor(QColor color)
{
    if (!color.isValid()) color = Qt::white;
    if (color.alpha() <= 0) color.setAlpha(255);

    const int alpha = color.alpha();

    // Le noir DWG/DXF signifie souvent "couleur de fond" dans AutoCAD. Sur un
    // thème sombre il doit rester visible, mais moins laiteux que l'ancien gris.
    if (color == QColor(Qt::black)) return QColor(205, 214, 224, alpha);

    // Les blancs CAD restent lisibles mais légèrement refroidis pour éviter un
    // rendu brûlé quand beaucoup de lignes se superposent.
    if (color == QColor(Qt::white)) return QColor(238, 244, 250, alpha);

    int h = color.hue();
    int s = color.saturation();
    int v = color.value();

    // Gris purs: augmenter le contraste sans injecter de teinte arbitraire.
    if (h < 0 || s < 8) {
        v = qBound(115, static_cast<int>(v * 1.18) + 18, 230);
        return QColor(v, v, v, alpha);
    }

    // Pipeline couleur "profond": saturation renforcée + valeur contrôlée.
    // On évite le néon en plafonnant légèrement la luminosité des couleurs déjà
    // très vives, tout en remontant les couleurs sombres pour le fond CAD.
    s = qBound(105, static_cast<int>(s * 1.28) + 24, 255);
    v = qBound(170, static_cast<int>(v * 1.08) + 18, 242);

    QColor out;
    out.setHsv(h, s, v, alpha);
    return out;
}

inline QColor deepCadFillColor(QColor color, int alpha)
{
    QColor fill = deepCadColor(color);
    fill.setAlpha(qBound(0, alpha, 255));
    return fill;
}

} // namespace CadRenderStyle
