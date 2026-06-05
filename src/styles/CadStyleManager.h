#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CadStyles {

struct TextStyle {
    QString name = QStringLiteral("Standard");
    QString fontFamily = QStringLiteral("TXT");
    double fixedHeight = 0.0;
    double widthFactor = 1.0;
    double obliqueAngleDeg = 0.0;
    bool annotative = false;
    QJsonObject toJson() const;
    static TextStyle fromJson(const QJsonObject& obj);
};

struct DimensionStyle {
    QString name = QStringLiteral("ISO-25");
    QString textStyle = QStringLiteral("Standard");
    double textHeight = 2.5;
    double arrowSize = 2.5;
    double extensionOffset = 0.625;
    double extensionOvershoot = 1.25;
    double dimensionGap = 0.625;
    int precision = 2;
    QString unitSuffix;
    QString decimalSeparator = QStringLiteral(".");
    bool suppressTrailingZeros = false;
    bool annotative = false;
    QJsonObject toJson() const;
    static DimensionStyle fromJson(const QJsonObject& obj);
};

struct LinetypeStroke {
    double length = 1.0;
    bool draw = true;
};

struct LinetypeDefinition {
    QString name = QStringLiteral("Continuous");
    QString description = QStringLiteral("Solid line");
    QVector<LinetypeStroke> pattern;
    double globalScale = 1.0;
    QJsonObject toJson() const;
    static LinetypeDefinition fromJson(const QJsonObject& obj);
};

struct MultileaderStyle {
    QString name = QStringLiteral("Standard");
    QString textStyle = QStringLiteral("Standard");
    double textHeight = 2.5;
    double landingLength = 6.0;
    double arrowSize = 2.5;
    bool doglegEnabled = true;
    QJsonObject toJson() const;
    static MultileaderStyle fromJson(const QJsonObject& obj);
};

struct TableStyle {
    QString name = QStringLiteral("Standard");
    QString textStyle = QStringLiteral("Standard");
    double titleHeight = 3.5;
    double headerHeight = 3.0;
    double dataHeight = 2.5;
    double cellMargin = 1.5;
    QJsonObject toJson() const;
    static TableStyle fromJson(const QJsonObject& obj);
};

class CadStyleManager
{
public:
    CadStyleManager();

    void resetToIsoDefaults();
    void resetToArchitecturalDefaults();

    bool addTextStyle(const TextStyle& style, QString* error = nullptr);
    bool addDimensionStyle(const DimensionStyle& style, QString* error = nullptr);
    bool addLinetype(const LinetypeDefinition& linetype, QString* error = nullptr);
    bool addMultileaderStyle(const MultileaderStyle& style, QString* error = nullptr);
    bool addTableStyle(const TableStyle& style, QString* error = nullptr);

    TextStyle textStyle(const QString& name) const;
    DimensionStyle dimensionStyle(const QString& name) const;
    LinetypeDefinition linetype(const QString& name) const;
    MultileaderStyle multileaderStyle(const QString& name) const;
    TableStyle tableStyle(const QString& name) const;

    QString currentTextStyle() const { return m_currentTextStyle; }
    QString currentDimensionStyle() const { return m_currentDimensionStyle; }
    QString currentLinetype() const { return m_currentLinetype; }
    void setCurrentTextStyle(const QString& name);
    void setCurrentDimensionStyle(const QString& name);
    void setCurrentLinetype(const QString& name);

    QStringList textStyleNames() const;
    QStringList dimensionStyleNames() const;
    QStringList linetypeNames() const;
    QStringList multileaderStyleNames() const;
    QStringList tableStyleNames() const;

    QString formatDistance(double value, const QString& dimensionStyleName = QString()) const;
    QVector<double> linetypeDashPattern(const QString& linetypeName, double entityScale = 1.0) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

private:
    QMap<QString, TextStyle> m_textStyles;
    QMap<QString, DimensionStyle> m_dimensionStyles;
    QMap<QString, LinetypeDefinition> m_linetypes;
    QMap<QString, MultileaderStyle> m_multileaderStyles;
    QMap<QString, TableStyle> m_tableStyles;
    QString m_currentTextStyle = QStringLiteral("Standard");
    QString m_currentDimensionStyle = QStringLiteral("ISO-25");
    QString m_currentLinetype = QStringLiteral("Continuous");
};

} // namespace CadStyles
