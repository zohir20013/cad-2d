#include "CadImportLibrary.h"

#include "../dxf_library/DxfLibrary.h"
#include "../cad/CadDocument.h"
#include "OdaFileConverter.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QCoreApplication>

QString CadImportReport::formatName() const
{
    switch (format) {
    case CadFileFormat::DWG: return QStringLiteral("DWG");
    case CadFileFormat::DXF: return QStringLiteral("DXF");
    default: return QStringLiteral("Unknown");
    }
}

QString CadImportReport::summary() const
{
    QString text = QStringLiteral("%1 imported by %2: %3 entities, %4 layers, %5 ms")
        .arg(formatName(), backend)
        .arg(entityCount)
        .arg(layerCount)
        .arg(elapsedMs);
    if (largeFileMode)
        text += QStringLiteral(" — large-file mode");
    if (!message.trimmed().isEmpty())
        text += QStringLiteral("\n") + message.trimmed();
    if (!warnings.isEmpty())
        text += QStringLiteral("\nWarnings: ") + warnings.join(QStringLiteral(" | "));
    return text;
}

CadFileFormat CadImportLibrary::detectFormat(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("dwg")) return CadFileFormat::DWG;
    if (suffix == QStringLiteral("dxf")) return CadFileFormat::DXF;
    return CadFileFormat::Unknown;
}

bool CadImportLibrary::loadEditable(const QString& filePath,
                                    CadDocument& document,
                                    QMap<QString, LayerInfo>& layers,
                                    QString* errorMessage,
                                    CadImportReport* report,
                                    const CadImportOptions& options)
{
    auto progress = [&](int value, const QString& text) {
        if (options.progressCallback)
            options.progressCallback(qBound(0, value, 100), text);
    };

    QElapsedTimer timer;
    timer.start();

    CadImportReport localReport;
    localReport.filePath = filePath;
    localReport.format = detectFormat(filePath);
    localReport.largeFileMode = options.largeFileMode;

    QString backendMessage;
    bool ok = false;
    DxfLibraryReport nativeDxfReport;

    progress(1, QStringLiteral("Analyzing file type..."));

    switch (localReport.format) {
    case CadFileFormat::DXF:
        localReport.backend = QStringLiteral("DWGView native DXF");
        progress(12, QStringLiteral("Reading DXF with the DWGView native parser..."));
        ok = DxfLibrary::loadEditableWithReport(filePath, document, layers, &nativeDxfReport, &backendMessage);
        progress(ok ? 92 : 100, ok
            ? QStringLiteral("DXF read with the DWGView native parser, finalizing drawing...")
            : QStringLiteral("Failed to read DXF with the DWGView native parser."));
        break;

    case CadFileFormat::DWG: {
        if (!options.odaConverterPath.trimmed().isEmpty())
            qputenv("DWGVIEWER_ODA_CONVERTER_PATH", options.odaConverterPath.toLocal8Bit());

        localReport.backend = QStringLiteral("ODA File Converter -> DWGView native DXF parser");
        progress(8, QStringLiteral("Searching for ODA File Converter..."));
        QString odaPath;
        if (!OdaFileConverter::isAvailable(&odaPath)) {
            backendMessage = QStringLiteral("ODA File Converter not found. Set DWGVIEWER_ODA_CONVERTER_PATH to the ODAFileConverter executable.");
            ok = false;
            progress(100, QStringLiteral("Failed: ODA File Converter not found."));
            break;
        }

        progress(18, QStringLiteral("ODA found. Preparing DWG -> DXF conversion..."));
        const OdaConversionResult oda = OdaFileConverter::convertDwgToDxf(
            filePath,
            QString(),
            options.odaTargetVersion,
            [&](int value, const QString& text) {
                // Mappe la progression interne ODA dans la plage 18..70.
                const int mapped = 18 + qBound(0, value, 100) * 52 / 100;
                progress(mapped, text);
            });

        if (!oda.ok) {
            backendMessage = oda.summary();
            localReport.warnings << oda.summary();
            ok = false;
            progress(100, QStringLiteral("ODA failed: DWG -> DXF conversion impossible."));
            break;
        }

        localReport.warnings << QStringLiteral("DWG converted by ODA to temporary DXF: %1").arg(oda.outputFile);
        progress(72, QStringLiteral("ODA conversion completed. Reading the temporary DXF with the DWGView native parser..."));
        ok = DxfLibrary::loadEditableWithReport(oda.outputFile, document, layers, &nativeDxfReport, &backendMessage);
        if (!ok) {
            localReport.warnings << QStringLiteral("ODA produced the DXF, but the DWGView native parser could not read it: %1").arg(backendMessage);
            progress(100, QStringLiteral("Failed: the DWGView native parser could not read the DXF produced by ODA."));
        } else {
            progress(92, QStringLiteral("DXF produced by ODA read with the DWGView native parser. Endalizing..."));
        }
        break;
    }

    default:
        backendMessage = QStringLiteral("Unsupported extension. Use a .dwg or .dxf file.");
        ok = false;
        progress(100, QStringLiteral("Failed: unsupported extension."));
        break;
    }

    localReport.elapsedMs = timer.elapsed();
    localReport.entityCount = document.entityCount();
    localReport.layerCount = layers.size();
    localReport.message = backendMessage;

    if (ok && localReport.format == CadFileFormat::DWG)
        localReport.warnings << QStringLiteral("DWG loaded via ODA File Converter -> temporary DXF -> DWGView native parser. No internal fallback was used.");
    if (ok && localReport.format == CadFileFormat::DXF)
        localReport.warnings << QStringLiteral("DXF loaded with the DWGView native parser. The native DXF parser is used.");
    if (ok && nativeDxfReport.blockDefinitionCount > 0)
        localReport.warnings << QStringLiteral("Native DXF blocks preserved: %1 block definition(s), %2 INSERT reference(s).").arg(nativeDxfReport.blockDefinitionCount).arg(nativeDxfReport.preservedBlockReferenceCount);
    if (ok && nativeDxfReport.fallback > 0)
        localReport.warnings << QStringLiteral("Native DXF fallback geometry used for %1 object(s).").arg(nativeDxfReport.fallback);
    if (ok && localReport.entityCount > 10000) {
        localReport.largeFileMode = true;
        localReport.warnings << QStringLiteral("Large file detected: simplified rendering is recommended during navigation.");
    }

    progress(ok ? 100 : 100, ok
        ? QStringLiteral("Open completed.")
        : QStringLiteral("Open failed."));

    if (report)
        *report = localReport;

    if (errorMessage) {
        if (ok)
            *errorMessage = localReport.summary();
        else
            *errorMessage = backendMessage.isEmpty() ? localReport.summary() : backendMessage;
    }

    return ok;
}
