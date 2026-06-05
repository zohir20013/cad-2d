#pragma once

#include "../cad/LayerInfo.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

class CadDocument;

struct DxfLoadOptions
{
    // Native mode now preserves BLOCK definitions and INSERT references by default,
    // so the Block menu can manage imported DXF blocks. Set forceExplodeBlocks to
    // true when a fully editable, exploded drawing is preferred.
    bool preserveBlocks = true;
    bool forceExplodeBlocks = false;
    bool loadModelSpace = true;
    bool loadPaperSpace = false;
    bool keepUnsupportedAsFallbackGeometry = true;
    int maxBlockPreviewEntities = 2048;
};

struct DxfExportOptions
{
    QString acadVersion = QStringLiteral("AC1015"); // AutoCAD 2000 DXF, widely compatible.
    bool exportBlockDefinitions = true;
    bool exportTrueColor = true;
    bool exportFallbackAci = true;
    bool exportTextStyles = true;
};

struct DxfScanInfo
{
    QString filePath;
    QString acadVersion;
    bool isBinary = false;
    bool hasHeader = false;
    bool hasTables = false;
    bool hasBlocks = false;
    bool hasEntities = false;
    bool hasObjects = false;
    int insUnits = 0;
    int layerCount = 0;
    int lineTypeCount = 0;
    int textStyleCount = 0;
    int blockDefinitionCount = 0;
    int entityCount = 0;
    int insertReferenceCount = 0;
    int objectCount = 0;
    QStringList sections;

    QString summary() const;
};

struct DxfLibraryReport
{
    bool ok = false;
    QString backend = QStringLiteral("DWGView native DXF");
    QString filePath;
    QString message;
    QString acadVersion;
    bool binaryDxf = false;
    int insUnits = 0;
    int entityCount = 0;
    int layerCount = 0;
    int imported = 0;
    int ignored = 0;
    int fallback = 0;
    int unsupported = 0;
    int invalidGeometry = 0;
    int blockDefinitionCount = 0;
    int insertReferenceCount = 0;
    int preservedBlockReferenceCount = 0;
    int expandedBlockReferenceCount = 0;
    int virtualBlockEntityEstimate = 0;
    int objectCount = 0;
    int layoutCount = 0;
    int xrefBlockCount = 0;
    int xdataAppCount = 0;
    int xdataPairCount = 0;
    bool paperSpaceSeen = false;
    QHash<QString, int> importedByType;
    QHash<QString, int> unsupportedByType;
    QStringList warnings;

    QString summary() const;
};

// Facade DXF native DWGView.
// Lecture: parseur DXF interne du projet, sans backend DXF externe.
// Ecriture: writer DXF interne dans CadDocument::saveAsDxf, sans backend DXF externe.
class DxfLibrary
{
public:
    static bool scanFile(const QString& filePath,
                         DxfScanInfo* info,
                         QString* errorMessage = nullptr);

    static bool loadEditable(const QString& filePath,
                             CadDocument& document,
                             QMap<QString, LayerInfo>& layers,
                             QString* errorMessage = nullptr);

    static bool loadEditableWithReport(const QString& filePath,
                                       CadDocument& document,
                                       QMap<QString, LayerInfo>& layers,
                                       DxfLibraryReport* report = nullptr,
                                       QString* errorMessage = nullptr,
                                       const DxfLoadOptions& options = DxfLoadOptions());

    static bool saveEditable(const QString& filePath,
                             const CadDocument& document,
                             DxfLibraryReport* report = nullptr,
                             QString* errorMessage = nullptr,
                             const DxfExportOptions& options = DxfExportOptions());
};
