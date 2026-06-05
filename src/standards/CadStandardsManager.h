#pragma once

#include "cad/CadDocument.h"
#include "cad/CadLayer.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CadStandards {

enum class StandardsSeverity {
    Info,
    Warning,
    Error
};

struct LayerStandard {
    QString name;
    QColor color = Qt::white;
    QString lineType = QStringLiteral("Continuous");
    double lineWeight = 0.0;
    bool required = false;
    bool allowEntityOverrides = true;

    QJsonObject toJson() const;
    static LayerStandard fromJson(const QJsonObject& obj);
};

struct CadStandardsIssue {
    StandardsSeverity severity = StandardsSeverity::Info;
    QString code;
    QString message;
    QString layerName;
    int entityIndex = -1;
    bool fixable = false;

    QJsonObject toJson() const;
};

struct CadStandardsReport {
    QVector<CadStandardsIssue> issues;
    int infoCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    int fixedCount = 0;

    bool hasErrors() const { return errorCount > 0; }
    bool clean() const { return issues.isEmpty(); }
    QJsonObject toJson() const;
};

class CadStandardsManager
{
public:
    QString profileName = QStringLiteral("Default metric CAD standard");
    QStringList allowedLineTypes = {QStringLiteral("Continuous"), QStringLiteral("Dashed"), QStringLiteral("Center"), QStringLiteral("Hidden"), QStringLiteral("Phantom")};
    QVector<double> allowedLineWeights = {0.0, 0.05, 0.09, 0.13, 0.18, 0.25, 0.35, 0.50, 0.70, 1.00, 1.40, 2.00};

    void clear();
    void addLayerStandard(const LayerStandard& standard);
    bool removeLayerStandard(const QString& layerName);
    bool hasLayerStandard(const QString& layerName) const;
    LayerStandard layerStandard(const QString& layerName, bool* found = nullptr) const;
    QStringList standardLayerNames() const;

    CadStandardsReport validate(const CadDocument& document) const;
    CadStandardsReport applyFixes(CadDocument& document, bool createMissingLayers = true, bool moveInvalidEntitiesToLayer0 = true) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

    static CadStandardsManager metricArchitecturalStandard();
    static CadStandardsManager metricMechanicalStandard();
    static QString severityName(StandardsSeverity severity);

private:
    QVector<LayerStandard> m_layers;

    int layerStandardIndex(const QString& layerName) const;
    static bool sameLayerName(const QString& a, const QString& b);
    static bool lineWeightAllowed(double value, const QVector<double>& allowed, double tolerance = 1.0e-6);
    static void appendIssue(CadStandardsReport& report, const CadStandardsIssue& issue);
};

} // namespace CadStandards
