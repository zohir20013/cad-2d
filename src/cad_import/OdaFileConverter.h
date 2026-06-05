#pragma once

#include <QString>
#include <QStringList>
#include <functional>

struct OdaConversionResult
{
    bool ok = false;
    QString converterPath;
    QString inputFile;
    QString outputFile;
    QString commandLine;
    QString stdOut;
    QString stdErr;
    QString error;
    int exitCode = -1;

    QString summary() const;
};

class OdaFileConverter
{
public:
    static QString findExecutable();
    static bool isAvailable(QString* resolvedPath = nullptr);

    static OdaConversionResult convertDwgToDxf(const QString& dwgPath,
                                               const QString& outputDxfPath = QString(),
                                               const QString& targetVersion = QStringLiteral("ACAD2018"));

    static OdaConversionResult convertDwgToDxf(const QString& dwgPath,
                                               const QString& outputDxfPath,
                                               const QString& targetVersion,
                                               const std::function<void(int, const QString&)>& progressCallback);

    static OdaConversionResult convertDxfToDwg(const QString& dxfPath,
                                               const QString& outputDwgPath,
                                               const QString& targetVersion = QStringLiteral("ACAD2013"));

private:
    static OdaConversionResult convertFile(const QString& inputPath,
                                           const QString& outputPath,
                                           const QString& outputType,
                                           const QString& targetVersion,
                                           const std::function<void(int, const QString&)>& progressCallback = {});
};
