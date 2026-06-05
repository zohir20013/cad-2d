#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace CadRecovery {

struct RecoveryFileInfo {
    QString path;
    QString originalPath;
    QDateTime createdAt;
    qint64 sizeBytes = 0;
    QString sha256;
    bool valid = false;
};

class CadFileSafetyManager
{
public:
    explicit CadFileSafetyManager(const QString& applicationName = QStringLiteral("DWGViewerAdvanced"));

    QString recoveryDirectory() const { return m_recoveryDirectory; }
    void setRecoveryDirectory(const QString& directory);

    QString autosavePathForDocument(const QString& documentPath) const;
    QString backupPathForDocument(const QString& documentPath, const QDateTime& timestamp = QDateTime::currentDateTimeUtc()) const;

    bool safeWriteJson(const QString& path, const QJsonObject& object, QString* error = nullptr) const;
    bool safeReadJson(const QString& path, QJsonObject* object, QString* error = nullptr) const;
    bool createBackup(const QString& path, QString* backupPath = nullptr, QString* error = nullptr) const;
    bool writeAutosave(const QString& originalPath, const QJsonObject& document, QString* autosavePath = nullptr, QString* error = nullptr) const;
    bool removeAutosave(const QString& originalPath, QString* error = nullptr) const;

    QStringList listAutosavePaths() const;
    QList<RecoveryFileInfo> listRecoveryFiles() const;
    int cleanupOldRecoveryFiles(int maxAgeDays, QString* error = nullptr) const;

    static QString sha256OfFile(const QString& path);
    static QString normalizedDocumentKey(const QString& documentPath);

private:
    QString m_applicationName;
    QString m_recoveryDirectory;
};

} // namespace CadRecovery
