#include "styles/CadStyleManager.h"

#include <QJsonArray>
#include <QLocale>
#include <QtMath>

namespace CadStyles {
namespace {

template <typename T>
static QStringList sortedKeys(const QMap<QString, T>& map)
{
    QStringList keys = map.keys();
    keys.sort(Qt::CaseInsensitive);
    return keys;
}

static bool validName(const QString& name)
{
    return !name.trimmed().isEmpty();
}

static QJsonArray strokesToJson(const QVector<LinetypeStroke>& strokes)
{
    QJsonArray array;
    for (const auto& stroke : strokes) {
        QJsonObject obj;
        obj[QStringLiteral("length")] = stroke.length;
        obj[QStringLiteral("draw")] = stroke.draw;
        array.append(obj);
    }
    return array;
}

static QVector<LinetypeStroke> strokesFromJson(const QJsonArray& array)
{
    QVector<LinetypeStroke> strokes;
    for (const QJsonValue& value : array) {
        const QJsonObject obj = value.toObject();
        LinetypeStroke stroke;
        stroke.length = obj.value(QStringLiteral("length")).toDouble(1.0);
        stroke.draw = obj.value(QStringLiteral("draw")).toBool(true);
        if (stroke.length > 0.0) strokes << stroke;
    }
    return strokes;
}

} // namespace

QJsonObject TextStyle::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("fontFamily")] = fontFamily;
    obj[QStringLiteral("fixedHeight")] = fixedHeight;
    obj[QStringLiteral("widthFactor")] = widthFactor;
    obj[QStringLiteral("obliqueAngleDeg")] = obliqueAngleDeg;
    obj[QStringLiteral("annotative")] = annotative;
    return obj;
}

TextStyle TextStyle::fromJson(const QJsonObject& obj)
{
    TextStyle style;
    style.name = obj.value(QStringLiteral("name")).toString(style.name);
    style.fontFamily = obj.value(QStringLiteral("fontFamily")).toString(style.fontFamily);
    style.fixedHeight = obj.value(QStringLiteral("fixedHeight")).toDouble(style.fixedHeight);
    style.widthFactor = obj.value(QStringLiteral("widthFactor")).toDouble(style.widthFactor);
    style.obliqueAngleDeg = obj.value(QStringLiteral("obliqueAngleDeg")).toDouble(style.obliqueAngleDeg);
    style.annotative = obj.value(QStringLiteral("annotative")).toBool(style.annotative);
    return style;
}

QJsonObject DimensionStyle::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("textStyle")] = textStyle;
    obj[QStringLiteral("textHeight")] = textHeight;
    obj[QStringLiteral("arrowSize")] = arrowSize;
    obj[QStringLiteral("extensionOffset")] = extensionOffset;
    obj[QStringLiteral("extensionOvershoot")] = extensionOvershoot;
    obj[QStringLiteral("dimensionGap")] = dimensionGap;
    obj[QStringLiteral("precision")] = precision;
    obj[QStringLiteral("unitSuffix")] = unitSuffix;
    obj[QStringLiteral("decimalSeparator")] = decimalSeparator;
    obj[QStringLiteral("suppressTrailingZeros")] = suppressTrailingZeros;
    obj[QStringLiteral("annotative")] = annotative;
    return obj;
}

DimensionStyle DimensionStyle::fromJson(const QJsonObject& obj)
{
    DimensionStyle style;
    style.name = obj.value(QStringLiteral("name")).toString(style.name);
    style.textStyle = obj.value(QStringLiteral("textStyle")).toString(style.textStyle);
    style.textHeight = obj.value(QStringLiteral("textHeight")).toDouble(style.textHeight);
    style.arrowSize = obj.value(QStringLiteral("arrowSize")).toDouble(style.arrowSize);
    style.extensionOffset = obj.value(QStringLiteral("extensionOffset")).toDouble(style.extensionOffset);
    style.extensionOvershoot = obj.value(QStringLiteral("extensionOvershoot")).toDouble(style.extensionOvershoot);
    style.dimensionGap = obj.value(QStringLiteral("dimensionGap")).toDouble(style.dimensionGap);
    style.precision = obj.value(QStringLiteral("precision")).toInt(style.precision);
    style.unitSuffix = obj.value(QStringLiteral("unitSuffix")).toString(style.unitSuffix);
    style.decimalSeparator = obj.value(QStringLiteral("decimalSeparator")).toString(style.decimalSeparator);
    style.suppressTrailingZeros = obj.value(QStringLiteral("suppressTrailingZeros")).toBool(style.suppressTrailingZeros);
    style.annotative = obj.value(QStringLiteral("annotative")).toBool(style.annotative);
    return style;
}

QJsonObject LinetypeDefinition::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("pattern")] = strokesToJson(pattern);
    obj[QStringLiteral("globalScale")] = globalScale;
    return obj;
}

LinetypeDefinition LinetypeDefinition::fromJson(const QJsonObject& obj)
{
    LinetypeDefinition def;
    def.name = obj.value(QStringLiteral("name")).toString(def.name);
    def.description = obj.value(QStringLiteral("description")).toString(def.description);
    def.pattern = strokesFromJson(obj.value(QStringLiteral("pattern")).toArray());
    def.globalScale = obj.value(QStringLiteral("globalScale")).toDouble(def.globalScale);
    return def;
}

QJsonObject MultileaderStyle::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("textStyle")] = textStyle;
    obj[QStringLiteral("textHeight")] = textHeight;
    obj[QStringLiteral("landingLength")] = landingLength;
    obj[QStringLiteral("arrowSize")] = arrowSize;
    obj[QStringLiteral("doglegEnabled")] = doglegEnabled;
    return obj;
}

MultileaderStyle MultileaderStyle::fromJson(const QJsonObject& obj)
{
    MultileaderStyle style;
    style.name = obj.value(QStringLiteral("name")).toString(style.name);
    style.textStyle = obj.value(QStringLiteral("textStyle")).toString(style.textStyle);
    style.textHeight = obj.value(QStringLiteral("textHeight")).toDouble(style.textHeight);
    style.landingLength = obj.value(QStringLiteral("landingLength")).toDouble(style.landingLength);
    style.arrowSize = obj.value(QStringLiteral("arrowSize")).toDouble(style.arrowSize);
    style.doglegEnabled = obj.value(QStringLiteral("doglegEnabled")).toBool(style.doglegEnabled);
    return style;
}

QJsonObject TableStyle::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("textStyle")] = textStyle;
    obj[QStringLiteral("titleHeight")] = titleHeight;
    obj[QStringLiteral("headerHeight")] = headerHeight;
    obj[QStringLiteral("dataHeight")] = dataHeight;
    obj[QStringLiteral("cellMargin")] = cellMargin;
    return obj;
}

TableStyle TableStyle::fromJson(const QJsonObject& obj)
{
    TableStyle style;
    style.name = obj.value(QStringLiteral("name")).toString(style.name);
    style.textStyle = obj.value(QStringLiteral("textStyle")).toString(style.textStyle);
    style.titleHeight = obj.value(QStringLiteral("titleHeight")).toDouble(style.titleHeight);
    style.headerHeight = obj.value(QStringLiteral("headerHeight")).toDouble(style.headerHeight);
    style.dataHeight = obj.value(QStringLiteral("dataHeight")).toDouble(style.dataHeight);
    style.cellMargin = obj.value(QStringLiteral("cellMargin")).toDouble(style.cellMargin);
    return style;
}

CadStyleManager::CadStyleManager()
{
    resetToIsoDefaults();
}

void CadStyleManager::resetToIsoDefaults()
{
    m_textStyles.clear();
    m_dimensionStyles.clear();
    m_linetypes.clear();
    m_multileaderStyles.clear();
    m_tableStyles.clear();

    addTextStyle(TextStyle{});

    DimensionStyle dim;
    addDimensionStyle(dim);

    addLinetype(LinetypeDefinition{});
    addLinetype(LinetypeDefinition{QStringLiteral("Hidden"), QStringLiteral("Hidden __ __ __"), {{6.0, true}, {3.0, false}}, 1.0});
    addLinetype(LinetypeDefinition{QStringLiteral("Center"), QStringLiteral("Center ____ _ ____"), {{10.0, true}, {2.0, false}, {2.0, true}, {2.0, false}}, 1.0});
    addLinetype(LinetypeDefinition{QStringLiteral("Dashed"), QStringLiteral("Dashed __ __ __"), {{5.0, true}, {2.5, false}}, 1.0});

    addMultileaderStyle(MultileaderStyle{});
    addTableStyle(TableStyle{});

    m_currentTextStyle = QStringLiteral("Standard");
    m_currentDimensionStyle = QStringLiteral("ISO-25");
    m_currentLinetype = QStringLiteral("Continuous");
}

void CadStyleManager::resetToArchitecturalDefaults()
{
    resetToIsoDefaults();
    DimensionStyle architectural = dimensionStyle(QStringLiteral("ISO-25"));
    architectural.name = QStringLiteral("Architectural");
    architectural.precision = 3;
    architectural.unitSuffix = QStringLiteral(" in");
    architectural.textHeight = 0.125;
    architectural.arrowSize = 0.125;
    addDimensionStyle(architectural);
    m_currentDimensionStyle = architectural.name;
}

bool CadStyleManager::addTextStyle(const TextStyle& style, QString* error)
{
    if (!validName(style.name)) {
        if (error) *error = QStringLiteral("Text style name is empty.");
        return false;
    }
    m_textStyles[style.name] = style;
    return true;
}

bool CadStyleManager::addDimensionStyle(const DimensionStyle& style, QString* error)
{
    if (!validName(style.name)) {
        if (error) *error = QStringLiteral("Dimension style name is empty.");
        return false;
    }
    m_dimensionStyles[style.name] = style;
    return true;
}

bool CadStyleManager::addLinetype(const LinetypeDefinition& linetype, QString* error)
{
    if (!validName(linetype.name)) {
        if (error) *error = QStringLiteral("Linetype name is empty.");
        return false;
    }
    m_linetypes[linetype.name] = linetype;
    return true;
}

bool CadStyleManager::addMultileaderStyle(const MultileaderStyle& style, QString* error)
{
    if (!validName(style.name)) {
        if (error) *error = QStringLiteral("Multileader style name is empty.");
        return false;
    }
    m_multileaderStyles[style.name] = style;
    return true;
}

bool CadStyleManager::addTableStyle(const TableStyle& style, QString* error)
{
    if (!validName(style.name)) {
        if (error) *error = QStringLiteral("Table style name is empty.");
        return false;
    }
    m_tableStyles[style.name] = style;
    return true;
}

TextStyle CadStyleManager::textStyle(const QString& name) const
{
    return m_textStyles.value(name, m_textStyles.value(QStringLiteral("Standard")));
}

DimensionStyle CadStyleManager::dimensionStyle(const QString& name) const
{
    return m_dimensionStyles.value(name, m_dimensionStyles.value(QStringLiteral("ISO-25")));
}

LinetypeDefinition CadStyleManager::linetype(const QString& name) const
{
    return m_linetypes.value(name, m_linetypes.value(QStringLiteral("Continuous")));
}

MultileaderStyle CadStyleManager::multileaderStyle(const QString& name) const
{
    return m_multileaderStyles.value(name, m_multileaderStyles.value(QStringLiteral("Standard")));
}

TableStyle CadStyleManager::tableStyle(const QString& name) const
{
    return m_tableStyles.value(name, m_tableStyles.value(QStringLiteral("Standard")));
}

void CadStyleManager::setCurrentTextStyle(const QString& name)
{
    if (m_textStyles.contains(name)) m_currentTextStyle = name;
}

void CadStyleManager::setCurrentDimensionStyle(const QString& name)
{
    if (m_dimensionStyles.contains(name)) m_currentDimensionStyle = name;
}

void CadStyleManager::setCurrentLinetype(const QString& name)
{
    if (m_linetypes.contains(name)) m_currentLinetype = name;
}

QStringList CadStyleManager::textStyleNames() const { return sortedKeys(m_textStyles); }
QStringList CadStyleManager::dimensionStyleNames() const { return sortedKeys(m_dimensionStyles); }
QStringList CadStyleManager::linetypeNames() const { return sortedKeys(m_linetypes); }
QStringList CadStyleManager::multileaderStyleNames() const { return sortedKeys(m_multileaderStyles); }
QStringList CadStyleManager::tableStyleNames() const { return sortedKeys(m_tableStyles); }

QString CadStyleManager::formatDistance(double value, const QString& dimensionStyleName) const
{
    const DimensionStyle style = dimensionStyle(dimensionStyleName.isEmpty() ? m_currentDimensionStyle : dimensionStyleName);
    QString text = QString::number(value, 'f', qBound(0, style.precision, 12));
    if (style.suppressTrailingZeros && text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) text.chop(1);
        if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    }
    if (style.decimalSeparator != QStringLiteral(".")) text.replace(QLatin1Char('.'), style.decimalSeparator);
    return text + style.unitSuffix;
}

QVector<double> CadStyleManager::linetypeDashPattern(const QString& linetypeName, double entityScale) const
{
    QVector<double> pattern;
    const LinetypeDefinition def = linetype(linetypeName);
    for (const auto& stroke : def.pattern) {
        pattern << qMax(0.01, stroke.length * def.globalScale * entityScale);
    }
    return pattern;
}

QJsonObject CadStyleManager::toJson() const
{
    QJsonObject obj;
    QJsonArray textStyles;
    for (const auto& style : m_textStyles) textStyles.append(style.toJson());
    QJsonArray dimStyles;
    for (const auto& style : m_dimensionStyles) dimStyles.append(style.toJson());
    QJsonArray linetypes;
    for (const auto& def : m_linetypes) linetypes.append(def.toJson());
    QJsonArray mleaders;
    for (const auto& style : m_multileaderStyles) mleaders.append(style.toJson());
    QJsonArray tables;
    for (const auto& style : m_tableStyles) tables.append(style.toJson());

    obj[QStringLiteral("textStyles")] = textStyles;
    obj[QStringLiteral("dimensionStyles")] = dimStyles;
    obj[QStringLiteral("linetypes")] = linetypes;
    obj[QStringLiteral("multileaderStyles")] = mleaders;
    obj[QStringLiteral("tableStyles")] = tables;
    obj[QStringLiteral("currentTextStyle")] = m_currentTextStyle;
    obj[QStringLiteral("currentDimensionStyle")] = m_currentDimensionStyle;
    obj[QStringLiteral("currentLinetype")] = m_currentLinetype;
    return obj;
}

bool CadStyleManager::fromJson(const QJsonObject& obj, QString* error)
{
    Q_UNUSED(error)
    m_textStyles.clear();
    m_dimensionStyles.clear();
    m_linetypes.clear();
    m_multileaderStyles.clear();
    m_tableStyles.clear();

    for (const QJsonValue& value : obj.value(QStringLiteral("textStyles")).toArray()) addTextStyle(TextStyle::fromJson(value.toObject()));
    for (const QJsonValue& value : obj.value(QStringLiteral("dimensionStyles")).toArray()) addDimensionStyle(DimensionStyle::fromJson(value.toObject()));
    for (const QJsonValue& value : obj.value(QStringLiteral("linetypes")).toArray()) addLinetype(LinetypeDefinition::fromJson(value.toObject()));
    for (const QJsonValue& value : obj.value(QStringLiteral("multileaderStyles")).toArray()) addMultileaderStyle(MultileaderStyle::fromJson(value.toObject()));
    for (const QJsonValue& value : obj.value(QStringLiteral("tableStyles")).toArray()) addTableStyle(TableStyle::fromJson(value.toObject()));

    if (m_textStyles.isEmpty() || m_dimensionStyles.isEmpty() || m_linetypes.isEmpty()) resetToIsoDefaults();
    setCurrentTextStyle(obj.value(QStringLiteral("currentTextStyle")).toString(m_currentTextStyle));
    setCurrentDimensionStyle(obj.value(QStringLiteral("currentDimensionStyle")).toString(m_currentDimensionStyle));
    setCurrentLinetype(obj.value(QStringLiteral("currentLinetype")).toString(m_currentLinetype));
    return true;
}

} // namespace CadStyles
