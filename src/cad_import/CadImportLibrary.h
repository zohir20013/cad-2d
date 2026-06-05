#pragma once

#include "../cad/LayerInfo.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

class CadDocument;

// Options communes d'import CAD. Le viewer ne doit plus connaitre les details
// internes DWG/DXF; il transmet seulement ces options a la bibliotheque.
struct CadImportOptions
{
    bool expandBlocks = true;
    bool loadModelSpace = true;
    bool loadPaperSpace = false;
    bool approximateCurves = true;
    bool largeFileMode = true;
    int maxSplineSegments = 256;
    int maxBlockDepth = 12;
    bool preferOdaForDwg = true;          // DWG -> DXF temporaire via ODA File Converter, puis DxfLibrary.
    QString odaConverterPath;             // Optionnel; sinon env DWGVIEWER_ODA_CONVERTER_PATH / PATH.
    QString odaTargetVersion = QStringLiteral("ACAD2018");
    std::function<void(int, const QString&)> progressCallback;
};

enum class CadFileFormat
{
    Unknown,
    DWG,
    DXF
};

struct CadImportReport
{
    CadFileFormat format = CadFileFormat::Unknown;
    QString backend;
    QString filePath;
    QString message;
    QStringList warnings;
    bool largeFileMode = false;
    int entityCount = 0;
    int layerCount = 0;
    qint64 elapsedMs = 0;

    QString formatName() const;
    QString summary() const;
};

// Bibliotheque commune d'import CAD.
// .dxf -> DWGView native DXF uniquement
// .dwg -> ODA File Converter en DXF temporaire, puis DWGView native DXF uniquement
class CadImportLibrary
{
public:
    static CadFileFormat detectFormat(const QString& filePath);

    static bool loadEditable(const QString& filePath,
                             CadDocument& document,
                             QMap<QString, LayerInfo>& layers,
                             QString* errorMessage = nullptr,
                             CadImportReport* report = nullptr,
                             const CadImportOptions& options = CadImportOptions());
};
