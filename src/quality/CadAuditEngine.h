#pragma once

#include "cad/CadDocument.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace CadQuality {

enum class AuditSeverity {
    Info,
    Warning,
    Error
};

enum class AuditCode {
    EmptyDocument,
    MissingLayer,
    EmptyLayer,
    DegenerateEntity,
    DuplicateGeometry,
    OutOfLimits,
    InvalidStyleValue,
    VerySmallGeometry,
    VeryLargeCoordinate
};

struct AuditIssue {
    AuditSeverity severity = AuditSeverity::Info;
    AuditCode code = AuditCode::EmptyDocument;
    int entityIndex = -1;
    QString layerName;
    QString message;
    QJsonObject toJson() const;
};

struct AuditReport {
    QVector<AuditIssue> issues;
    int infoCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    bool clean() const { return warningCount == 0 && errorCount == 0; }
    QJsonObject toJson() const;
};

QString severityName(AuditSeverity severity);
QString auditCodeName(AuditCode code);
AuditReport auditDocument(const CadDocument& document, double tolerance = 1.0e-7);
int purgeDegenerateEntities(CadDocument& document, double tolerance = 1.0e-7);
int purgeEmptyLayers(CadDocument& document);
int healMissingLayers(CadDocument& document);
void normalizeEntityStyles(CadDocument& document);

} // namespace CadQuality
