#include "CadStandardsManager.h"

#include <QJsonArray>
#include <QtMath>

namespace CadStandards {

namespace {
QJsonObject colorToJson(const QColor& c)
{
    QJsonObject o;
    o["r"] = c.red();
    o["g"] = c.green();
    o["b"] = c.blue();
    o["a"] = c.alpha();
    return o;
}

QColor colorFromJson(const QJsonObject& o, const QColor& fallback)
{
    if (o.isEmpty()) return fallback;
    return QColor(o.value("r").toInt(fallback.red()),
                  o.value("g").toInt(fallback.green()),
                  o.value("b").toInt(fallback.blue()),
                  o.value("a").toInt(fallback.alpha()));
}

QJsonArray stringListToJson(const QStringList& items)
{
    return QJsonArray::fromStringList(items);
}

QStringList stringListFromJson(const QJsonArray& array)
{
    QStringList list;
    for (const QJsonValue& v : array) list << v.toString();
    return list;
}

QJsonArray doublesToJson(const QVector<double>& values)
{
    QJsonArray arr;
    for (double v : values) arr.append(v);
    return arr;
}

QVector<double> doublesFromJson(const QJsonArray& array)
{
    QVector<double> values;
    for (const QJsonValue& v : array) values.push_back(v.toDouble());
    return values;
}
}

QJsonObject LayerStandard::toJson() const
{
    QJsonObject obj;
    obj["name"] = name;
    obj["color"] = colorToJson(color);
    obj["lineType"] = lineType;
    obj["lineWeight"] = lineWeight;
    obj["required"] = required;
    obj["allowEntityOverrides"] = allowEntityOverrides;
    return obj;
}

LayerStandard LayerStandard::fromJson(const QJsonObject& obj)
{
    LayerStandard s;
    s.name = obj.value("name").toString();
    s.color = colorFromJson(obj.value("color").toObject(), s.color);
    s.lineType = obj.value("lineType").toString(s.lineType);
    s.lineWeight = obj.value("lineWeight").toDouble(s.lineWeight);
    s.required = obj.value("required").toBool(s.required);
    s.allowEntityOverrides = obj.value("allowEntityOverrides").toBool(s.allowEntityOverrides);
    return s;
}

QJsonObject CadStandardsIssue::toJson() const
{
    QJsonObject obj;
    obj["severity"] = CadStandardsManager::severityName(severity);
    obj["code"] = code;
    obj["message"] = message;
    obj["layerName"] = layerName;
    obj["entityIndex"] = entityIndex;
    obj["fixable"] = fixable;
    return obj;
}

QJsonObject CadStandardsReport::toJson() const
{
    QJsonObject obj;
    obj["infoCount"] = infoCount;
    obj["warningCount"] = warningCount;
    obj["errorCount"] = errorCount;
    obj["fixedCount"] = fixedCount;
    QJsonArray arr;
    for (const CadStandardsIssue& issue : issues) arr.append(issue.toJson());
    obj["issues"] = arr;
    return obj;
}

void CadStandardsManager::clear()
{
    m_layers.clear();
}

void CadStandardsManager::addLayerStandard(const LayerStandard& standard)
{
    const int index = layerStandardIndex(standard.name);
    if (index >= 0) m_layers[index] = standard;
    else m_layers.push_back(standard);
}

bool CadStandardsManager::removeLayerStandard(const QString& layerName)
{
    const int index = layerStandardIndex(layerName);
    if (index < 0) return false;
    m_layers.removeAt(index);
    return true;
}

bool CadStandardsManager::hasLayerStandard(const QString& layerName) const
{
    return layerStandardIndex(layerName) >= 0;
}

LayerStandard CadStandardsManager::layerStandard(const QString& layerName, bool* found) const
{
    const int index = layerStandardIndex(layerName);
    const bool ok = index >= 0;
    if (found) *found = ok;
    return ok ? m_layers[index] : LayerStandard{};
}

QStringList CadStandardsManager::standardLayerNames() const
{
    QStringList names;
    for (const LayerStandard& s : m_layers) names << s.name;
    names.sort(Qt::CaseInsensitive);
    return names;
}

CadStandardsReport CadStandardsManager::validate(const CadDocument& document) const
{
    CadStandardsReport report;

    for (const LayerStandard& standard : m_layers) {
        if (standard.required && !document.layers().contains(standard.name)) {
            appendIssue(report, {StandardsSeverity::Error,
                                 QStringLiteral("MISSING_REQUIRED_LAYER"),
                                 QStringLiteral("Required layer '%1' is missing.").arg(standard.name),
                                 standard.name,
                                 -1,
                                 true});
        }
    }

    for (auto it = document.layers().constBegin(); it != document.layers().constEnd(); ++it) {
        const QString layerName = it.key();
        const CadLayer& layer = it.value();
        bool found = false;
        const LayerStandard standard = layerStandard(layerName, &found);
        if (!found) {
            appendIssue(report, {StandardsSeverity::Warning,
                                 QStringLiteral("NON_STANDARD_LAYER"),
                                 QStringLiteral("Layer '%1' is not defined in the standards profile.").arg(layerName),
                                 layerName,
                                 -1,
                                 false});
            continue;
        }
        if (layer.color != standard.color) {
            appendIssue(report, {StandardsSeverity::Warning,
                                 QStringLiteral("LAYER_COLOR_MISMATCH"),
                                 QStringLiteral("Layer '%1' has a non-standard color.").arg(layerName),
                                 layerName,
                                 -1,
                                 true});
        }
        if (!sameLayerName(layer.lineType, standard.lineType)) {
            appendIssue(report, {StandardsSeverity::Warning,
                                 QStringLiteral("LAYER_LINETYPE_MISMATCH"),
                                 QStringLiteral("Layer '%1' has a non-standard line type.").arg(layerName),
                                 layerName,
                                 -1,
                                 true});
        }
        if (!qFuzzyCompare(layer.lineWeight + 1.0, standard.lineWeight + 1.0)) {
            appendIssue(report, {StandardsSeverity::Warning,
                                 QStringLiteral("LAYER_LINEWEIGHT_MISMATCH"),
                                 QStringLiteral("Layer '%1' has a non-standard line weight.").arg(layerName),
                                 layerName,
                                 -1,
                                 true});
        }
    }

    for (int i = 0; i < document.entityCount(); ++i) {
        const CadEntity* entity = document.entityAt(i);
        if (!entity) continue;
        if (!document.layers().contains(entity->layer())) {
            appendIssue(report, {StandardsSeverity::Error,
                                 QStringLiteral("ENTITY_LAYER_MISSING"),
                                 QStringLiteral("Entity %1 references missing layer '%2'.").arg(i).arg(entity->layer()),
                                 entity->layer(),
                                 i,
                                 true});
            continue;
        }
        bool found = false;
        const LayerStandard standard = layerStandard(entity->layer(), &found);
        if (found && !standard.allowEntityOverrides) {
            if (entity->color() != standard.color || !sameLayerName(entity->lineType(), standard.lineType)
                || !qFuzzyCompare(entity->lineWeight() + 1.0, standard.lineWeight + 1.0)) {
                appendIssue(report, {StandardsSeverity::Warning,
                                     QStringLiteral("ENTITY_OVERRIDE_NOT_ALLOWED"),
                                     QStringLiteral("Entity %1 has style overrides on controlled layer '%2'.").arg(i).arg(entity->layer()),
                                     entity->layer(),
                                     i,
                                     true});
            }
        }
        if (!allowedLineTypes.contains(entity->lineType(), Qt::CaseInsensitive)) {
            appendIssue(report, {StandardsSeverity::Warning,
                                 QStringLiteral("ENTITY_UNKNOWN_LINETYPE"),
                                 QStringLiteral("Entity %1 uses unknown line type '%2'.").arg(i).arg(entity->lineType()),
                                 entity->layer(),
                                 i,
                                 true});
        }
        if (!lineWeightAllowed(entity->lineWeight(), allowedLineWeights)) {
            appendIssue(report, {StandardsSeverity::Warning,
                                 QStringLiteral("ENTITY_NON_STANDARD_LINEWEIGHT"),
                                 QStringLiteral("Entity %1 uses non-standard line weight.").arg(i),
                                 entity->layer(),
                                 i,
                                 true});
        }
    }

    return report;
}

CadStandardsReport CadStandardsManager::applyFixes(CadDocument& document, bool createMissingLayers, bool moveInvalidEntitiesToLayer0) const
{
    CadStandardsReport report = validate(document);
    int fixed = 0;

    if (createMissingLayers) {
        for (const LayerStandard& standard : m_layers) {
            if (standard.required && !document.layers().contains(standard.name)) {
                document.createLayer(standard.name);
                document.setLayerColor(standard.name, standard.color, false);
                document.setLayerLineType(standard.name, standard.lineType, false);
                document.setLayerLineWeight(standard.name, standard.lineWeight, false);
                ++fixed;
            }
        }
    }

    for (const LayerStandard& standard : m_layers) {
        if (document.layers().contains(standard.name)) {
            document.setLayerColor(standard.name, standard.color, false);
            document.setLayerLineType(standard.name, standard.lineType, false);
            document.setLayerLineWeight(standard.name, standard.lineWeight, false);
        }
    }

    if (moveInvalidEntitiesToLayer0) {
        document.ensureLayer(QStringLiteral("0"));
        QVector<int> invalidIndices;
        for (int i = 0; i < document.entityCount(); ++i) {
            CadEntity* entity = document.entityAt(i);
            if (!entity) continue;
            if (!document.layers().contains(entity->layer())) invalidIndices.push_back(i);
        }
        if (!invalidIndices.isEmpty()) {
            document.moveEntitiesToLayer(invalidIndices, QStringLiteral("0"));
            fixed += invalidIndices.size();
        }
    }

    for (int i = 0; i < document.entityCount(); ++i) {
        CadEntity* entity = document.entityAt(i);
        if (!entity) continue;
        bool found = false;
        const LayerStandard standard = layerStandard(entity->layer(), &found);
        if (found && !standard.allowEntityOverrides) {
            entity->setColor(standard.color);
            entity->setLineType(standard.lineType);
            entity->setLineWeight(standard.lineWeight);
            ++fixed;
        } else {
            if (!allowedLineTypes.contains(entity->lineType(), Qt::CaseInsensitive)) {
                entity->setLineType(QStringLiteral("Continuous"));
                ++fixed;
            }
            if (!lineWeightAllowed(entity->lineWeight(), allowedLineWeights)) {
                entity->setLineWeight(0.0);
                ++fixed;
            }
        }
    }

    report.fixedCount = fixed;
    return report;
}

QJsonObject CadStandardsManager::toJson() const
{
    QJsonObject obj;
    obj["profileName"] = profileName;
    obj["allowedLineTypes"] = stringListToJson(allowedLineTypes);
    obj["allowedLineWeights"] = doublesToJson(allowedLineWeights);
    QJsonArray layers;
    for (const LayerStandard& layer : m_layers) layers.append(layer.toJson());
    obj["layers"] = layers;
    return obj;
}

bool CadStandardsManager::fromJson(const QJsonObject& obj, QString* error)
{
    profileName = obj.value("profileName").toString(profileName);
    const QStringList lineTypes = stringListFromJson(obj.value("allowedLineTypes").toArray());
    if (!lineTypes.isEmpty()) allowedLineTypes = lineTypes;
    const QVector<double> weights = doublesFromJson(obj.value("allowedLineWeights").toArray());
    if (!weights.isEmpty()) allowedLineWeights = weights;
    m_layers.clear();
    const QJsonArray layers = obj.value("layers").toArray();
    for (const QJsonValue& v : layers) {
        const LayerStandard s = LayerStandard::fromJson(v.toObject());
        if (s.name.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("Layer standard with empty name.");
            return false;
        }
        addLayerStandard(s);
    }
    return true;
}

CadStandardsManager CadStandardsManager::metricArchitecturalStandard()
{
    CadStandardsManager manager;
    manager.profileName = QStringLiteral("Metric architectural CAD standard");
    manager.addLayerStandard({"0", QColor(Qt::white), "Continuous", 0.0, true, true});
    manager.addLayerStandard({"A-WALL", QColor(255, 255, 255), "Continuous", 0.35, true, false});
    manager.addLayerStandard({"A-DOOR", QColor(0, 255, 0), "Continuous", 0.25, false, false});
    manager.addLayerStandard({"A-WIND", QColor(0, 255, 255), "Continuous", 0.25, false, false});
    manager.addLayerStandard({"A-DIMS", QColor(255, 255, 0), "Continuous", 0.18, true, false});
    manager.addLayerStandard({"A-TEXT", QColor(255, 255, 0), "Continuous", 0.18, true, false});
    manager.addLayerStandard({"A-HATCH", QColor(128, 128, 128), "Continuous", 0.09, false, true});
    manager.addLayerStandard({"A-CNTR", QColor(255, 0, 255), "Center", 0.13, false, false});
    return manager;
}

CadStandardsManager CadStandardsManager::metricMechanicalStandard()
{
    CadStandardsManager manager;
    manager.profileName = QStringLiteral("Metric mechanical CAD standard");
    manager.addLayerStandard({"0", QColor(Qt::white), "Continuous", 0.0, true, true});
    manager.addLayerStandard({"M-OBJECT", QColor(255, 255, 255), "Continuous", 0.35, true, false});
    manager.addLayerStandard({"M-HIDDEN", QColor(255, 255, 0), "Hidden", 0.18, false, false});
    manager.addLayerStandard({"M-CENTER", QColor(255, 0, 255), "Center", 0.13, false, false});
    manager.addLayerStandard({"M-DIMS", QColor(0, 255, 255), "Continuous", 0.18, true, false});
    manager.addLayerStandard({"M-HATCH", QColor(128, 128, 128), "Continuous", 0.09, false, true});
    return manager;
}

QString CadStandardsManager::severityName(StandardsSeverity severity)
{
    switch (severity) {
    case StandardsSeverity::Info: return QStringLiteral("Info");
    case StandardsSeverity::Warning: return QStringLiteral("Warning");
    case StandardsSeverity::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Info");
}

int CadStandardsManager::layerStandardIndex(const QString& layerName) const
{
    for (int i = 0; i < m_layers.size(); ++i) {
        if (sameLayerName(m_layers[i].name, layerName)) return i;
    }
    return -1;
}

bool CadStandardsManager::sameLayerName(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

bool CadStandardsManager::lineWeightAllowed(double value, const QVector<double>& allowed, double tolerance)
{
    for (double candidate : allowed) {
        if (qAbs(candidate - value) <= tolerance) return true;
    }
    return false;
}

void CadStandardsManager::appendIssue(CadStandardsReport& report, const CadStandardsIssue& issue)
{
    report.issues.push_back(issue);
    switch (issue.severity) {
    case StandardsSeverity::Info: ++report.infoCount; break;
    case StandardsSeverity::Warning: ++report.warningCount; break;
    case StandardsSeverity::Error: ++report.errorCount; break;
    }
}

} // namespace CadStandards
