#include "OdaFileConverter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QUuid>

namespace {

QString cleanPath(QString path)
{
    path = path.trimmed();
    if ((path.startsWith('"') && path.endsWith('"')) ||
        (path.startsWith('\'') && path.endsWith('\''))) {
        path = path.mid(1, path.size() - 2);
    }
    return QDir::cleanPath(path);
}

QStringList candidateExecutables()
{
    QStringList out;

    const QStringList envNames = {
        QStringLiteral("DWGVIEWER_ODA_CONVERTER_PATH"),
        QStringLiteral("ODA_FILE_CONVERTER"),
        QStringLiteral("ODAFC_PATH")
    };
    for (const QString& name : envNames) {
        const QByteArray value = qgetenv(name.toUtf8().constData());
        if (!value.isEmpty())
            out << cleanPath(QString::fromLocal8Bit(value));
    }

    const QStringList names = {
        QStringLiteral("ODAFileConverter"),
        QStringLiteral("ODAFileConverter_QT5_lnxX64"),
        QStringLiteral("ODAFileConverter_QT6_lnxX64"),
        QStringLiteral("ODAFileConverter_lnxX64")
    };
    for (const QString& name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            out << found;
    }

    out << QStringLiteral("/usr/bin/ODAFileConverter")
        << QStringLiteral("/usr/local/bin/ODAFileConverter")
        << QStringLiteral("/opt/oda/ODAFileConverter/ODAFileConverter")
        << QStringLiteral("/opt/ODAFileConverter/ODAFileConverter")
        << QStringLiteral("/opt/ODA/ODAFileConverter/ODAFileConverter")
        << QStringLiteral("/opt/ODAFileConverter_QT5_lnxX64")
        << QStringLiteral("/opt/ODAFileConverter_QT6_lnxX64");

    QStringList unique;
    for (const QString& item : out) {
        const QString p = cleanPath(item);
        if (!p.isEmpty() && !unique.contains(p))
            unique << p;
    }
    return unique;
}

QString suffixForType(const QString& outputType)
{
    return outputType.compare(QStringLiteral("DWG"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("dwg")
        : QStringLiteral("dxf");
}

QString makeSafeBaseName(const QString& filePath)
{
    QString base = QFileInfo(filePath).completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("drawing");
    return base;
}

} // namespace

QString OdaConversionResult::summary() const
{
    QString s;
    if (ok) {
        s = QStringLiteral("ODA File Converter OK: %1 -> %2")
                .arg(QFileInfo(inputFile).fileName(), QFileInfo(outputFile).fileName());
    } else {
        s = QStringLiteral("ODA File Converter failed: %1").arg(error);
    }
    if (!converterPath.isEmpty())
        s += QStringLiteral("\nExecutable: %1").arg(converterPath);
    if (!commandLine.isEmpty())
        s += QStringLiteral("\nCommand: %1").arg(commandLine);
    if (!stdErr.trimmed().isEmpty())
        s += QStringLiteral("\nStderr: %1").arg(stdErr.trimmed());
    if (!stdOut.trimmed().isEmpty())
        s += QStringLiteral("\nStdout: %1").arg(stdOut.trimmed());
    return s;
}

QString OdaFileConverter::findExecutable()
{
    for (const QString& path : candidateExecutables()) {
        QFileInfo info(path);
        if (info.exists() && info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return QString();
}

bool OdaFileConverter::isAvailable(QString* resolvedPath)
{
    const QString exe = findExecutable();
    if (resolvedPath)
        *resolvedPath = exe;
    return !exe.isEmpty();
}

OdaConversionResult OdaFileConverter::convertDwgToDxf(const QString& dwgPath,
                                                       const QString& outputDxfPath,
                                                       const QString& targetVersion)
{
    return convertDwgToDxf(dwgPath, outputDxfPath, targetVersion, {});
}

OdaConversionResult OdaFileConverter::convertDwgToDxf(const QString& dwgPath,
                                                       const QString& outputDxfPath,
                                                       const QString& targetVersion,
                                                       const std::function<void(int, const QString&)>& progressCallback)
{
    QString out = outputDxfPath;
    if (out.isEmpty()) {
        const QString dir = QDir::tempPath() + QStringLiteral("/DWGViewer_ODA_") + QUuid::createUuid().toString(QUuid::Id128);
        QDir().mkpath(dir);
        out = dir + QLatin1Char('/') + makeSafeBaseName(dwgPath) + QStringLiteral(".dxf");
    }
    return convertFile(dwgPath, out, QStringLiteral("DXF"), targetVersion, progressCallback);
}

OdaConversionResult OdaFileConverter::convertDxfToDwg(const QString& dxfPath,
                                                       const QString& outputDwgPath,
                                                       const QString& targetVersion)
{
    return convertFile(dxfPath, outputDwgPath, QStringLiteral("DWG"), targetVersion);
}

OdaConversionResult OdaFileConverter::convertFile(const QString& inputPath,
                                                   const QString& outputPath,
                                                   const QString& outputType,
                                                   const QString& targetVersion,
                                                   const std::function<void(int, const QString&)>& progressCallback)
{
    auto progress = [&](int value, const QString& text) {
        if (progressCallback)
            progressCallback(qBound(0, value, 100), text);
    };
    OdaConversionResult result;
    result.inputFile = inputPath;
    result.outputFile = outputPath;

    progress(2, QStringLiteral("Searching for the ODA File Converter executable..."));
    const QString exe = findExecutable();
    result.converterPath = exe;
    if (exe.isEmpty()) {
        result.error = QStringLiteral("ODA File Converter not found. Set DWGVIEWER_ODA_CONVERTER_PATH to the ODAFileConverter executable.");
        progress(100, QStringLiteral("ODA not found."));
        return result;
    }

    QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists()) {
        result.error = QStringLiteral("Source file not found: %1").arg(inputPath);
        return result;
    }
    if (outputPath.isEmpty()) {
        result.error = QStringLiteral("Output path is empty.");
        return result;
    }

    progress(8, QStringLiteral("Preparing ODA temporary folders..."));
    QTemporaryDir tempDir(QDir::tempPath() + QStringLiteral("/DWGViewer_ODA_run_XXXXXX"));
    if (!tempDir.isValid()) {
        result.error = QStringLiteral("Cannot create the ODA temporary folder.");
        return result;
    }

    const QString inputDir = tempDir.path() + QStringLiteral("/in");
    const QString outputDir = tempDir.path() + QStringLiteral("/out");
    QDir().mkpath(inputDir);
    QDir().mkpath(outputDir);

    const QString inputSuffix = inputInfo.suffix().toLower();
    const QString safeInput = inputDir + QLatin1Char('/') + makeSafeBaseName(inputPath) + QLatin1Char('.') + inputSuffix;
    progress(14, QStringLiteral("Copying DWG to the ODA temporary folder..."));
    if (!QFile::copy(inputPath, safeInput)) {
        result.error = QStringLiteral("Cannot copy the source file to the ODA temporary folder.");
        progress(100, QStringLiteral("ODA preparation failed."));
        return result;
    }

    const QString version = targetVersion.isEmpty() ? QStringLiteral("ACAD2018") : targetVersion;
    const QString filter = QStringLiteral("*.%1").arg(inputSuffix.toUpper());

    QStringList args;
    args << inputDir
         << outputDir
         << version
         << outputType.toUpper()
         << QStringLiteral("0")   // non-récursif
         << QStringLiteral("1")   // audit enabled
         << filter;

    QStringList printable;
    printable << QDir::toNativeSeparators(exe);
    for (const QString& a : args)
        printable << QStringLiteral("\"") + a + QStringLiteral("\"");
    result.commandLine = printable.join(QLatin1Char(' '));

    progress(22, QStringLiteral("Launching ODA File Converter in the background..."));
    QProcess process;
    process.setProgram(exe);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(10000)) {
        result.error = QStringLiteral("Cannot launch ODA File Converter.");
        progress(100, QStringLiteral("ODA launch failed."));
        return result;
    }

    QElapsedTimer odaTimer;
    odaTimer.start();
    int lastPulse = 22;
    while (!process.waitForFinished(250)) {
        if (odaTimer.elapsed() > 10 * 60 * 1000) {
            process.kill();
            process.waitForFinished(3000);
            result.error = QStringLiteral("ODA File Converter exceeded the 10-minute timeout.");
            result.stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
            result.stdErr = QString::fromLocal8Bit(process.readAllStandardError());
            progress(100, QStringLiteral("Failed: ODA timeout exceeded."));
            return result;
        }
        // ODA CLI ne donne pas toujours une progression exploitable; on pulse
        // donc prudemment jusqu'à 82% pendant la conversion réelle.
        if (lastPulse < 82)
            ++lastPulse;
        progress(lastPulse, QStringLiteral("ODA is converting the file in the background... %1 s").arg(odaTimer.elapsed() / 1000));
        QThread::msleep(25);
    }
    progress(86, QStringLiteral("ODA finished, checking generated file..."));

    result.exitCode = process.exitCode();
    result.stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    result.stdErr = QString::fromLocal8Bit(process.readAllStandardError());

    const QString outSuffix = suffixForType(outputType);
    QString produced = outputDir + QLatin1Char('/') + makeSafeBaseName(inputPath) + QLatin1Char('.') + outSuffix;
    if (!QFileInfo::exists(produced)) {
        const QFileInfoList files = QDir(outputDir).entryInfoList(QStringList()
            << QStringLiteral("*.%1").arg(outSuffix)
            << QStringLiteral("*.%1").arg(outSuffix.toUpper()),
            QDir::Files);
        if (!files.isEmpty())
            produced = files.first().absoluteFilePath();
    }

    if (!QFileInfo::exists(produced)) {
        result.error = QStringLiteral("ODA did not produce a .%1 file. Code=%2").arg(outSuffix).arg(result.exitCode);
        progress(100, QStringLiteral("Failed: ODA did not produce the expected file."));
        return result;
    }

    progress(92, QStringLiteral("Copying ODA result to the final temporary file..."));
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    if (QFileInfo::exists(outputPath) && !QFile::remove(outputPath)) {
        result.error = QStringLiteral("Cannot replace output file: %1").arg(outputPath);
        return result;
    }
    if (!QFile::copy(produced, outputPath)) {
        result.error = QStringLiteral("Cannot copy ODA result to: %1").arg(outputPath);
        return result;
    }

    result.ok = true;
    progress(100, QStringLiteral("ODA conversion completed."));
    return result;
}
