#include "quality/CadAuditEngine.h"
#include "core/CadEntityUtils.h"
#include "geometry/GeometryKernel.h"
#include "layers/CadLayerStateManager.h"

#include <QCryptographicHash>
#include <QSet>
#include <QtMath>
#include <QStringList>
#include <algorithm>

namespace CadQuality {
namespace {

static void addIssue(AuditReport& report, AuditSeverity severity, AuditCode code, int entityIndex,
                     const QString& layerName, const QString& message)
{
    AuditIssue issue;
    issue.severity = severity;
    issue.code = code;
    issue.entityIndex = entityIndex;
    issue.layerName = layerName;
    issue.message = message;
    report.issues.push_back(issue);
    switch (severity) {
    case AuditSeverity::Info: ++report.infoCount; break;
    case AuditSeverity::Warning: ++report.warningCount; break;
    case AuditSeverity::Error: ++report.errorCount; break;
    }
}

static QString roundedPointKey(const QPointF& p, double tolerance)
{
    const double scale = tolerance > 0.0 ? 1.0 / tolerance : 1.0e7;
    return QStringLiteral("%1,%2").arg(qRound64(p.x() * scale)).arg(qRound64(p.y() * scale));
}

static QString duplicateKey(const CadEntity& entity, double tolerance)
{
    const QString type = CadCore::entityTypeName(entity);
    QVector<QPointF> pts = CadCore::representativePoints(entity, 32);
    QStringList parts;
    parts << type << entity.layer();
    for (const QPointF& p : pts) parts << roundedPointKey(p, tolerance);
    return parts.join(QLatin1Char('|'));
}

} // namespace

QString severityName(AuditSeverity severity)
{
    switch (severity) {
    case AuditSeverity::Info: return QStringLiteral("Info");
    case AuditSeverity::Warning: return QStringLiteral("Warning");
    case AuditSeverity::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString auditCodeName(AuditCode code)
{
    switch (code) {
    case AuditCode::EmptyDocument: return QStringLiteral("EmptyDocument");
    case AuditCode::MissingLayer: return QStringLiteral("MissingLayer");
    case AuditCode::EmptyLayer: return QStringLiteral("EmptyLayer");
    case AuditCode::DegenerateEntity: return QStringLiteral("DegenerateEntity");
    case AuditCode::DuplicateGeometry: return QStringLiteral("DuplicateGeometry");
    case AuditCode::OutOfLimits: return QStringLiteral("OutOfLimits");
    case AuditCode::InvalidStyleValue: return QStringLiteral("InvalidStyleValue");
    case AuditCode::VerySmallGeometry: return QStringLiteral("VerySmallGeometry");
    case AuditCode::VeryLargeCoordinate: return QStringLiteral("VeryLargeCoordinate");
    }
    return QStringLiteral("Unknown");
}

QJsonObject AuditIssue::toJson() const
{
    QJsonObject obj;
    obj["severity"] = severityName(severity);
    obj["code"] = auditCodeName(code);
    obj["entityIndex"] = entityIndex;
    obj["layerName"] = layerName;
    obj["message"] = message;
    return obj;
}

QJsonObject AuditReport::toJson() const
{
    QJsonObject obj;
    obj["clean"] = clean();
    obj["infoCount"] = infoCount;
    obj["warningCount"] = warningCount;
    obj["errorCount"] = errorCount;
    QJsonArray arr;
    for (const AuditIssue& issue : issues) arr.append(issue.toJson());
    obj["issues"] = arr;
    return obj;
}

AuditReport auditDocument(const CadDocument& document, double tolerance)
{
    AuditReport report;
    const double tol = qMax(1.0e-12, tolerance);

    if (document.entityCount() == 0) {
        addIssue(report, AuditSeverity::Info, AuditCode::EmptyDocument, -1, QString(), QStringLiteral("The drawing contains no entities."));
    }

    const QMap<QString, int> counts = CadLayers::entityCountByLayer(document);
    for (auto it = document.layers().constBegin(); it != document.layers().constEnd(); ++it) {
        const CadLayer& layer = it.value();
        if (layer.name != QStringLiteral("0") && counts.value(layer.name, 0) == 0) {
            addIssue(report, AuditSeverity::Info, AuditCode::EmptyLayer, -1, layer.name,
                     QStringLiteral("Layer '%1' has no entities.").arg(layer.name));
        }
        if (layer.lineWeight < 0.0) {
            addIssue(report, AuditSeverity::Warning, AuditCode::InvalidStyleValue, -1, layer.name,
                     QStringLiteral("Layer '%1' has a negative lineweight.").arg(layer.name));
        }
    }

    QSet<QString> duplicateKeys;
    for (int i = 0; i < document.entityCount(); ++i) {
        const CadEntity* entity = document.entityAt(i);
        if (!entity) continue;
        if (!document.layers().contains(entity->layer())) {
            addIssue(report, AuditSeverity::Warning, AuditCode::MissingLayer, i, entity->layer(),
                     QStringLiteral("Entity %1 references missing layer '%2'.").arg(i).arg(entity->layer()));
        }
        if (CadCore::isEntityDegenerate(*entity, tol)) {
            addIssue(report, AuditSeverity::Warning, AuditCode::DegenerateEntity, i, entity->layer(),
                     QStringLiteral("Entity %1 is degenerate or has zero geometry.").arg(i));
        }
        if (entity->lineWeight() < 0.0) {
            addIssue(report, AuditSeverity::Warning, AuditCode::InvalidStyleValue, i, entity->layer(),
                     QStringLiteral("Entity %1 has a negative lineweight.").arg(i));
        }

        const QRectF bounds = CadCore::entityBoundingRect(*entity);
        if (!document.limits().contains(bounds)) {
            addIssue(report, AuditSeverity::Info, AuditCode::OutOfLimits, i, entity->layer(),
                     QStringLiteral("Entity %1 is outside the drawing limits.").arg(i));
        }
        if (qAbs(bounds.left()) > 1.0e9 || qAbs(bounds.right()) > 1.0e9 ||
            qAbs(bounds.top()) > 1.0e9 || qAbs(bounds.bottom()) > 1.0e9) {
            addIssue(report, AuditSeverity::Warning, AuditCode::VeryLargeCoordinate, i, entity->layer(),
                     QStringLiteral("Entity %1 has very large coordinates.").arg(i));
        }
        if (bounds.width() > 0.0 && bounds.height() > 0.0 && bounds.width() < tol && bounds.height() < tol) {
            addIssue(report, AuditSeverity::Info, AuditCode::VerySmallGeometry, i, entity->layer(),
                     QStringLiteral("Entity %1 is smaller than the audit tolerance.").arg(i));
        }

        const QString key = duplicateKey(*entity, tol);
        if (duplicateKeys.contains(key)) {
            addIssue(report, AuditSeverity::Info, AuditCode::DuplicateGeometry, i, entity->layer(),
                     QStringLiteral("Entity %1 appears to duplicate earlier geometry.").arg(i));
        } else {
            duplicateKeys.insert(key);
        }
    }
    return report;
}

int purgeDegenerateEntities(CadDocument& document, double tolerance)
{
    QVector<int> remove;
    for (int i = 0; i < document.entityCount(); ++i) {
        const CadEntity* entity = document.entityAt(i);
        if (entity && CadCore::isEntityDegenerate(*entity, tolerance)) remove.push_back(i);
    }
    document.removeEntities(remove);
    return remove.size();
}

int purgeEmptyLayers(CadDocument& document)
{
    const QMap<QString, int> counts = CadLayers::entityCountByLayer(document);
    int removed = 0;
    QStringList names = document.layers().keys();
    for (const QString& name : names) {
        if (name == QStringLiteral("0")) continue;
        if (counts.value(name, 0) == 0) {
            QString error;
            if (document.deleteLayer(name, &error)) ++removed;
        }
    }
    return removed;
}

int healMissingLayers(CadDocument& document)
{
    int created = 0;
    for (int i = 0; i < document.entityCount(); ++i) {
        const CadEntity* entity = document.entityAt(i);
        if (!entity) continue;
        if (!document.layers().contains(entity->layer())) {
            document.ensureLayer(entity->layer());
            ++created;
        }
    }
    if (created > 0) document.setModified(true);
    return created;
}

void normalizeEntityStyles(CadDocument& document)
{
    for (int i = 0; i < document.entityCount(); ++i) {
        CadEntity* entity = document.entityAt(i);
        if (!entity) continue;
        if (entity->lineWeight() < 0.0) entity->setLineWeight(0.0);
        if (entity->lineType().trimmed().isEmpty()) entity->setLineType(QStringLiteral("Continuous"));
        if (!entity->color().isValid()) entity->setColor(Qt::white);
    }
    for (auto it = document.layers().begin(); it != document.layers().end(); ++it) {
        if (it.value().lineWeight < 0.0) it.value().lineWeight = 0.0;
        if (it.value().lineType.trimmed().isEmpty()) it.value().lineType = QStringLiteral("Continuous");
        if (!it.value().color.isValid()) it.value().color = Qt::white;
    }
    document.setModified(true);
}

} // namespace CadQuality
