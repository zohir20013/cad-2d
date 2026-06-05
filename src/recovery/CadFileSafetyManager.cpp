#include "recovery/CadFileSafetyManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace CadRecovery {
namespace {

static bool ensureDir(const QString& path, QString* error)
{
    QDir dir(path);
    if (dir.exists()) return true;
    if (dir.mkpath(QStringLiteral("."))) return true;
    if (error) *error = QStringLiteral("Cannot create directory: %1").arg(path);
    return false;
}

static QString timestampString(const QDateTime& dt)
{
    return dt.toUTC().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
}

static QJsonObject recoverySidecar(const QString& originalPath, const QString& dataPath)
{
    QJsonObject obj;
    obj[QStringLiteral("originalPath")] = QFileInfo(originalPath).absoluteFilePath();
    obj[QStringLiteral("dataPath")] = QFileInfo(dataPath).absoluteFilePath();
    obj[QStringLiteral("createdAtUtc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    obj[QStringLiteral("sha256")] = CadFileSafetyManager::sha256OfFile(dataPath);
    return obj;
}

} // namespace

CadFileSafetyManager::CadFileSafetyManager(const QString& applicationName)
    : m_applicationName(applicationName)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_recoveryDirectory = base.isEmpty()
        ? QDir::homePath() + QStringLiteral("/.") + m_applicationName + QStringLiteral("/recovery")
        : base + QStringLiteral("/recovery");
}

void CadFileSafetyManager::setRecoveryDirectory(const QString& directory)
{
    if (!directory.trimmed().isEmpty()) m_recoveryDirectory = directory;
}

QString CadFileSafetyManager::normalizedDocumentKey(const QString& documentPath)
{
    const QByteArray encoded = QFileInfo(documentPath).absoluteFilePath().toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(encoded).left(160);
}

QString CadFileSafetyManager::autosavePathForDocument(const QString& documentPath) const
{
    return QDir(m_recoveryDirectory).filePath(normalizedDocumentKey(documentPath) + QStringLiteral(".autosave.json"));
}

QString CadFileSafetyManager::backupPathForDocument(const QString& documentPath, const QDateTime& timestamp) const
{
    const QFileInfo info(documentPath);
    const QString name = info.completeBaseName().isEmpty() ? QStringLiteral("untitled") : info.completeBaseName();
    return QDir(m_recoveryDirectory).filePath(QStringLiteral("%1_%2.backup.json").arg(name, timestampString(timestamp)));
}

bool CadFileSafetyManager::safeWriteJson(const QString& path, const QJsonObject& object, QString* error) const
{
    const QFileInfo info(path);
    if (!ensureDir(info.absolutePath(), error)) return false;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool CadFileSafetyManager::safeReadJson(const QString& path, QJsonObject* object, QString* error) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = parseError.errorString();
        return false;
    }
    if (object) *object = doc.object();
    return true;
}

bool CadFileSafetyManager::createBackup(const QString& path, QString* backupPath, QString* error) const
{
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("Source file does not exist: %1").arg(path);
        return false;
    }
    if (!ensureDir(m_recoveryDirectory, error)) return false;
    const QString dest = backupPathForDocument(path);
    QFile::remove(dest);
    if (!QFile::copy(path, dest)) {
        if (error) *error = QStringLiteral("Cannot create backup: %1").arg(dest);
        return false;
    }
    safeWriteJson(dest + QStringLiteral(".meta"), recoverySidecar(path, dest), nullptr);
    if (backupPath) *backupPath = dest;
    return true;
}

bool CadFileSafetyManager::writeAutosave(const QString& originalPath, const QJsonObject& document, QString* autosavePath, QString* error) const
{
    if (!ensureDir(m_recoveryDirectory, error)) return false;
    const QString path = autosavePathForDocument(originalPath);
    QJsonObject wrapper;
    wrapper[QStringLiteral("originalPath")] = QFileInfo(originalPath).absoluteFilePath();
    wrapper[QStringLiteral("createdAtUtc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    wrapper[QStringLiteral("document")] = document;
    if (!safeWriteJson(path, wrapper, error)) return false;
    if (autosavePath) *autosavePath = path;
    return true;
}

bool CadFileSafetyManager::removeAutosave(const QString& originalPath, QString* error) const
{
    const QString path = autosavePathForDocument(originalPath);
    if (!QFileInfo::exists(path)) return true;
    if (QFile::remove(path)) return true;
    if (error) *error = QStringLiteral("Cannot remove autosave: %1").arg(path);
    return false;
}

QStringList CadFileSafetyManager::listAutosavePaths() const
{
    QDir dir(m_recoveryDirectory);
    return dir.entryList(QStringList() << QStringLiteral("*.autosave.json"), QDir::Files, QDir::Time);
}

QList<RecoveryFileInfo> CadFileSafetyManager::listRecoveryFiles() const
{
    QList<RecoveryFileInfo> out;
    QDir dir(m_recoveryDirectory);
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.autosave.json") << QStringLiteral("*.backup.json"),
                                                  QDir::Files, QDir::Time);
    for (const QFileInfo& info : files) {
        RecoveryFileInfo item;
        item.path = info.absoluteFilePath();
        item.createdAt = info.lastModified();
        item.sizeBytes = info.size();
        item.sha256 = sha256OfFile(item.path);
        QJsonObject wrapper;
        if (safeReadJson(item.path, &wrapper, nullptr)) {
            item.originalPath = wrapper.value(QStringLiteral("originalPath")).toString();
            item.valid = true;
        } else {
            QJsonObject meta;
            if (safeReadJson(item.path + QStringLiteral(".meta"), &meta, nullptr)) {
                item.originalPath = meta.value(QStringLiteral("originalPath")).toString();
                item.valid = true;
            }
        }
        out << item;
    }
    return out;
}

int CadFileSafetyManager::cleanupOldRecoveryFiles(int maxAgeDays, QString* error) const
{
    Q_UNUSED(error)
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-qMax(1, maxAgeDays));
    int removed = 0;
    QDir dir(m_recoveryDirectory);
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.autosave.json") << QStringLiteral("*.backup.json") << QStringLiteral("*.meta"), QDir::Files);
    for (const QFileInfo& info : files) {
        if (info.lastModified().toUTC() < cutoff && QFile::remove(info.absoluteFilePath())) ++removed;
    }
    return removed;
}

QString CadFileSafetyManager::sha256OfFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace CadRecovery
