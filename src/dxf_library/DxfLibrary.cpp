#include "DxfLibrary.h"
#include "../cad/CadDocument.h"
#include "../cad/CadEntity.h"
#include "../cad/CadLayer.h"
#include "../DebugLogger.h"

#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QHash>
#include <QJsonArray>
#include <QLineF>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QRegularExpression>
#include <QSet>
#include <QMultiHash>
#include <QString>
#include <QStringList>
#include <QStringConverter>
#include <QTextStream>
#include <QVector>
#include <QVector3D>
#include <QtGlobal>

#include <algorithm>
#include <limits>
#include <cmath>
#include <functional>
#include <cstring>
#include <memory>
#include <vector>
#include <utility>

namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr int kMaxBlockPreviewEntities = 256;
constexpr int kMaxBlockScannedEntities = 250000;
constexpr int kMaxBlockExpandDepth = 16;
constexpr int kMaxExpandedBlockEntities = 2000000;
constexpr int kVirtualBlockEntityThreshold = 250000;
constexpr int kVirtualBlockPerInsertThreshold = 12000;
constexpr int kMaxVirtualBlockPreviewEntities = 2048;
constexpr int kDxfHugeVirtualBlockThreshold = 750000;
constexpr int kDxfMaxNestedVirtualDepth = 24;
constexpr int kSplineMaxSamples = 96;
constexpr int kBulgeMaxSegments = 32;

struct DxfPair {
    int code = -9999;
    QString value;
};

class DxfPairReader
{
public:
    explicit DxfPairReader(const QString& path) : m_file(path), m_stream(&m_file) {}

    bool open(QString* error)
    {
        if (!m_file.open(QIODevice::ReadOnly)) {
            if (error) *error = QStringLiteral("Cannot open DXF file: %1").arg(m_file.errorString());
            return false;
        }

        const QByteArray head = m_file.peek(32);
        if (head.startsWith("AutoCAD Binary DXF")) {
            m_binary = true;
            m_binaryData = m_file.readAll();
            // Binary DXF sentinel: "AutoCAD Binary DXF\r\n\x1A\0".
            const QByteArray sentinel("AutoCAD Binary DXF\r\n\x1A\0", 22);
            m_binaryPos = m_binaryData.startsWith(sentinel) ? sentinel.size() : qMin<qsizetype>(22, m_binaryData.size());
            m_pairNumber = 0;
            return true;
        }

        m_file.close();
        if (!m_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) *error = QStringLiteral("Cannot open DXF file: %1").arg(m_file.errorString());
            return false;
        }
        // DXF ASCII moderne est souvent UTF-8, les anciens DXF ANSI restent lisibles
        // pour les codes numeriques et les textes simples.
        m_stream.setEncoding(QStringConverter::Utf8);
        return true;
    }

    bool next(DxfPair& p)
    {
        if (!m_unread.empty()) {
            p = m_unread.back();
            m_unread.pop_back();
            return true;
        }

        if (m_binary) return nextBinary(p);

        QString codeLine;
        while (!m_stream.atEnd()) {
            codeLine = m_stream.readLine();
            ++m_lineNumber;
            if (!codeLine.trimmed().isEmpty()) break;
        }
        if (codeLine.trimmed().isEmpty() && m_stream.atEnd()) return false;
        if (m_stream.atEnd()) return false;

        QString valueLine = m_stream.readLine();
        ++m_lineNumber;
        bool ok = false;
        const int code = codeLine.trimmed().toInt(&ok);
        p.code = ok ? code : -9999;
        p.value = valueLine.trimmed();
        return ok;
    }

    void unread(const DxfPair& p) { m_unread.push_back(p); }
    qint64 lineNumber() const { return m_binary ? m_pairNumber : m_lineNumber; }
    bool isBinary() const { return m_binary; }

private:
    enum class BinaryValueKind { String, Double, Int16, Int32, Int64, Bool, BinaryChunk };

    static quint16 u16le(const QByteArray& a, qsizetype pos)
    {
        return quint16(quint8(a.at(pos))) | (quint16(quint8(a.at(pos + 1))) << 8);
    }

    static quint32 u32le(const QByteArray& a, qsizetype pos)
    {
        return quint32(quint8(a.at(pos))) |
               (quint32(quint8(a.at(pos + 1))) << 8) |
               (quint32(quint8(a.at(pos + 2))) << 16) |
               (quint32(quint8(a.at(pos + 3))) << 24);
    }

    static quint64 u64le(const QByteArray& a, qsizetype pos)
    {
        quint64 v = 0;
        for (int i = 0; i < 8; ++i) v |= (quint64(quint8(a.at(pos + i))) << (8 * i));
        return v;
    }

    static BinaryValueKind kindForCode(int code)
    {
        if (code == 1004 || (code >= 310 && code <= 319)) return BinaryValueKind::BinaryChunk;
        if ((code >= 10 && code <= 59) ||
            (code >= 110 && code <= 149) ||
            (code >= 210 && code <= 239) ||
            (code >= 460 && code <= 469) ||
            (code >= 1010 && code <= 1059)) return BinaryValueKind::Double;
        if ((code >= 60 && code <= 79) ||
            (code >= 170 && code <= 179) ||
            (code >= 270 && code <= 289) ||
            (code >= 370 && code <= 389) ||
            (code >= 400 && code <= 409) ||
            (code >= 1060 && code <= 1070)) return BinaryValueKind::Int16;
        if ((code >= 90 && code <= 99) ||
            (code >= 420 && code <= 429) ||
            (code >= 440 && code <= 459) ||
            code == 1071) return BinaryValueKind::Int32;
        if (code >= 160 && code <= 169) return BinaryValueKind::Int64;
        if (code >= 290 && code <= 299) return BinaryValueKind::Bool;
        return BinaryValueKind::String;
    }

    bool readBinaryCode(int& code)
    {
        if (m_binaryPos + 2 > m_binaryData.size()) return false;
        code = qint16(u16le(m_binaryData, m_binaryPos));
        m_binaryPos += 2;
        return true;
    }

    QString readBinaryString()
    {
        const qsizetype start = m_binaryPos;
        while (m_binaryPos < m_binaryData.size() && m_binaryData.at(m_binaryPos) != '\0') ++m_binaryPos;
        const QByteArray raw = m_binaryData.mid(start, m_binaryPos - start);
        if (m_binaryPos < m_binaryData.size()) ++m_binaryPos;
        QString text = QString::fromUtf8(raw);
        if (text.contains(QChar::ReplacementCharacter)) text = QString::fromLatin1(raw);
        return text.trimmed();
    }

    QString readBinaryChunk()
    {
        if (m_binaryPos >= m_binaryData.size()) return QString();
        const int len = quint8(m_binaryData.at(m_binaryPos++));
        const int safeLen = qMin<int>(len, int(m_binaryData.size() - m_binaryPos));
        const QByteArray chunk = m_binaryData.mid(m_binaryPos, safeLen);
        m_binaryPos += safeLen;
        return QString::fromLatin1(chunk.toHex().toUpper());
    }

    bool nextBinary(DxfPair& p)
    {
        int code = -9999;
        if (!readBinaryCode(code)) return false;
        p.code = code;
        ++m_pairNumber;

        const BinaryValueKind kind = kindForCode(code);
        switch (kind) {
        case BinaryValueKind::Double: {
            if (m_binaryPos + 8 > m_binaryData.size()) return false;
            quint64 bits = u64le(m_binaryData, m_binaryPos);
            double v = 0.0;
            static_assert(sizeof(double) == sizeof(quint64));
            std::memcpy(&v, &bits, sizeof(double));
            m_binaryPos += 8;
            p.value = QString::number(v, 'g', 17);
            return true;
        }
        case BinaryValueKind::Int16: {
            if (m_binaryPos + 2 > m_binaryData.size()) return false;
            const qint16 v = qint16(u16le(m_binaryData, m_binaryPos));
            m_binaryPos += 2;
            p.value = QString::number(v);
            return true;
        }
        case BinaryValueKind::Int32: {
            if (m_binaryPos + 4 > m_binaryData.size()) return false;
            const qint32 v = qint32(u32le(m_binaryData, m_binaryPos));
            m_binaryPos += 4;
            p.value = QString::number(v);
            return true;
        }
        case BinaryValueKind::Int64: {
            if (m_binaryPos + 8 > m_binaryData.size()) return false;
            const qint64 v = qint64(u64le(m_binaryData, m_binaryPos));
            m_binaryPos += 8;
            p.value = QString::number(v);
            return true;
        }
        case BinaryValueKind::Bool: {
            if (m_binaryPos + 1 > m_binaryData.size()) return false;
            const int v = quint8(m_binaryData.at(m_binaryPos++));
            p.value = QString::number(v != 0 ? 1 : 0);
            return true;
        }
        case BinaryValueKind::BinaryChunk:
            p.value = readBinaryChunk();
            return true;
        case BinaryValueKind::String:
        default:
            p.value = readBinaryString();
            return true;
        }
    }

    QFile m_file;
    QTextStream m_stream;
    std::vector<DxfPair> m_unread;
    qint64 m_lineNumber = 0;
    bool m_binary = false;
    QByteArray m_binaryData;
    qsizetype m_binaryPos = 0;
    qint64 m_pairNumber = 0;
};

struct DxfHeader {
    QString acadVersion;
    QString handSeed;
    int insUnits = 0;
    int measurement = 0;
    int pdMode = 0;
    double pdSize = 0.0;
    QRectF extents;
    QPointF extMin;
    QPointF extMax;
    bool hasExtMin = false;
    bool hasExtMax = false;
};

struct DxfLineType {
    QString name = QStringLiteral("Continuous");
    QString description;
    QVector<double> pattern;
};

struct DxfTextStyle {
    QString name = QStringLiteral("Standard");
    QString font;
    double fixedHeight = 0.0;
    double widthFactor = 1.0;
    double obliqueAngleDeg = 0.0;
    int generationFlags = 0;
};

struct DxfDimStyle {
    QString name = QStringLiteral("Standard");
    double overallScale = 1.0;      // DIMSCALE / group 40
    double arrowSize = 2.5;         // DIMASZ / group 41
    double extensionOffset = 0.625; // DIMEXO / group 42
    double extensionExtend = 1.25;  // DIMEXE / group 44
    double textHeight = 2.5;        // DIMTXT / group 140
    double linearScaleFactor = 1.0; // DIMLFAC / group 144
    double textGap = 0.625;         // DIMGAP / group 147
    bool hasArrowSize = false;
    bool hasTextHeight = false;
};

struct DxfTables {
    QHash<QString, DxfLineType> lineTypes;
    QHash<QString, DxfTextStyle> textStyles;
    QHash<QString, DxfDimStyle> dimStyles;
    DxfDimStyle headerDimStyle;
    QSet<QString> appIds;
};

struct DxfRawEntity {
    QString type;
    QString handle;
    QString ownerHandle;
    QString materialHandle;
    QString layoutHandle;
    QString layoutName = QStringLiteral("Model");
    QString extensionDictionaryHandle;
    QString attribTag;
    QStringList subclassMarkers;
    QString layer = QStringLiteral("0");
    QString lineType = QStringLiteral("Continuous");
    QString text;
    QString blockName;
    QString dimStyleName = QStringLiteral("Standard");
    QString textStyle = QStringLiteral("Standard");
    QString hatchPattern = QStringLiteral("SOLID");
    int colorIndex = 256;
    int trueColor = -1;
    int lineWeight = -1;
    int transparency = -1;
    int visibility = 0;
    double lineTypeScale = 1.0;
    int flags = 0;
    int attrFollow = 0;
    int space = 0; // 0=model, 1=paper
    int vertexFlags = 0;
    int vIndex1 = 0;
    int vIndex2 = 0;
    int vIndex3 = 0;
    int vIndex4 = 0;
    int rowCount = 1;
    int colCount = 1;
    int vertexCount = 0;
    int hatchLoopCount = 0;
    int hatchStyle = 0;
    int hatchPathFlags = 0;
    double hatchScale = 1.0;
    double hatchAngle = 0.0;
    QPointF hatchOrigin;
    bool solidFill = false;
    bool associative = false;
    double rowSpacing = 0.0;
    double colSpacing = 0.0;
    double elevation = 0.0;
    bool hasElevation = false;
    double extrusionX = 0.0;
    double extrusionY = 0.0;
    double extrusionZ = 1.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double z1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double z2 = 0.0;
    double x3 = 0.0;
    double y3 = 0.0;
    double z3 = 0.0;
    double x4 = 0.0;
    double y4 = 0.0;
    double z4 = 0.0;
    double radius = 0.0;
    double height = 2.5;
    bool hasHeight = false;
    double rotation = 0.0;
    double textWidthFactor = 1.0;
    double textObliqueAngle = 0.0;
    int textHJustification = 0;
    int textVJustification = 0;
    int textGenerationFlags = 0;
    int mtextAttachmentPoint = 1;
    int mtextDrawingDirection = 1;
    int mtextLineSpacingStyle = 1;
    double mtextWidth = 0.0;
    double mtextLineSpacingFactor = 1.0;
    int splineDegree = 3;
    int splineKnotCount = 0;
    int splineControlCount = 0;
    int splineFitCount = 0;
    QVector<double> splineKnots;
    QVector<double> splineWeights;
    QVector<QPointF> splineFitPoints;
    double startAngle = 0.0;
    double endAngle = 360.0;
    double axisX = 0.0;
    double axisY = 0.0;
    double ratio = 1.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double scaleZ = 1.0;
    QVector<QPointF> points;
    QVector<double> bulges;
    QVector<QVector<QPointF>> loops;
    QStringList xdataAppIds;
    QStringList controlStrings;
    int xdataPairCount = 0;
    bool hasExtensionDictionary = false;
};

struct DxfBlockDef {
    QString name;
    QString handle;
    QString layer = QStringLiteral("0");
    QPointF basePoint;
    QJsonArray previewEntities;
    QVector<DxfRawEntity> rawEntities;
    int scannedEntities = 0;
    int flags = 0;
    bool anonymous = false;
    bool xref = false;
    QString ownerHandle;
};

double dxfY(double y);
QPointF dxfPoint(double x, double y);
QPointF dxfVector(double x, double y);
QString cleanDxfText(QString text)
{
    if (text.isEmpty()) return text;

    // Decode common AutoCAD TEXT/MTEXT/MLEADER control sequences while keeping
    // the visible engineering note content. This is deliberately conservative:
    // unsupported styling is removed, but paragraph breaks, Unicode and stacked
    // fractions are preserved as readable text.
    text.replace(QStringLiteral("\\P"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\\p"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\\~"), QStringLiteral(" "));
    text.replace(QStringLiteral("%%d"), QString::fromUtf8("°"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("%%p"), QString::fromUtf8("±"), Qt::CaseInsensitive);
    text.replace(QStringLiteral("%%c"), QString::fromUtf8("Ø"), Qt::CaseInsensitive);

    QRegularExpression unicodeToken(QStringLiteral("\\\\U\\+([0-9A-Fa-f]{4})"));
    QRegularExpressionMatchIterator it = unicodeToken.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok = false;
        const uint code = m.captured(1).toUInt(&ok, 16);
        if (ok) text.replace(m.captured(0), QString(QChar(code)));
    }

    // \S1^2; or \S1#2; stacked fraction -> 1/2.
    QRegularExpression stackedFractions(QStringLiteral("\\\\S([^;\\^#]+)[\\^#]([^;]+);"));
    text.replace(stackedFractions, QStringLiteral("\\1/\\2"));

    // Drop inline formatting such as \fArial|b0|i0;, \H0.7x;, \W1.0;, \C1;.
    // Keep the text after the command; remove only the command token.
    QRegularExpression semicolonCommand(QStringLiteral("\\\\[A-Za-z][^;\\n]*;"));
    text.remove(semicolonCommand);
    QRegularExpression shortCommand(QStringLiteral("\\\\[A-Za-z][0-9.+/-]*"));
    text.remove(shortCommand);

    text.remove(QChar('{'));
    text.remove(QChar('}'));
    text.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));

    QStringList lines;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        line = line.trimmed();
        lines << line;
    }
    while (!lines.isEmpty() && lines.first().isEmpty()) lines.removeFirst();
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

QVector<QPointF> approximateSplinePoints(const DxfRawEntity& raw);
QVector<QPointF> approximateEllipsePoints(const DxfRawEntity& raw, bool* fullEllipse = nullptr);

// ezdxf-like lightweight document model.  The viewer still receives a CadDocument,
// but this model preserves DXF concepts that a mature DXF stack needs: handles,
// owners, layouts, blocks, object records, xdata counters and block references.
// It is intentionally internal for now; later UI/export code can expose it without
// reparsing the file.
struct DxfAttribValue {
    QString tag;
    QString value;
    QString handle;
    QString layer;
    QPointF position;
    double height = 2.5;
    bool hasHeight = false;
    double rotation = 0.0;
};

struct DxfInsertReference {
    QString handle;
    QString blockName;
    QString ownerHandle;
    QString layoutName;
    QPointF insertPoint;
    double rotation = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    int rowCount = 1;
    int colCount = 1;
    double rowSpacing = 0.0;
    double colSpacing = 0.0;
    QVector<DxfAttribValue> attributes;
};

struct DxfLayoutBucket {
    QString name;
    QVector<DxfRawEntity> entities;
};

struct DxfObjectRecord {
    QString type;
    QString handle;
    QString ownerHandle;
    QString name;
    int pairCount = 0;
};

struct DxfDocumentModel {
    DxfHeader header;
    DxfTables tables;
    QHash<QString, DxfBlockDef> blocks;
    QHash<QString, DxfLayoutBucket> layouts;
    QHash<QString, DxfRawEntity> entityDb;
    QHash<QString, DxfObjectRecord> objects;
    QMultiHash<QString, QString> ownerToHandles;
    QVector<DxfInsertReference> insertRefs;
    QSet<QString> xdataApps;
    int anonymousBlocks = 0;
    int xrefBlocks = 0;
    int preservedXDataPairs = 0;
    int preservedControlStrings = 0;
    int extensionDictionaries = 0;

    void registerEntity(const DxfRawEntity& raw)
    {
        const QString layout = raw.layoutName.trimmed().isEmpty() ? QStringLiteral("Model") : raw.layoutName.trimmed();
        if (!layouts.contains(layout)) {
            DxfLayoutBucket bucket;
            bucket.name = layout;
            layouts.insert(layout, bucket);
        }
        layouts[layout].entities.append(raw);
        if (!raw.handle.trimmed().isEmpty()) entityDb.insert(raw.handle.trimmed().toUpper(), raw);
        if (!raw.ownerHandle.trimmed().isEmpty() && !raw.handle.trimmed().isEmpty())
            ownerToHandles.insert(raw.ownerHandle.trimmed().toUpper(), raw.handle.trimmed().toUpper());
        for (const QString& app : raw.xdataAppIds) xdataApps.insert(app.trimmed().toUpper());
        preservedXDataPairs += raw.xdataPairCount;
        preservedControlStrings += raw.controlStrings.size();
        if (raw.hasExtensionDictionary) ++extensionDictionaries;
    }

    void registerBlock(const DxfBlockDef& block)
    {
        blocks.insert(block.name, block);
        if (block.anonymous) ++anonymousBlocks;
        if (block.xref) ++xrefBlocks;
    }

    void registerInsert(const DxfRawEntity& raw, const QVector<DxfRawEntity>& attrs)
    {
        DxfInsertReference ref;
        ref.handle = raw.handle;
        ref.blockName = raw.blockName;
        ref.ownerHandle = raw.ownerHandle;
        ref.layoutName = raw.layoutName;
        ref.insertPoint = dxfPoint(raw.x1, raw.y1);
        ref.rotation = raw.rotation;
        ref.scaleX = raw.scaleX;
        ref.scaleY = raw.scaleY;
        ref.rowCount = raw.rowCount;
        ref.colCount = raw.colCount;
        ref.rowSpacing = raw.rowSpacing;
        ref.colSpacing = raw.colSpacing;
        for (const DxfRawEntity& attr : attrs) {
            DxfAttribValue av;
            av.tag = attr.attribTag;
            av.value = cleanDxfText(attr.text);
            av.handle = attr.handle;
            av.layer = attr.layer;
            av.position = dxfPoint(attr.x1, attr.y1);
            av.height = attr.height;
            av.rotation = attr.rotation;
            ref.attributes.append(av);
        }
        insertRefs.append(ref);
    }
};

QString blockLookupKey(const QString& name, const QHash<QString, DxfBlockDef>& blocks)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return QString();
    if (blocks.contains(trimmed)) return trimmed;
    for (auto it = blocks.constBegin(); it != blocks.constEnd(); ++it) {
        if (it.key().compare(trimmed, Qt::CaseInsensitive) == 0) return it.key();
    }
    return QString();
}

bool isAnonymousOrDynamicBlockName(const QString& name)
{
    const QString n = name.trimmed();
    return n.startsWith(QLatin1Char('*')) || n.startsWith(QStringLiteral("`*"));
}

struct DxfStats {
    int imported = 0;
    int ignored = 0;
    int fallback = 0;
    int unsupported = 0;
    int blocks = 0;
    int classes = 0;
    int blockRecords = 0;
    int objects = 0;
    int images = 0;
    int expandedBlockRefs = 0;
    int expandedBlockEntities = 0;
    int blockDepthLimitHits = 0;
    int invalidGeometry = 0;
    int attachedAttributes = 0;
    int hatchLoops = 0;
    int hatchInnerLoops = 0;
    int entityDbHandles = 0;
    int layouts = 0;
    int insertRefs = 0;
    int anonymousBlocks = 0;
    int xrefBlocks = 0;
    int xdataApps = 0;
    int xdataPairs = 0;
    int extensionDictionaries = 0;
    int virtualBlockRefs = 0;
    int virtualBlockEntities = 0;
    int ocsTransformedEntities = 0;
    int exactHandleExportReady = 0;
    bool paperSpaceSeen = false;
    bool objectsSeen = false;
    QHash<QString, int> importedByType;
    QHash<QString, int> unsupportedByType;
};

bool isBinaryDxfFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.peek(32);
    return head.startsWith("AutoCAD Binary DXF");
}

QColor aciColor(int index)
{
    switch (std::abs(index)) {
    case 1:  return QColor(255, 0, 0);
    case 2:  return QColor(255, 255, 0);
    case 3:  return QColor(0, 255, 0);
    case 4:  return QColor(0, 255, 255);
    case 5:  return QColor(0, 0, 255);
    case 6:  return QColor(255, 0, 255);
    case 7:  return QColor(255, 255, 255);
    case 8:  return QColor(128, 128, 128);
    case 9:  return QColor(192, 192, 192);
    case 10: return QColor(255, 0, 0);
    case 30: return QColor(255, 127, 0);
    case 50: return QColor(255, 255, 0);
    case 90: return QColor(0, 255, 0);
    case 130:return QColor(0, 255, 255);
    case 170:return QColor(0, 0, 255);
    case 210:return QColor(255, 0, 255);
    default: return QColor(255, 255, 255);
    }
}

QString cleanLayerName(const QString& name)
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("0") : trimmed;
}


QString joinTopTypes(const QHash<QString, int>& values, int limit = 10)
{
    QVector<QPair<QString, int>> pairs;
    pairs.reserve(values.size());
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) pairs.append(qMakePair(it.key(), it.value()));
    std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    QStringList out;
    for (int i = 0; i < pairs.size() && i < limit; ++i) out << QStringLiteral("%1=%2").arg(pairs.at(i).first).arg(pairs.at(i).second);
    return out.join(QStringLiteral(", "));
}

bool isModernDxfVersion(const QString& acadVersion)
{
    // AutoCAD 2010/2011/2012 DXF uses AC1024. Older common values: AC1009=R12, AC1015=2000.
    return acadVersion.compare(QStringLiteral("AC1024"), Qt::CaseInsensitive) == 0 ||
           acadVersion.compare(QStringLiteral("AC1027"), Qt::CaseInsensitive) == 0 ||
           acadVersion.compare(QStringLiteral("AC1032"), Qt::CaseInsensitive) == 0;
}

double dxfY(double y)
{
    // DXF/CAD coordinates are Y-up, while QGraphicsScene is Y-down.
    // Convert every imported DXF point to the internal scene coordinate system here.
    return -y;
}

QPointF dxfPoint(double x, double y)
{
    return QPointF(x, dxfY(y));
}

QPointF dxfVector(double x, double y)
{
    return QPointF(x, dxfY(y));
}

QPointF cadPolarPoint(const QPointF& center, double radius, double angleDeg)
{
    const double a = angleDeg * kPi / 180.0;
    return QPointF(center.x() + radius * std::cos(a),
                   center.y() - radius * std::sin(a));
}


double normalizePositiveSpan(double spanDeg)
{
    while (spanDeg < 0.0) spanDeg += 360.0;
    while (spanDeg >= 360.0 && spanDeg - 360.0 > 1e-9) spanDeg -= 360.0;
    return std::abs(spanDeg) < 1e-9 ? 360.0 : spanDeg;
}

QPointF rotateOffset(double dx, double dy, double angleDeg)
{
    const double a = angleDeg * kPi / 180.0;
    const double ca = std::cos(a);
    const double sa = std::sin(a);
    return QPointF(dx * ca - dy * sa, dx * sa + dy * ca);
}

double cadAngleDeg(const QPointF& a, const QPointF& b)
{
    return std::atan2(b.y() - a.y(), b.x() - a.x()) * 180.0 / kPi;
}

double normalizedTextHeight(double height, double fallback = 2.5)
{
    return (qIsFinite(height) && height > 1.0e-9) ? height : fallback;
}

double normalizedWidthFactor(double widthFactor)
{
    return (qIsFinite(widthFactor) && widthFactor > 1.0e-9) ? qBound(0.01, widthFactor, 100.0) : 1.0;
}


bool isDefaultTextWidthFactor(double widthFactor)
{
    return std::abs(widthFactor - 1.0) <= 1.0e-9;
}

const DxfTextStyle* lookupTextStyle(const DxfTables* tables, const QString& name)
{
    if (!tables) return nullptr;
    const QString key = name.trimmed().isEmpty() ? QStringLiteral("STANDARD") : name.trimmed().toUpper();
    auto it = tables->textStyles.constFind(key);
    if (it != tables->textStyles.constEnd()) return &(*it);
    return nullptr;
}

QString resolvedTextFontName(const DxfTables* tables, const QString& styleName)
{
    const DxfTextStyle* style = lookupTextStyle(tables, styleName);
    if (style && !style->font.trimmed().isEmpty()) return style->font.trimmed();
    return styleName.trimmed().isEmpty() ? QStringLiteral("TXT") : styleName.trimmed();
}

double estimatedTextWidth(const QString& text, double height, double widthFactor)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    int maxChars = 1;
    for (const QString& line : lines) maxChars = std::max(maxChars, static_cast<int>(line.size()));
    return std::max(height * 0.25, maxChars * height * 0.62 * widthFactor);
}

double estimatedTextBlockHeight(const QString& text, double height, double lineSpacing = 1.0)
{
    const int lines = std::max(1, static_cast<int>(text.count(QLatin1Char('\n')) + 1));
    return height * (1.0 + (lines - 1) * 1.25 * qBound(0.25, lineSpacing, 4.0));
}

QPointF lowerLeftFromTextAnchor(const QPointF& anchor,
                               const QString& text,
                               double height,
                               double widthFactor,
                               double angleDeg,
                               int hJust,
                               int vJust)
{
    const double w = estimatedTextWidth(text, height, widthFactor);
    const double h = estimatedTextBlockHeight(text, height);
    double dx = 0.0;
    double dy = 0.0;

    switch (hJust) {
    case 1: dx = -w * 0.5; break;       // center
    case 2: dx = -w; break;             // right
    case 4: dx = -w * 0.5; break;       // middle
    default: dx = 0.0; break;           // left/aligned/fit start
    }

    switch (vJust) {
    case 1: dy = 0.0; break;            // bottom
    case 2: dy = -h * 0.5; break;       // middle
    case 3: dy = -h; break;             // top
    default: dy = 0.0; break;           // baseline
    }

    return anchor + rotateOffset(dx, dy, angleDeg);
}

QPointF lowerLeftFromMTextAnchor(const QPointF& anchor,
                                const QString& text,
                                double height,
                                double width,
                                double angleDeg,
                                int attachmentPoint,
                                double lineSpacingFactor)
{
    const double w = width > 1.0e-9 ? width : estimatedTextWidth(text, height, 1.0);
    const double h = estimatedTextBlockHeight(text, height, lineSpacingFactor);
    double dx = 0.0;
    double dy = 0.0;

    switch (attachmentPoint) {
    case 2: case 5: case 8: dx = -w * 0.5; break;
    case 3: case 6: case 9: dx = -w; break;
    default: dx = 0.0; break;
    }
    switch (attachmentPoint) {
    case 1: case 2: case 3: dy = -h; break;
    case 4: case 5: case 6: dy = -h * 0.5; break;
    default: dy = 0.0; break;
    }

    return anchor + rotateOffset(dx, dy, angleDeg);
}

QVector<QPointF> transformedPreviewPoints(const QVector<QPointF>& in, const QPointF& base, const QPointF& ins,
                                          double sx, double sy, double rotDeg)
{
    QVector<QPointF> out;
    out.reserve(in.size());
    const double a = rotDeg * kPi / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    for (const QPointF& p : in) {
        const double x = (p.x() - base.x()) * sx;
        const double y = (p.y() - base.y()) * sy;
        out.append(QPointF(ins.x() + x * c - y * s, ins.y() + x * s + y * c));
    }
    return out;
}

QColor resolveEntityColor(const DxfRawEntity& raw, const QMap<QString, LayerInfo>& layers)
{
    if (raw.trueColor >= 0) {
        return QColor((raw.trueColor >> 16) & 0xff, (raw.trueColor >> 8) & 0xff, raw.trueColor & 0xff);
    }
    if (raw.colorIndex == 256) {
        const QString layerName = cleanLayerName(raw.layer);
        if (layers.contains(layerName)) return layers.value(layerName).color;
        return QColor(Qt::white);
    }
    if (raw.colorIndex == 0) return QColor(Qt::white);
    return aciColor(raw.colorIndex);
}

QString normalizeLineType(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || n.compare(QStringLiteral("BYLAYER"), Qt::CaseInsensitive) == 0) return QStringLiteral("Continuous");
    if (n.contains(QStringLiteral("DASH"), Qt::CaseInsensitive)) return QStringLiteral("dashed");
    if (n.contains(QStringLiteral("DOT"), Qt::CaseInsensitive)) return QStringLiteral("dotted");
    if (n.contains(QStringLiteral("CENTER"), Qt::CaseInsensitive)) return QStringLiteral("dashdot");
    return n;
}

void applyCommon(CadEntity* entity, const DxfRawEntity& raw, const QMap<QString, LayerInfo>& layers)
{
    if (!entity) return;
    const QString layerName = cleanLayerName(raw.layer);
    entity->setLayer(layerName);
    entity->setColor(resolveEntityColor(raw, layers));
    if (raw.lineWeight > 0) entity->setLineWeight(double(raw.lineWeight) / 100.0);
    else entity->setLineWeight(0.0);
    entity->setLineType(normalizeLineType(raw.lineType));
}

void extendBoundsFromEntity(const CadEntity* entity, QRectF& bounds)
{
    if (!entity) return;
    QGraphicsItem* tmp = entity->createGraphicsItem();
    if (!tmp) return;
    const QRectF b = tmp->sceneBoundingRect();
    if (b.isValid() && !b.isNull()) bounds = bounds.isNull() ? b : bounds.united(b);
    delete tmp;
}

void addFinitePoint(QVector<QPointF>& points, const QPointF& p)
{
    if (qIsFinite(p.x()) && qIsFinite(p.y())) points.append(p);
}

QRectF boundsFromPoints(const QVector<QPointF>& points)
{
    QRectF bounds;
    for (const QPointF& p : points) {
        const QRectF r(p, QSizeF(1.0e-6, 1.0e-6));
        bounds = bounds.isNull() ? r : bounds.united(r);
    }
    return bounds;
}

QRectF boundsFromJsonEntities(const QJsonArray& entities)
{
    QRectF bounds;
    for (const QJsonValue& value : entities) {
        std::unique_ptr<CadEntity> entity = CadEntity::fromJson(value.toObject());
        if (!entity) continue;
        QGraphicsItem* item = entity->createGraphicsItem();
        if (!item) continue;
        const QRectF b = item->sceneBoundingRect();
        if (b.isValid() && !b.isNull()) bounds = bounds.isNull() ? b : bounds.united(b);
        delete item;
    }
    return bounds;
}

double rectDiagonal(const QRectF& r)
{
    return std::hypot(r.width(), r.height());
}

double distanceBetweenPoints(const QPointF& a, const QPointF& b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QVector<QPointF> dimensionReferencePoints(const DxfRawEntity& raw)
{
    QVector<QPointF> pts;
    addFinitePoint(pts, dxfPoint(raw.x1, raw.y1));
    addFinitePoint(pts, dxfPoint(raw.x2, raw.y2));
    addFinitePoint(pts, dxfPoint(raw.x3, raw.y3));
    addFinitePoint(pts, dxfPoint(raw.x4, raw.y4));
    for (const QPointF& p : raw.points) addFinitePoint(pts, p);
    return pts;
}

DxfDimStyle resolveDimensionStyle(const DxfRawEntity& raw, const DxfTables* tables)
{
    DxfDimStyle style;
    if (!tables) return style;
    style = tables->headerDimStyle;
    const QString key = raw.dimStyleName.trimmed().isEmpty()
        ? QStringLiteral("STANDARD")
        : raw.dimStyleName.trimmed().toUpper();
    if (tables->dimStyles.contains(key)) style = tables->dimStyles.value(key);
    else if (tables->dimStyles.contains(QStringLiteral("STANDARD"))) style = tables->dimStyles.value(QStringLiteral("STANDARD"));
    return style;
}

double dimensionReferenceSize(const QPointF& a, const QPointF& b, const QPointF& c)
{
    const double len = QLineF(a, b).length();
    const QPointF mid((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
    const double offset = QLineF(mid, c).length();
    return std::max({1.0, len, offset});
}

double clampDimensionAnnotationSize(double requested, double referenceSize, double fallbackRatio)
{
    double v = qIsFinite(requested) && requested > 1.0e-9 ? requested : referenceSize * fallbackRatio;
    const double maxReasonable = std::max(0.05, referenceSize * 0.12);
    if (v > maxReasonable) v = maxReasonable;
    return std::max(1.0e-6, v);
}

double resolvedDimensionTextHeight(const DxfRawEntity& raw, const DxfTables* tables,
                                   const QPointF& a, const QPointF& b, const QPointF& c)
{
    const DxfDimStyle style = resolveDimensionStyle(raw, tables);
    const double scale = std::abs(style.overallScale) > 1.0e-12 ? std::abs(style.overallScale) : 1.0;
    double requested = style.hasTextHeight ? style.textHeight : (raw.hasHeight ? raw.height : style.textHeight);
    if (!qIsFinite(requested) || requested <= 1.0e-9) requested = 2.5;
    requested *= scale;
    return clampDimensionAnnotationSize(requested, dimensionReferenceSize(a, b, c), 0.035);
}

double resolvedDimensionArrowSize(const DxfRawEntity& raw, const DxfTables* tables,
                                  const QPointF& a, const QPointF& b, const QPointF& c,
                                  double textHeight)
{
    const DxfDimStyle style = resolveDimensionStyle(raw, tables);
    const double scale = std::abs(style.overallScale) > 1.0e-12 ? std::abs(style.overallScale) : 1.0;
    double requested = style.hasArrowSize ? style.arrowSize * scale : textHeight * 0.65;
    return clampDimensionAnnotationSize(requested, dimensionReferenceSize(a, b, c), 0.03);
}

std::unique_ptr<CadLinearDimension> makeFallbackDimensionEntity(const DxfRawEntity& raw, const DxfTables* tables)
{
    const bool hasDimExtPair = raw.points.size() >= 2;
    const bool hasExt1 = hasDimExtPair || std::abs(raw.x3) > 1.0e-12 || std::abs(raw.y3) > 1.0e-12;
    const bool hasExt2 = hasDimExtPair || std::abs(raw.x4) > 1.0e-12 || std::abs(raw.y4) > 1.0e-12;
    const QPointF a = hasDimExtPair ? raw.points.at(0) : (hasExt1 ? dxfPoint(raw.x3, raw.y3) : dxfPoint(raw.x1, raw.y1));
    const QPointF b = hasDimExtPair ? raw.points.at(1) : (hasExt2 ? dxfPoint(raw.x4, raw.y4) : dxfPoint(raw.x2, raw.y2));
    QPointF c = dxfPoint(raw.x1, raw.y1);
    if (QLineF(a, b).length() <= 1.0e-9) c = QPointF(a.x() + 10.0, a.y() + 10.0);
    const double textHeight = resolvedDimensionTextHeight(raw, tables, a, b, c);
    const double arrowSize = resolvedDimensionArrowSize(raw, tables, a, b, c, textHeight);
    return std::make_unique<CadLinearDimension>(a, b, c, textHeight, arrowSize);
}

bool dimensionBlockLooksAbsoluteInModelSpace(const DxfRawEntity& raw, const DxfBlockDef& block)
{
    if (block.previewEntities.isEmpty()) return false;
    const QString blockName = block.name.trimmed();
    const bool anonymousDimensionBlock = blockName.startsWith(QStringLiteral("*D"), Qt::CaseInsensitive) ||
                                         raw.blockName.trimmed().startsWith(QStringLiteral("*D"), Qt::CaseInsensitive) ||
                                         block.anonymous;
    if (!anonymousDimensionBlock) return false;

    const QRectF previewBounds = boundsFromJsonEntities(block.previewEntities);
    if (!previewBounds.isValid() || previewBounds.isNull()) return false;

    const QRectF referenceBounds = boundsFromPoints(dimensionReferencePoints(raw));
    if (!referenceBounds.isValid() || referenceBounds.isNull()) return false;

    const QPointF referenceCenter = referenceBounds.center();
    const QPointF normalInsert = dxfPoint(raw.x1, raw.y1);
    const QRectF normallyInsertedBounds = previewBounds.translated(normalInsert - block.basePoint);
    const QRectF absoluteBounds = previewBounds;

    const double absoluteDistance = distanceBetweenPoints(absoluteBounds.center(), referenceCenter);
    const double normalDistance = distanceBetweenPoints(normallyInsertedBounds.center(), referenceCenter);
    const double scale = std::max({1.0, rectDiagonal(previewBounds), rectDiagonal(referenceBounds)});

    if (absoluteDistance <= scale * 2.0 && absoluteDistance < normalDistance * 0.35) return true;
    if (normalDistance > scale * 10.0 && absoluteDistance < normalDistance) return true;
    return false;
}

bool dimensionBlockLooksOversizedAgainstReferences(const DxfRawEntity& raw, const DxfBlockDef& block)
{
    if (block.previewEntities.isEmpty()) return false;
    const QRectF previewBounds = boundsFromJsonEntities(block.previewEntities);
    const QRectF referenceBounds = boundsFromPoints(dimensionReferencePoints(raw));
    if (!previewBounds.isValid() || previewBounds.isNull() || !referenceBounds.isValid() || referenceBounds.isNull()) return false;
    const double previewDiag = rectDiagonal(previewBounds);
    const double referenceDiag = std::max(1.0, rectDiagonal(referenceBounds));
    return previewDiag > referenceDiag * 6.0;
}

void setPointComponent(QVector<QPointF>& pts, double value, bool isX)
{
    if (isX) {
        pts.append(QPointF(value, 0.0));
    } else {
        if (pts.isEmpty()) pts.append(QPointF(0.0, dxfY(value)));
        else pts.last().setY(dxfY(value));
    }
}

QVector<QPointF> arcPoints(const QPointF& center, double radius, double startDeg, double endDeg, int segments)
{
    QVector<QPointF> pts;
    if (radius <= 0.0) return pts;
    double span = endDeg - startDeg;
    if (std::abs(span) < 1e-9) span = 360.0;
    while (span < 0.0) span += 360.0;
    const int n = std::max(4, segments);
    pts.reserve(n + 1);
    for (int i = 0; i <= n; ++i) {
        pts.append(cadPolarPoint(center, radius, startDeg + span * double(i) / double(n)));
    }
    return pts;
}

QVector<QPointF> approximateBulge(const QPointF& p1, const QPointF& p2, double bulge)
{
    QVector<QPointF> pts;
    if (std::abs(bulge) < 1e-12) {
        pts.append(p2);
        return pts;
    }

    const double chord = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
    if (chord < 1e-12) return pts;
    const double theta = 4.0 * std::atan(bulge);
    const double radius = chord / (2.0 * std::sin(std::abs(theta) / 2.0));
    const QPointF mid((p1.x() + p2.x()) * 0.5, (p1.y() + p2.y()) * 0.5);
    const double dx = (p2.x() - p1.x()) / chord;
    const double dy = (p2.y() - p1.y()) / chord;
    const double h = std::sqrt(std::max(0.0, radius * radius - (chord * chord * 0.25)));
    const double side = bulge >= 0.0 ? 1.0 : -1.0;
    const QPointF center(mid.x() - side * dy * h, mid.y() + side * dx * h);
    double a1 = std::atan2(p1.y() - center.y(), p1.x() - center.x());
    double a2 = std::atan2(p2.y() - center.y(), p2.x() - center.x());
    double span = a2 - a1;
    if (bulge > 0.0 && span < 0.0) span += 2.0 * kPi;
    if (bulge < 0.0 && span > 0.0) span -= 2.0 * kPi;
    const int seg = std::min(kBulgeMaxSegments, std::max(4, int(std::ceil(std::abs(span) / (kPi / 18.0)))));
    for (int i = 1; i <= seg; ++i) {
        const double a = a1 + span * double(i) / double(seg);
        pts.append(QPointF(center.x() + radius * std::cos(a),
                           center.y() + radius * std::sin(a)));
    }
    return pts;
}

QVector<QPointF> expandBulges(const QVector<QPointF>& points, const QVector<double>& bulges, bool closed)
{
    if (points.size() < 2) return points;
    QVector<QPointF> out;
    out.reserve(points.size() * 2);
    out.append(points.first());
    const int count = points.size();
    const int last = closed ? count : count - 1;
    for (int i = 0; i < last; ++i) {
        const QPointF a = points.at(i);
        const QPointF b = points.at((i + 1) % count);
        const double bulge = i < bulges.size() ? -bulges.at(i) : 0.0;
        QVector<QPointF> segment = approximateBulge(a, b, bulge);
        for (const QPointF& p : segment) out.append(p);
    }
    return out;
}

bool pointsLookClosed(const QVector<QPointF>& points)
{
    if (points.size() < 4) return false;
    QRectF bounds(points.first(), QSizeF(0.0, 0.0));
    for (const QPointF& p : points) bounds |= QRectF(p, QSizeF(0.0, 0.0));
    const double diag = std::hypot(bounds.width(), bounds.height());
    if (diag < 1.0e-9) return false;
    const double gap = QLineF(points.first(), points.last()).length();
    // Revision clouds exported from DWG often lose the closed flag and can have
    // a small-to-medium endpoint gap because the cloud is represented by many
    // bulged polyline pieces.  Allow a larger closure tolerance only for the
    // later dense-segment cloud test.
    return gap <= std::max(1.0e-4, diag * 0.10);
}

bool looksLikeRevisionCloudPolyline(const QVector<QPointF>& points)
{
    // Revision clouds are usually closed, dense, small-segment polylines around notes.
    // AutoCAD-compatible DXFs sometimes lose the closed flag during export; restore it
    // when the endpoints are almost coincident so no visual gap/stray connection appears.
    if (!pointsLookClosed(points) || points.size() < 12) return false;
    QRectF bounds(points.first(), QSizeF(0.0, 0.0));
    for (const QPointF& p : points) bounds |= QRectF(p, QSizeF(0.0, 0.0));
    const double diag = std::hypot(bounds.width(), bounds.height());
    if (diag < 1.0e-9) return false;
    double total = 0.0;
    double maxSeg = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        const double len = QLineF(points.at(i - 1), points.at(i)).length();
        total += len;
        maxSeg = std::max(maxSeg, len);
    }
    const double avg = total / std::max(1, static_cast<int>(points.size()) - 1);
    return avg < diag * 0.08 && maxSeg < diag * 0.25;
}

QVector<int> validPolyfaceIndices(const DxfRawEntity& face)
{
    QVector<int> ids;
    const int rawIds[4] = {face.vIndex1, face.vIndex2, face.vIndex3, face.vIndex4};
    for (int idx : rawIds) {
        if (idx == 0) continue;
        ids.append(std::abs(idx));
    }
    return ids;
}

DxfRawEntity parseEntityBody(const QString& type, DxfPairReader& reader)
{
    DxfRawEntity raw;
    raw.type = type.toUpper();
    DxfPair p;

    while (reader.next(p)) {
        if (p.code == 0) {
            reader.unread(p);
            break;
        }

        const double d = p.value.toDouble();
        const int i = p.value.toInt();
        switch (p.code) {
        case 5: raw.handle = p.value; break;
        case 330: raw.ownerHandle = p.value; break;
        case 347: raw.materialHandle = p.value; break;
        case 360: raw.extensionDictionaryHandle = p.value; raw.hasExtensionDictionary = true; break;
        case 410: raw.layoutName = p.value.trimmed().isEmpty() ? QStringLiteral("Model") : p.value.trimmed(); break;
        case 100: raw.subclassMarkers.append(p.value); break;
        case 102: raw.controlStrings.append(p.value); break; // application-defined control string / extension dictionary marker
        case 1:
            if (raw.type == QStringLiteral("TEXT") || raw.type == QStringLiteral("MTEXT") ||
                raw.type == QStringLiteral("ATTRIB") || raw.type == QStringLiteral("ATTDEF") ||
                raw.type == QStringLiteral("TOLERANCE") || raw.type == QStringLiteral("ARCALIGNEDTEXT")) raw.text += p.value;
            else if (raw.type == QStringLiteral("HATCH")) raw.hatchPattern = p.value;
            break;
        case 2:
            if (raw.type == QStringLiteral("INSERT") || raw.type == QStringLiteral("MINSERT") || raw.type == QStringLiteral("DIMENSION") || raw.type == QStringLiteral("SHAPE")) raw.blockName = p.value;
            else if (raw.type == QStringLiteral("ATTRIB") || raw.type == QStringLiteral("ATTDEF")) raw.attribTag = p.value.trimmed();
            else if (raw.type == QStringLiteral("HATCH")) raw.hatchPattern = p.value;
            break;
        case 3:
            if (raw.type == QStringLiteral("DIMENSION")) raw.dimStyleName = p.value.trimmed().isEmpty() ? QStringLiteral("Standard") : p.value.trimmed();
            else if (raw.type == QStringLiteral("MTEXT") || raw.type == QStringLiteral("TOLERANCE") || raw.type == QStringLiteral("ARCALIGNEDTEXT")) raw.text += p.value;
            break;
        case 6: raw.lineType = p.value; break;
        case 7: raw.textStyle = p.value; break;
        case 8: raw.layer = cleanLayerName(p.value); break;
        case 10:
            if (raw.type == QStringLiteral("LWPOLYLINE") || raw.type == QStringLiteral("SPLINE") ||
                raw.type == QStringLiteral("LEADER") || raw.type == QStringLiteral("MLEADER")) {
                setPointComponent(raw.points, d, true);
            } else {
                raw.x1 = d;
            }
            break;
        case 20:
            if (raw.type == QStringLiteral("LWPOLYLINE") || raw.type == QStringLiteral("SPLINE") ||
                raw.type == QStringLiteral("LEADER") || raw.type == QStringLiteral("MLEADER")) {
                setPointComponent(raw.points, d, false);
            } else {
                raw.y1 = d;
            }
            break;
        case 30: raw.z1 = d; break;
        case 38: raw.elevation = d; raw.hasElevation = true; break;
        case 39: break; // thickness is currently not rendered by the 2D engine
        case 48: raw.lineTypeScale = d; break;
        case 60: raw.visibility = i; break;
        case 11:
            if (raw.type == QStringLiteral("SPLINE")) setPointComponent(raw.splineFitPoints, d, true);
            else if (raw.type == QStringLiteral("MLINE")) setPointComponent(raw.points, d, true);
            else { raw.x2 = d; raw.axisX = d; }
            break;
        case 21:
            if (raw.type == QStringLiteral("SPLINE")) setPointComponent(raw.splineFitPoints, d, false);
            else if (raw.type == QStringLiteral("MLINE")) setPointComponent(raw.points, d, false);
            else { raw.y2 = d; raw.axisY = d; }
            break;
        case 31: raw.z2 = d; break;
        case 12: raw.x3 = d; break;
        case 22: raw.y3 = d; break;
        case 32: raw.z3 = d; break;
        case 13: raw.x4 = d; raw.points.append(dxfPoint(d, 0.0)); break;
        case 23:
            raw.y4 = d;
            if (!raw.points.isEmpty()) raw.points.last().setY(dxfY(d));
            break;
        case 33: raw.z4 = d; break;
        case 14: raw.points.append(dxfPoint(d, 0.0)); break;
        case 24:
            if (!raw.points.isEmpty()) raw.points.last().setY(dxfY(d));
            break;
        case 40:
            if (raw.type == QStringLiteral("CIRCLE") || raw.type == QStringLiteral("ARC")) raw.radius = std::abs(d);
            else if (raw.type == QStringLiteral("ELLIPSE")) raw.ratio = std::abs(d);
            else if (raw.type == QStringLiteral("SPLINE")) raw.splineKnots.append(d);
            else if (raw.type == QStringLiteral("LWPOLYLINE") || raw.type == QStringLiteral("VERTEX")) { /* start width: preserved as geometry-neutral metadata for now */ }
            else { raw.height = std::abs(d) > 1e-12 ? std::abs(d) : raw.height; raw.hasHeight = true; }
            break;
        case 41:
            if (raw.type == QStringLiteral("INSERT") || raw.type == QStringLiteral("MINSERT")) raw.scaleX = std::abs(d) <= 1e-12 ? 1.0 : d;
            else if (raw.type == QStringLiteral("TEXT") || raw.type == QStringLiteral("ATTRIB") || raw.type == QStringLiteral("ATTDEF")) raw.textWidthFactor = normalizedWidthFactor(d);
            else if (raw.type == QStringLiteral("MTEXT")) raw.mtextWidth = std::abs(d);
            else if (raw.type == QStringLiteral("SPLINE")) raw.splineWeights.append(std::abs(d) <= 1e-12 ? 1.0 : d);
            else if (raw.type == QStringLiteral("LWPOLYLINE") || raw.type == QStringLiteral("VERTEX")) { /* end width: not rendered by the current 2D engine */ }
            else raw.startAngle = d * 180.0 / kPi;
            break;
        case 42:
            if (raw.type == QStringLiteral("INSERT") || raw.type == QStringLiteral("MINSERT")) raw.scaleY = std::abs(d) <= 1e-12 ? 1.0 : d;
            else if (raw.type == QStringLiteral("LWPOLYLINE") || raw.type == QStringLiteral("VERTEX")) raw.bulges.append(d);
            else if (raw.type == QStringLiteral("SPLINE")) { /* knot tolerance: ignored by the tessellator */ }
            else raw.endAngle = d * 180.0 / kPi;
            break;
        case 43:
            if (raw.type == QStringLiteral("INSERT") || raw.type == QStringLiteral("MINSERT")) raw.scaleZ = std::abs(d) <= 1e-12 ? 1.0 : d;
            break;
        case 50: raw.rotation = d; raw.startAngle = d; break;
        case 51:
            if (raw.type == QStringLiteral("TEXT") || raw.type == QStringLiteral("ATTRIB") || raw.type == QStringLiteral("ATTDEF")) raw.textObliqueAngle = d;
            else raw.endAngle = d;
            break;
        case 62: raw.colorIndex = i; break;
        case 66: raw.attrFollow = i; break;
        case 67: raw.space = i; break;
        case 70:
            if (raw.type == QStringLiteral("INSERT") || raw.type == QStringLiteral("MINSERT")) raw.colCount = std::max(1, i);
            else raw.flags = i;
            break;
        case 71:
            if (raw.type == QStringLiteral("INSERT") || raw.type == QStringLiteral("MINSERT")) raw.rowCount = std::max(1, i);
            else if (raw.type == QStringLiteral("MTEXT")) raw.mtextAttachmentPoint = i > 0 ? i : 1;
            else if (raw.type == QStringLiteral("TEXT") || raw.type == QStringLiteral("ATTRIB") || raw.type == QStringLiteral("ATTDEF")) raw.textGenerationFlags = i;
            else if (raw.type == QStringLiteral("SPLINE")) raw.splineDegree = std::max(1, i);
            else raw.vIndex1 = i;
            break;
        case 72:
            if (raw.type == QStringLiteral("TEXT") || raw.type == QStringLiteral("ATTRIB") || raw.type == QStringLiteral("ATTDEF")) raw.textHJustification = i;
            else if (raw.type == QStringLiteral("MTEXT")) raw.mtextDrawingDirection = i;
            else if (raw.type == QStringLiteral("SPLINE")) raw.splineKnotCount = std::max(0, i);
            else raw.vIndex2 = i;
            break;
        case 73:
            if (raw.type == QStringLiteral("TEXT") || raw.type == QStringLiteral("ATTRIB") || raw.type == QStringLiteral("ATTDEF")) raw.textVJustification = i;
            else if (raw.type == QStringLiteral("SPLINE")) raw.splineControlCount = std::max(0, i);
            else raw.vIndex3 = i;
            break;
        case 74:
            if (raw.type == QStringLiteral("SPLINE")) raw.splineFitCount = std::max(0, i);
            else if (raw.type == QStringLiteral("MTEXT")) raw.mtextLineSpacingStyle = i;
            else raw.vIndex4 = i;
            break;
        case 90: raw.vertexCount = i; break;
        case 91: raw.hatchLoopCount = i; break;
        case 44:
            if (raw.type == QStringLiteral("MTEXT")) raw.mtextLineSpacingFactor = d > 1.0e-9 ? d : 1.0;
            else raw.colSpacing = d;
            break;
        case 45: raw.rowSpacing = d; break;
        case 210: raw.extrusionX = d; break;
        case 220: raw.extrusionY = d; break;
        case 230: raw.extrusionZ = d; break;
        case 370: raw.lineWeight = i; break;
        case 420: raw.trueColor = i; break;
        case 300:
        case 301:
        case 302:
        case 303:
        case 304:
        case 305:
        case 306:
        case 307:
        case 308:
        case 309:
            if (raw.type == QStringLiteral("MLEADER") || raw.type == QStringLiteral("MTEXT") || raw.type == QStringLiteral("LEADER")) {
                if (!p.value.trimmed().isEmpty()) raw.text += p.value;
            }
            break;
        case 440: raw.transparency = i; break;
        default:
            if (p.code == 1001) raw.xdataAppIds.append(p.value.trimmed());
            else if (p.code >= 1000 && p.code <= 1071) ++raw.xdataPairCount;
            // XDATA / reactors / extension dictionaries are part of modern DXF (R2000+).
            // They are now counted/preserved in the internal DxfDocumentModel even when
            // not rendered by the 2D viewer.
            break;
        }
    }
    return raw;
}

DxfRawEntity parsePolylineEntity(DxfPairReader& reader)
{
    DxfRawEntity raw = parseEntityBody(QStringLiteral("POLYLINE"), reader);
    const bool isPolyface = (raw.flags & 64) != 0;
    QVector<QPointF> meshVertices;
    QVector<DxfRawEntity> meshFaces;

    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("SEQEND")) break;
        if (type != QStringLiteral("VERTEX")) {
            reader.unread(p);
            break;
        }
        DxfRawEntity vertex = parseEntityBody(QStringLiteral("VERTEX"), reader);
        if (isPolyface && (vertex.flags & 128)) {
            if (vertex.flags & 64) {
                meshVertices.append(dxfPoint(vertex.x1, vertex.y1));
            } else {
                meshFaces.append(vertex);
            }
        } else {
            raw.points.append(dxfPoint(vertex.x1, vertex.y1));
            raw.bulges.append(vertex.bulges.isEmpty() ? 0.0 : vertex.bulges.first());
        }
    }

    if (isPolyface && !meshVertices.isEmpty()) {
        for (const DxfRawEntity& face : meshFaces) {
            QVector<QPointF> loop;
            for (int id : validPolyfaceIndices(face)) {
                const int pos = id - 1;
                if (pos >= 0 && pos < meshVertices.size()) loop.append(meshVertices.at(pos));
            }
            if (loop.size() >= 3) raw.loops.append(loop);
        }
        if (raw.loops.isEmpty() && meshVertices.size() >= 3) raw.loops.append(meshVertices);
    }
    return raw;
}

DxfRawEntity parseHatchEntity(DxfPairReader& reader)
{
    DxfRawEntity raw;
    raw.type = QStringLiteral("HATCH");

    QVector<QPointF> currentLoop;
    QVector<QPointF> pendingPolylinePoints;
    QVector<double> pendingPolylineBulges;
    QPointF lineStart;
    QPointF lineEnd;
    QPointF arcCenter;
    QPointF ellipseMajor;
    QVector<QPointF> edgeSplineControls;
    QVector<QPointF> edgeSplineFitPoints;
    QVector<double> edgeSplineKnots;
    QVector<double> edgeSplineWeights;
    double arcRadius = 0.0;
    double startAngle = 0.0;
    double endAngle = 0.0;
    double edgeBulge = 0.0;
    double ellipseRatio = 1.0;
    int edgeType = 0;
    int edgeSplineDegree = 3;
    int edgeSplineFlags = 0;
    int edgeSplineKnotCount = 0;
    int edgeSplineControlCount = 0;
    int edgeSplineFitCount = 0;
    bool loopStarted = false;
    bool polylineBoundary = false;
    bool loopClosed = true;
    bool arcCcw = true;
    bool haveLineStart = false;
    bool haveLineEnd = false;
    bool currentLoopHasUnsafeConnection = false;

    auto appendHatchEdgePoint = [&](const QPointF& point, bool startsNewEdge) {
        if (!qIsFinite(point.x()) || !qIsFinite(point.y())) return;

        // In DXF HATCH edge paths, each edge start must touch the previous edge end.
        // Some files contain stale/unordered edge records. If we append those starts to
        // one QPainterPath, Qt draws a false long bridge line through the drawing.
        // Mark the loop unsafe instead of inventing that bridge. Polyline-boundary
        // hatches are handled separately and are not checked with this rule.
        if (startsNewEdge && !currentLoop.isEmpty()) {
            const double gap = QLineF(currentLoop.last(), point).length();
            if (gap > 1.0e-4) currentLoopHasUnsafeConnection = true;
        }
        currentLoop.append(point);
    };

    auto cleanLoop = [](QVector<QPointF> loop) {
        QVector<QPointF> out;
        out.reserve(loop.size());
        for (const QPointF& p : loop) {
            if (!qIsFinite(p.x()) || !qIsFinite(p.y())) continue;
            if (out.isEmpty() || std::hypot(out.last().x() - p.x(), out.last().y() - p.y()) > 1e-8) out.append(p);
        }
        while (out.size() > 2 && std::hypot(out.first().x() - out.last().x(), out.first().y() - out.last().y()) < 1e-8)
            out.removeLast();

        // Some ODA/AutoCAD exported HATCH boundaries contain origin/seed points (often 0,0)
        // inside the polyline vertex stream. QCAD/dxflib does not render these as boundary
        // edges. If they are kept, the viewer draws long diagonal spikes from the real hatch
        // loop to the drawing origin. Remove only isolated points whose nearest neighbour is
        // wildly farther than the normal local edge spacing, so valid large hatch borders stay intact.
        if (out.size() >= 4) {
            QVector<double> nearest;
            nearest.reserve(out.size());
            for (int i = 0; i < out.size(); ++i) {
                double best = std::numeric_limits<double>::max();
                for (int j = 0; j < out.size(); ++j) {
                    if (i == j) continue;
                    best = std::min(best, QLineF(out.at(i), out.at(j)).length());
                }
                if (qIsFinite(best) && best < std::numeric_limits<double>::max()) nearest.append(best);
            }
            if (nearest.size() >= 4) {
                std::sort(nearest.begin(), nearest.end());
                const double medianNearest = std::max(1.0e-6, nearest.at(nearest.size() / 2));
                QVector<QPointF> filtered;
                filtered.reserve(out.size());
                for (int i = 0; i < out.size(); ++i) {
                    double best = std::numeric_limits<double>::max();
                    for (int j = 0; j < out.size(); ++j) {
                        if (i == j) continue;
                        best = std::min(best, QLineF(out.at(i), out.at(j)).length());
                    }
                    const bool isolatedSpikePoint = best > medianNearest * 25.0 && best > 250.0;
                    if (!isolatedSpikePoint) filtered.append(out.at(i));
                }
                if (filtered.size() >= 3) out = filtered;
            }
        }

        while (out.size() > 2 && std::hypot(out.first().x() - out.last().x(), out.first().y() - out.last().y()) < 1e-8)
            out.removeLast();
        return out;
    };

    auto resetHatchEdgeState = [&]() {
        currentLoop.clear();
        pendingPolylinePoints.clear();
        pendingPolylineBulges.clear();
        edgeSplineControls.clear();
        edgeSplineFitPoints.clear();
        edgeSplineKnots.clear();
        edgeSplineWeights.clear();
        lineStart = QPointF();
        lineEnd = QPointF();
        arcCenter = QPointF();
        ellipseMajor = QPointF();
        arcRadius = 0.0;
        startAngle = 0.0;
        endAngle = 0.0;
        edgeBulge = 0.0;
        ellipseRatio = 1.0;
        edgeType = 0;
        edgeSplineDegree = 3;
        edgeSplineFlags = 0;
        edgeSplineKnotCount = 0;
        edgeSplineControlCount = 0;
        edgeSplineFitCount = 0;
        polylineBoundary = false;
        loopClosed = true;
        arcCcw = true;
        haveLineStart = false;
        haveLineEnd = false;
        currentLoopHasUnsafeConnection = false;
    };

    auto flushPolylineBoundary = [&]() {
        if (!pendingPolylinePoints.isEmpty()) {
            QVector<QPointF> expanded = expandBulges(pendingPolylinePoints, pendingPolylineBulges, loopClosed);
            if (expanded.size() >= 3) for (const QPointF& ep : expanded) currentLoop.append(ep);
            else for (const QPointF& ep : pendingPolylinePoints) currentLoop.append(ep);
        }
        pendingPolylinePoints.clear();
        pendingPolylineBulges.clear();
    };

    auto flushSplineEdge = [&]() {
        if (edgeType != 4) return;
        DxfRawEntity spline;
        spline.type = QStringLiteral("SPLINE");
        spline.flags = edgeSplineFlags;
        spline.splineDegree = edgeSplineDegree;
        spline.splineKnotCount = edgeSplineKnotCount;
        spline.splineControlCount = edgeSplineControlCount;
        spline.splineFitCount = edgeSplineFitCount;
        spline.points = edgeSplineControls;
        spline.splineFitPoints = edgeSplineFitPoints;
        spline.splineKnots = edgeSplineKnots;
        spline.splineWeights = edgeSplineWeights;
        QVector<QPointF> pts = approximateSplinePoints(spline);
        for (int idx = 0; idx < pts.size(); ++idx) appendHatchEdgePoint(pts.at(idx), idx == 0);
        edgeSplineControls.clear();
        edgeSplineFitPoints.clear();
        edgeSplineKnots.clear();
        edgeSplineWeights.clear();
        edgeSplineDegree = 3;
        edgeSplineFlags = 0;
        edgeSplineKnotCount = 0;
        edgeSplineControlCount = 0;
        edgeSplineFitCount = 0;
    };

    auto flushArcEllipseEdge = [&]() {
        if (edgeType == 2 && arcRadius > 0.0) {
            QVector<QPointF> pts = arcPoints(arcCenter, arcRadius, startAngle, endAngle, 32);
            if (!arcCcw) std::reverse(pts.begin(), pts.end());
            for (int idx = 0; idx < pts.size(); ++idx) appendHatchEdgePoint(pts.at(idx), idx == 0);
        } else if (edgeType == 3) {
            const double a = std::hypot(ellipseMajor.x(), ellipseMajor.y());
            const double b = a * (ellipseRatio <= 0.0 ? 1.0 : ellipseRatio);
            if (a > 0.0 && b > 0.0) {
                double span = endAngle - startAngle;
                while (span < 0.0) span += 360.0;
                const int n = std::max(12, std::min(96, int(std::ceil(std::abs(span) / 6.0))));
                const double rot = std::atan2(ellipseMajor.y(), ellipseMajor.x());
                const double cr = std::cos(rot), sr = std::sin(rot);
                QVector<QPointF> pts;
                pts.reserve(n + 1);
                for (int k = 0; k <= n; ++k) {
                    const double t = (startAngle + span * double(k) / double(n)) * kPi / 180.0;
                    const double x = a * std::cos(t);
                    const double y = b * std::sin(t);
                    pts.append(QPointF(arcCenter.x() + x * cr + y * sr, arcCenter.y() + x * sr - y * cr));
                }
                if (!arcCcw) std::reverse(pts.begin(), pts.end());
                for (int idx = 0; idx < pts.size(); ++idx) appendHatchEdgePoint(pts.at(idx), idx == 0);
            }
        }
        if (edgeType == 2 || edgeType == 3) {
            arcRadius = 0.0;
            startAngle = 0.0;
            endAngle = 0.0;
            ellipseRatio = 1.0;
            ellipseMajor = QPointF();
            arcCcw = true;
        }
    };

    auto flushLine = [&]() {
        if (haveLineStart) appendHatchEdgePoint(lineStart, true);
        if (haveLineEnd) appendHatchEdgePoint(lineEnd, false);
        haveLineStart = false;
        haveLineEnd = false;
    };

    auto flushLoop = [&]() {
        flushPolylineBoundary();
        flushLine();
        flushArcEllipseEdge();
        flushSplineEdge();
        QVector<QPointF> loop = cleanLoop(currentLoop);
        if (!currentLoopHasUnsafeConnection && loop.size() >= 3) raw.loops.append(loop);
        currentLoop.clear();
        loopStarted = false;
        polylineBoundary = false;
        edgeType = 0;
        loopClosed = true;
        currentLoopHasUnsafeConnection = false;
    };

    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0) {
            reader.unread(p);
            break;
        }
        const double d = p.value.toDouble();
        const int i = p.value.toInt();
        switch (p.code) {
        case 5: raw.handle = p.value; break;
        case 330: raw.ownerHandle = p.value; break;
        case 347: raw.materialHandle = p.value; break;
        case 410: raw.layoutName = p.value.trimmed().isEmpty() ? QStringLiteral("Model") : p.value.trimmed(); break;
        case 100: raw.subclassMarkers.append(p.value); break;
        case 102: raw.controlStrings.append(p.value); break;
        case 1: raw.hatchPattern = p.value; break;
        case 2: raw.hatchPattern = p.value; break;
        case 8: raw.layer = cleanLayerName(p.value); break;
        case 62: raw.colorIndex = i; break;
        case 67: raw.space = i; break;
        case 70: raw.solidFill = (i != 0); break;
        case 71: raw.associative = (i != 0); break;
        case 75: raw.hatchStyle = i; break;
        case 91: raw.hatchLoopCount = i; break;
        case 370: raw.lineWeight = i; break;
        case 420: raw.trueColor = i; break;
        case 440: raw.transparency = i; break;
        case 92:
            if (loopStarted) flushLoop();
            resetHatchEdgeState();
            raw.hatchPathFlags = i;
            loopStarted = true;
            polylineBoundary = (i & 2) != 0;
            break;
        case 72:
            // In edge paths this is edge type; in polyline paths it means "has bulge".
            if (!polylineBoundary) { flushLine(); flushArcEllipseEdge(); flushSplineEdge(); edgeType = i; }
            break;
        case 73:
            if (polylineBoundary) loopClosed = (i != 0);
            else if (edgeType == 4) { if (i != 0) edgeSplineFlags |= 4; } // rational
            else arcCcw = (i != 0);
            break;
        case 74:
            if (edgeType == 4 && i != 0) edgeSplineFlags |= 2; // periodic
            break;
        case 93:
            // Number of edges or vertices. We validate by actual points collected.
            break;
        case 94:
            if (edgeType == 4) edgeSplineDegree = std::max(1, i);
            break;
        case 95:
            if (edgeType == 4) edgeSplineKnotCount = std::max(0, i);
            break;
        case 96:
            if (edgeType == 4) edgeSplineControlCount = std::max(0, i);
            break;
        case 97:
            if (edgeType == 4) edgeSplineFitCount = std::max(0, i);
            break;
        case 10:
            if (!loopStarted) break;
            if (polylineBoundary) {
                pendingPolylinePoints.append(dxfPoint(d, 0.0));
            } else if (edgeType == 4) {
                setPointComponent(edgeSplineControls, d, true);
            } else if (edgeType == 2 || edgeType == 3) {
                arcCenter.setX(d);
            } else {
                lineStart.setX(d); haveLineStart = true;
            }
            break;
        case 20:
            if (!loopStarted) break;
            if (polylineBoundary) {
                if (pendingPolylinePoints.isEmpty()) pendingPolylinePoints.append(dxfPoint(0.0, d));
                else pendingPolylinePoints.last().setY(dxfY(d));
            } else if (edgeType == 4) {
                setPointComponent(edgeSplineControls, d, false);
            } else if (edgeType == 2 || edgeType == 3) {
                arcCenter.setY(dxfY(d));
            } else {
                lineStart.setY(dxfY(d)); haveLineStart = true;
            }
            break;
        case 11:
            if (!loopStarted) break;
            if (edgeType == 4) setPointComponent(edgeSplineFitPoints, d, true);
            else if (edgeType == 3) ellipseMajor.setX(d);
            else { lineEnd.setX(d); haveLineEnd = true; }
            break;
        case 21:
            if (!loopStarted) break;
            if (edgeType == 4) setPointComponent(edgeSplineFitPoints, d, false);
            else if (edgeType == 3) ellipseMajor.setY(dxfY(d));
            else { lineEnd.setY(dxfY(d)); haveLineEnd = true; }
            break;
        case 40:
            if (edgeType == 4) edgeSplineKnots.append(d);
            else if (edgeType == 3) ellipseRatio = std::abs(d);
            else arcRadius = std::abs(d);
            break;
        case 41:
            if (edgeType == 4) edgeSplineWeights.append(std::abs(d) <= 1.0e-12 ? 1.0 : d);
            else raw.hatchScale = std::abs(d) > 1.0e-12 ? std::abs(d) : raw.hatchScale;
            break;
        case 42:
            if (polylineBoundary) pendingPolylineBulges.append(d);
            else if (edgeType == 4) edgeSplineWeights.append(std::abs(d) <= 1.0e-12 ? 1.0 : d);
            else edgeBulge = d;
            break;
        case 50: startAngle = d; break;
        case 52: raw.hatchAngle = d; break;
        case 51:
            endAngle = d;
            break;
        default:
            if (p.code == 1001) raw.xdataAppIds.append(p.value.trimmed());
            else if (p.code >= 1000 && p.code <= 1071) ++raw.xdataPairCount;
            break;
        }
    }
    if (loopStarted || !currentLoop.isEmpty() || !pendingPolylinePoints.isEmpty()) flushLoop();
    if (raw.loops.isEmpty() && !raw.points.isEmpty()) raw.loops.append(raw.points);
    return raw;
}

bool isSupportedEntityType(const QString& type)
{
    const QString t = type.toUpper();
    return t == QStringLiteral("LINE") || t == QStringLiteral("CIRCLE") || t == QStringLiteral("ARC") ||
           t == QStringLiteral("LWPOLYLINE") || t == QStringLiteral("POLYLINE") || t == QStringLiteral("ELLIPSE") ||
           t == QStringLiteral("POINT") || t == QStringLiteral("TEXT") || t == QStringLiteral("MTEXT") ||
           t == QStringLiteral("ATTRIB") || t == QStringLiteral("ATTDEF") || t == QStringLiteral("INSERT") || t == QStringLiteral("MINSERT") ||
           t == QStringLiteral("SPLINE") || t == QStringLiteral("HATCH") || t == QStringLiteral("DIMENSION") ||
           t == QStringLiteral("LEADER") || t == QStringLiteral("MLEADER") || t == QStringLiteral("3DFACE") ||
           t == QStringLiteral("SOLID") || t == QStringLiteral("TRACE") || t == QStringLiteral("XLINE") ||
           t == QStringLiteral("RAY") || t == QStringLiteral("VIEWPORT") || t == QStringLiteral("IMAGE") ||
           t == QStringLiteral("WIPEOUT") || t == QStringLiteral("MLINE") || t == QStringLiteral("HELIX") ||
           t == QStringLiteral("SHAPE") || t == QStringLiteral("TOLERANCE") || t == QStringLiteral("ARCALIGNEDTEXT");
}

QVector<QPointF> catmullRomPoints(const QVector<QPointF>& controls)
{
    if (controls.size() < 3) return controls;
    QVector<QPointF> out;
    const int n = controls.size();
    out.reserve(std::min(512, n * 12));

    for (int i = 0; i < n - 1; ++i) {
        const QPointF p0 = controls.at(std::max(0, i - 1));
        const QPointF p1 = controls.at(i);
        const QPointF p2 = controls.at(i + 1);
        const QPointF p3 = controls.at(std::min(n - 1, i + 2));
        const int steps = std::max(4, std::min(24, int(std::hypot(p2.x() - p1.x(), p2.y() - p1.y()) / 6.0) + 4));
        for (int j = 0; j < steps; ++j) {
            const double t = double(j) / double(steps);
            const double t2 = t * t;
            const double t3 = t2 * t;
            const double x = 0.5 * ((2.0 * p1.x()) + (-p0.x() + p2.x()) * t +
                                    (2.0*p0.x() - 5.0*p1.x() + 4.0*p2.x() - p3.x()) * t2 +
                                    (-p0.x() + 3.0*p1.x() - 3.0*p2.x() + p3.x()) * t3);
            const double y = 0.5 * ((2.0 * p1.y()) + (-p0.y() + p2.y()) * t +
                                    (2.0*p0.y() - 5.0*p1.y() + 4.0*p2.y() - p3.y()) * t2 +
                                    (-p0.y() + 3.0*p1.y() - 3.0*p2.y() + p3.y()) * t3);
            out.append(QPointF(x, y));
            if (out.size() >= kSplineMaxSamples * 4) break;
        }
        if (out.size() >= kSplineMaxSamples * 4) break;
    }
    out.append(controls.last());
    return out;
}

QVector<double> makeClampedUniformKnots(int controlCount, int degree)
{
    QVector<double> knots;
    const int n = std::max(0, controlCount - 1);
    const int p = std::max(1, std::min(degree, n));
    const int knotCount = controlCount + p + 1;
    knots.reserve(knotCount);
    for (int i = 0; i < knotCount; ++i) {
        if (i <= p) knots.append(0.0);
        else if (i >= controlCount) knots.append(1.0);
        else knots.append(double(i - p) / double(controlCount - p));
    }
    return knots;
}

int findSplineSpan(const QVector<double>& knots, int controlCount, int degree, double t)
{
    const int n = controlCount - 1;
    if (t >= knots.at(n + 1)) return n;
    if (t <= knots.at(degree)) return degree;
    int low = degree;
    int high = n + 1;
    int mid = (low + high) / 2;
    while (t < knots.at(mid) || t >= knots.at(mid + 1)) {
        if (t < knots.at(mid)) high = mid;
        else low = mid;
        mid = (low + high) / 2;
    }
    return mid;
}

QPointF deBoorPoint(const QVector<QPointF>& controls,
                    const QVector<double>& weights,
                    const QVector<double>& knots,
                    int degree,
                    double t)
{
    struct HP { double x = 0.0; double y = 0.0; double w = 1.0; };
    const int nCtrl = controls.size();
    const int p = std::max(1, std::min(degree, nCtrl - 1));
    const int span = findSplineSpan(knots, nCtrl, p, t);
    QVector<HP> d;
    d.reserve(p + 1);
    for (int j = 0; j <= p; ++j) {
        const int idx = span - p + j;
        const double w = (idx >= 0 && idx < weights.size()) ? weights.at(idx) : 1.0;
        const QPointF c = controls.at(idx);
        d.append({c.x() * w, c.y() * w, w});
    }
    for (int r = 1; r <= p; ++r) {
        for (int j = p; j >= r; --j) {
            const int i = span - p + j;
            const double denom = knots.at(i + p - r + 1) - knots.at(i);
            const double alpha = std::abs(denom) <= 1.0e-12 ? 0.0 : (t - knots.at(i)) / denom;
            d[j].x = (1.0 - alpha) * d[j - 1].x + alpha * d[j].x;
            d[j].y = (1.0 - alpha) * d[j - 1].y + alpha * d[j].y;
            d[j].w = (1.0 - alpha) * d[j - 1].w + alpha * d[j].w;
        }
    }
    const double w = std::abs(d[p].w) <= 1.0e-12 ? 1.0 : d[p].w;
    return QPointF(d[p].x / w, d[p].y / w);
}

QVector<QPointF> approximateSplinePoints(const DxfRawEntity& raw)
{
    // QCAD dxflib exposes SPLINE as degree + knots + control points + fit points.
    // Prefer fit points when present because they lie on the intended curve; otherwise
    // evaluate the B-spline/NURBS control data with a lightweight de Boor sampler.
    if (raw.splineFitPoints.size() >= 2) return catmullRomPoints(raw.splineFitPoints);
    if (raw.points.size() < 2) return raw.points;
    if (raw.points.size() == 2) return raw.points;

    const int nCtrl = raw.points.size();
    const int degree = std::max(1, std::min(raw.splineDegree, nCtrl - 1));
    QVector<double> knots = raw.splineKnots;
    if (knots.size() < nCtrl + degree + 1) knots = makeClampedUniformKnots(nCtrl, degree);

    const double tStart = knots.at(degree);
    const double tEnd = knots.at(nCtrl);
    if (!qIsFinite(tStart) || !qIsFinite(tEnd) || tEnd <= tStart) return catmullRomPoints(raw.points);

    const int samples = std::max(24, std::min(512, std::max(nCtrl * 16, kSplineMaxSamples)));
    QVector<QPointF> out;
    out.reserve(samples + 1);
    for (int i = 0; i <= samples; ++i) {
        const double t = (i == samples) ? tEnd : (tStart + (tEnd - tStart) * double(i) / double(samples));
        out.append(deBoorPoint(raw.points, raw.splineWeights, knots, degree, t));
    }
    if ((raw.flags & 1) != 0 && out.size() > 2 && QLineF(out.first(), out.last()).length() > 1.0e-8)
        out.append(out.first());
    return out;
}

QVector<QPointF> approximateEllipsePoints(const DxfRawEntity& raw, bool* fullEllipse)
{
    const double a = std::hypot(raw.axisX, raw.axisY);
    const double ratio = raw.ratio <= 0.0 ? 1.0 : raw.ratio;
    const double b = a * ratio;
    QVector<QPointF> pts;
    if (a <= 1.0e-12 || b <= 1.0e-12) {
        if (fullEllipse) *fullEllipse = false;
        return pts;
    }

    double start = raw.startAngle;
    double end = raw.endAngle;
    if (std::abs(end - start) <= 1.0e-9) end = start + 360.0;
    double span = end - start;
    while (span < 0.0) span += 360.0;
    const bool full = std::abs(span - 360.0) <= 1.0e-6 || std::abs(span) <= 1.0e-6;
    if (fullEllipse) *fullEllipse = full;
    if (full) span = 360.0;

    const int segments = std::max(24, std::min(256, int(std::ceil(std::abs(span) / 4.0))));
    const QPointF axis = dxfVector(raw.axisX, raw.axisY);
    const double rot = std::atan2(axis.y(), axis.x());
    const double cr = std::cos(rot);
    const double sr = std::sin(rot);
    const QPointF center = dxfPoint(raw.x1, raw.y1);
    pts.reserve(segments + (full ? 0 : 1));
    const int count = full ? segments : segments + 1;
    for (int i = 0; i < count; ++i) {
        const double deg = start + span * double(i) / double(segments);
        const double t = deg * kPi / 180.0;
        const double x = a * std::cos(t);
        const double y = b * std::sin(t);
        pts.append(QPointF(center.x() + x * cr + y * sr, center.y() + x * sr - y * cr));
    }
    return pts;
}


QRectF boundsForPointLoop(const QVector<QPointF>& loop)
{
    QRectF bounds;
    for (const QPointF& p : loop) {
        if (!qIsFinite(p.x()) || !qIsFinite(p.y())) continue;
        const QRectF pointRect(p, QSizeF(0.0, 0.0));
        bounds = bounds.isNull() ? pointRect : bounds.united(pointRect);
    }
    return bounds.normalized();
}

double medianPositiveLength(QVector<double> values)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) {
                     return !qIsFinite(v) || v <= 1.0e-7;
                 }), values.end());
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

bool hatchLoopHasUnsafeGap(const QVector<QPointF>& loop, double* maxGapOut = nullptr, double* typicalOut = nullptr)
{
    if (loop.size() < 3) return true;

    QVector<double> segments;
    segments.reserve(loop.size());
    double maxGap = 0.0;
    for (int i = 0; i < loop.size(); ++i) {
        const QPointF& a = loop.at(i);
        const QPointF& b = loop.at((i + 1) % loop.size());
        if (!qIsFinite(a.x()) || !qIsFinite(a.y()) || !qIsFinite(b.x()) || !qIsFinite(b.y())) return true;
        const double length = QLineF(a, b).length();
        segments.append(length);
        maxGap = std::max(maxGap, length);
    }

    const double typical = medianPositiveLength(segments);
    if (maxGapOut) *maxGapOut = maxGap;
    if (typicalOut) *typicalOut = typical;
    if (typical <= 1.0e-9) return true;

    const QRectF bounds = boundsForPointLoop(loop);
    const double diagonal = std::hypot(bounds.width(), bounds.height());
    const double closingGap = QLineF(loop.last(), loop.first()).length();

    // Several real-world DXF files contain HATCH paths with stale seed/origin points,
    // non-continuous edge paths, or failed associative boundaries. If we close these
    // paths blindly, Qt draws a very long segment across the drawing. LibreCAD/QCAD
    // usually reports these as "Hatch failed due to a gap" and avoids rendering them.
    // Keep normal large boundaries, but reject loops that contain one segment wildly
    // larger than the normal local edge spacing.
    const double hugeGapLimit = std::max(40.0, typical * 8.0);
    const bool hugeSegment = maxGap > hugeGapLimit;
    const bool hugeClosingGap = closingGap > std::max(25.0, typical * 5.0);

    // If the loop has only a few points, a long rectangle edge can be valid. Only mark it
    // unsafe when the long edge is also a major outlier compared with the bounding box.
    if (loop.size() <= 5 && diagonal > 1.0e-9 && maxGap <= diagonal * 1.05 && maxGap <= typical * 3.5)
        return false;

    return hugeSegment || hugeClosingGap;
}

QVector<QPointF> compactFiniteLoop(const QVector<QPointF>& loop)
{
    QVector<QPointF> out;
    out.reserve(loop.size());
    for (const QPointF& p : loop) {
        if (!qIsFinite(p.x()) || !qIsFinite(p.y())) continue;
        if (out.isEmpty() || QLineF(out.last(), p).length() > 1.0e-7) out.append(p);
    }
    while (out.size() > 2 && QLineF(out.first(), out.last()).length() < 1.0e-7)
        out.removeLast();
    return out;
}

QVector<QVector<QPointF>> sanitizeDxfHatchLoops(const QVector<QVector<QPointF>>& rawLoops, bool* rejectedUnsafeLoops = nullptr)
{
    QVector<QVector<QPointF>> loops;
    if (rejectedUnsafeLoops) *rejectedUnsafeLoops = false;

    for (const QVector<QPointF>& rawLoop : rawLoops) {
        QVector<QPointF> loop = compactFiniteLoop(rawLoop);
        if (loop.size() < 3) continue;

        double maxGap = 0.0;
        double typical = 0.0;
        if (hatchLoopHasUnsafeGap(loop, &maxGap, &typical)) {
            if (rejectedUnsafeLoops) *rejectedUnsafeLoops = true;

            // Conservative recovery: remove isolated origin/seed/outlier points and
            // re-test after each removal. Some DXF files contain more than one stale
            // HATCH point, so a single-pass fix is not enough. We never add geometry;
            // we only remove points that are most responsible for unsafe bridge edges.
            QVector<QPointF> recovered = loop;
            for (int pass = 0; pass < 12 && recovered.size() > 4; ++pass) {
                int worstIndex = -1;
                double worstScore = 0.0;
                for (int i = 0; i < recovered.size(); ++i) {
                    const QPointF& prev = recovered.at((i + recovered.size() - 1) % recovered.size());
                    const QPointF& cur = recovered.at(i);
                    const QPointF& next = recovered.at((i + 1) % recovered.size());
                    const double score = QLineF(prev, cur).length() + QLineF(cur, next).length();
                    if (score > worstScore) {
                        worstScore = score;
                        worstIndex = i;
                    }
                }
                if (worstIndex < 0) break;
                recovered.removeAt(worstIndex);
                recovered = compactFiniteLoop(recovered);
                if (recovered.size() >= 3 && !hatchLoopHasUnsafeGap(recovered)) {
                    loops.append(recovered);
                    recovered.clear();
                    break;
                }
            }
            if (recovered.isEmpty()) continue;

            // Invalid HATCH: do not render it as a closed filled path, because that creates
            // long false lines outside the hatch boundary.
            continue;
        }

        loops.append(loop);
    }

    return loops;
}

std::unique_ptr<CadEntity> entityFromRaw(const DxfRawEntity& raw,
                                         const QMap<QString, LayerInfo>& layers,
                                         const QHash<QString, DxfBlockDef>& blocks,
                                         const DxfTables* tables,
                                         bool* usedFallback)
{
    if (usedFallback) *usedFallback = false;
    const QString t = raw.type.toUpper();
    std::unique_ptr<CadEntity> entity;

    if (t == QStringLiteral("LINE")) {
        entity = std::make_unique<CadLine>(dxfPoint(raw.x1, raw.y1), dxfPoint(raw.x2, raw.y2));
    } else if (t == QStringLiteral("CIRCLE")) {
        if (raw.radius <= 0.0) return nullptr;
        entity = std::make_unique<CadCircle>(dxfPoint(raw.x1, raw.y1), raw.radius);
    } else if (t == QStringLiteral("ARC")) {
        if (raw.radius <= 0.0) return nullptr;
        const double span = normalizePositiveSpan(raw.endAngle - raw.startAngle);
        entity = std::make_unique<CadArc>(dxfPoint(raw.x1, raw.y1), raw.radius, raw.startAngle, span);
    } else if (t == QStringLiteral("LWPOLYLINE") || t == QStringLiteral("POLYLINE")) {
        if (!raw.loops.isEmpty()) {
            QVector<QPointF> boundary;
            for (const QVector<QPointF>& loop : raw.loops) {
                if (loop.size() > boundary.size()) boundary = loop;
            }
            if (boundary.size() >= 3) {
                entity = std::make_unique<CadPolyline>(boundary, true);
                if (usedFallback) *usedFallback = true;
            }
        } else {
            if (raw.points.size() < 2) return nullptr;
            const bool flagClosed = (raw.flags & 1) != 0;
            QVector<QPointF> expanded = expandBulges(raw.points, raw.bulges, flagClosed);
            const bool closed = flagClosed || looksLikeRevisionCloudPolyline(expanded);
            entity = std::make_unique<CadPolyline>(expanded, closed);
        }
    } else if (t == QStringLiteral("ELLIPSE")) {
        bool fullEllipse = false;
        QVector<QPointF> ellipse = approximateEllipsePoints(raw, &fullEllipse);
        if (ellipse.size() < 2) return nullptr;
        const bool axisAligned = std::abs(raw.axisY) <= 1.0e-9;
        if (fullEllipse && axisAligned) {
            const double a = std::hypot(raw.axisX, raw.axisY);
            const double b = a * (raw.ratio <= 0.0 ? 1.0 : raw.ratio);
            const QPointF center = dxfPoint(raw.x1, raw.y1);
            entity = std::make_unique<CadEllipse>(QRectF(center.x() - a, center.y() - b, 2.0 * a, 2.0 * b));
        } else {
            entity = std::make_unique<CadPolyline>(ellipse, fullEllipse);
            if (usedFallback) *usedFallback = true;
        }
    } else if (t == QStringLiteral("POINT")) {
        entity = std::make_unique<CadCircle>(dxfPoint(raw.x1, raw.y1), 1.0);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("SHAPE")) {
        auto shapeText = std::make_unique<CadText>(dxfPoint(raw.x1, raw.y1),
                                                   raw.blockName.trimmed().isEmpty() ? QStringLiteral("SHAPE") : raw.blockName.trimmed(),
                                                   normalizedTextHeight(raw.hasHeight ? std::abs(raw.height) : 2.5), raw.rotation);
        entity = std::move(shapeText);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("TOLERANCE") || t == QStringLiteral("ARCALIGNEDTEXT")) {
        const QString label = cleanDxfText(raw.text.isEmpty() ? raw.blockName : raw.text);
        if (label.isEmpty()) return nullptr;
        entity = std::make_unique<CadText>(dxfPoint(raw.x1, raw.y1), label, normalizedTextHeight(raw.hasHeight ? std::abs(raw.height) : 2.5), raw.rotation);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("TEXT") || t == QStringLiteral("MTEXT") || t == QStringLiteral("ATTRIB") || t == QStringLiteral("ATTDEF")) {
        const QString text = cleanDxfText(raw.text);
        if (text.isEmpty()) return nullptr;
        const DxfTextStyle* style = lookupTextStyle(tables, raw.textStyle);
        double styleHeight = style ? std::abs(style->fixedHeight) : 0.0;
        const double rawHeight = raw.hasHeight ? std::abs(raw.height) : 0.0;
        const double height = normalizedTextHeight(rawHeight > 1.0e-9 ? rawHeight : styleHeight);
        double widthFactor = normalizedWidthFactor(raw.textWidthFactor);
        if (style && isDefaultTextWidthFactor(widthFactor)) widthFactor = normalizedWidthFactor(style->widthFactor);
        double obliqueDeg = raw.textObliqueAngle;
        if (style && std::abs(obliqueDeg) <= 1.0e-9) obliqueDeg = style->obliqueAngleDeg;
        int generationFlags = raw.textGenerationFlags | (style ? style->generationFlags : 0);
        QPointF visualPos = dxfPoint(raw.x1, raw.y1);
        double angleDeg = raw.rotation;

        if (t == QStringLiteral("MTEXT")) {
            const QPointF direction(raw.x2, raw.y2);
            if (std::hypot(direction.x(), direction.y()) > 1.0e-9)
                angleDeg = std::atan2(direction.y(), direction.x()) * 180.0 / kPi;
            const int attachment = raw.mtextAttachmentPoint > 0 ? raw.mtextAttachmentPoint : 1;
            visualPos = lowerLeftFromMTextAnchor(visualPos, text, height, raw.mtextWidth,
                                                 angleDeg, attachment, raw.mtextLineSpacingFactor);
        } else {
            const QPointF alignPoint = dxfPoint(raw.x2, raw.y2);
            if ((raw.textHJustification == 3 || raw.textHJustification == 5) &&
                QLineF(visualPos, alignPoint).length() > 1.0e-9) {
                const double naturalWidth = estimatedTextWidth(text, height, 1.0);
                const double targetWidth = QLineF(visualPos, alignPoint).length();
                if (naturalWidth > 1.0e-9) widthFactor = qBound(0.01, targetWidth / naturalWidth, 100.0);
                angleDeg = cadAngleDeg(visualPos, alignPoint);
            } else if (raw.textHJustification != 0 || raw.textVJustification != 0) {
                visualPos = lowerLeftFromTextAnchor(alignPoint, text, height, widthFactor, angleDeg,
                                                    raw.textHJustification, raw.textVJustification);
            }
            if (generationFlags & 4) angleDeg += 180.0;
        }

        auto textEntity = std::make_unique<CadText>(visualPos, text, height, angleDeg);
        textEntity->setWidthFactor(widthFactor);
        textEntity->setObliqueAngleDeg(obliqueDeg);
        textEntity->setFontName(resolvedTextFontName(tables, raw.textStyle));
        if (t == QStringLiteral("MTEXT")) {
            const int ap = raw.mtextAttachmentPoint;
            textEntity->setMText(true);
            textEntity->setBoxWidth(raw.mtextWidth);
            textEntity->setLineSpacingFactor(raw.mtextLineSpacingFactor);
            textEntity->setHorizontalJustification((ap == 2 || ap == 5 || ap == 8) ? 1 : (ap == 3 || ap == 6 || ap == 9) ? 2 : 0);
            textEntity->setVerticalJustification((ap >= 1 && ap <= 3) ? 3 : (ap >= 4 && ap <= 6) ? 2 : 1);
        } else {
            textEntity->setHorizontalJustification(raw.textHJustification);
            textEntity->setVerticalJustification(raw.textVJustification);
        }
        entity = std::move(textEntity);
    } else if (t == QStringLiteral("INSERT") || t == QStringLiteral("MINSERT")) {
        const QString blockName = raw.blockName.trimmed();
        const QString blockKey = blockLookupKey(blockName, blocks);
        const DxfBlockDef block = blockKey.isEmpty() ? DxfBlockDef() : blocks.value(blockKey);
        const double scale = (std::abs(raw.scaleX) + std::abs(raw.scaleY)) * 0.5;
        entity = std::make_unique<CadBlockReference>(
            blockName.isEmpty() ? QStringLiteral("DXF_INSERT") : blockName,
            block.basePoint,
            dxfPoint(raw.x1, raw.y1),
            block.previewEntities,
            std::abs(scale) <= 1e-12 ? 1.0 : scale,
            raw.rotation);
    } else if (t == QStringLiteral("SPLINE")) {
        if (raw.points.size() < 2 && raw.splineFitPoints.size() < 2) return nullptr;
        QVector<QPointF> sampled = approximateSplinePoints(raw);
        if (sampled.size() < 2) sampled = raw.points.size() >= 2 ? raw.points : raw.splineFitPoints;
        const bool closed = ((raw.flags & 1) != 0) || looksLikeRevisionCloudPolyline(sampled);
        entity = std::make_unique<CadPolyline>(sampled, closed);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("HATCH")) {
        QVector<QVector<QPointF>> rawLoops;
        for (const QVector<QPointF>& loop : raw.loops) {
            if (loop.size() >= 3) rawLoops.append(loop);
        }
        if (rawLoops.isEmpty() && raw.points.size() >= 3) rawLoops.append(raw.points);

        bool rejectedUnsafeHatchLoop = false;
        QVector<QVector<QPointF>> loops = sanitizeDxfHatchLoops(rawLoops, &rejectedUnsafeHatchLoop);
        if (loops.isEmpty()) {
            if (usedFallback) *usedFallback = true;
            return nullptr;
        }

        entity = std::make_unique<CadHatch>(loops, raw.solidFill ? QStringLiteral("SOLID") : raw.hatchPattern,
                                            raw.hatchScale > 1.0e-9 ? raw.hatchScale : 1.0, raw.hatchAngle);
        if (usedFallback) *usedFallback = raw.associative || loops.size() > 1 || rejectedUnsafeHatchLoop;
    } else if (t == QStringLiteral("DIMENSION")) {
        const QString dimBlockKey = blockLookupKey(raw.blockName, blocks);
        if (!raw.blockName.trimmed().isEmpty() && !dimBlockKey.isEmpty()) {
            const DxfBlockDef block = blocks.value(dimBlockKey);
            if (dimensionBlockLooksOversizedAgainstReferences(raw, block)) {
                // Some DXF producers write anonymous dimension blocks with stale/extreme graphics.
                // Use a compact style-based fallback instead of drawing huge text/arrows over the plan.
                entity = makeFallbackDimensionEntity(raw, tables);
                if (usedFallback) *usedFallback = true;
            } else if (dimensionBlockLooksAbsoluteInModelSpace(raw, block)) {
                entity = std::make_unique<CadBlockReference>(raw.blockName.trimmed(), QPointF(0.0, 0.0), QPointF(0.0, 0.0), block.previewEntities, 1.0, 0.0);
                if (usedFallback) *usedFallback = true;
            } else {
                entity = std::make_unique<CadBlockReference>(raw.blockName.trimmed(), block.basePoint, dxfPoint(raw.x1, raw.y1), block.previewEntities, 1.0, raw.rotation);
            }
        } else {
            // Linear/rotated DIMENSION entities store extension origins in 13/23 and 14/24,
            // while 10/20 is the dimension line definition point and 11/21 is often the text point.
            // Use DIMSTYLE/header variables and guard against oversized annotation in model space.
            entity = makeFallbackDimensionEntity(raw, tables);
            if (usedFallback) *usedFallback = true;
        }
    } else if (t == QStringLiteral("MLINE") || t == QStringLiteral("HELIX")) {
        QVector<QPointF> pts = raw.points;
        if (t == QStringLiteral("MLINE") && pts.size() == 1) pts.prepend(dxfPoint(raw.x1, raw.y1));
        if (pts.size() < 2 && (std::abs(raw.x2) > 1.0e-12 || std::abs(raw.y2) > 1.0e-12))
            pts << dxfPoint(raw.x1, raw.y1) << dxfPoint(raw.x2, raw.y2);
        if (pts.size() >= 2) entity = std::make_unique<CadPolyline>(pts, false);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("LEADER") || t == QStringLiteral("MLEADER")) {
        const QString leaderText = cleanDxfText(raw.text);
        const double leaderHeight = normalizedTextHeight(raw.hasHeight ? std::abs(raw.height) : 2.5);
        if (raw.points.size() >= 2) {
            entity = std::make_unique<CadLeader>(raw.points.first(), raw.points.last(), leaderText, leaderHeight);
        } else {
            entity = std::make_unique<CadLeader>(dxfPoint(raw.x1, raw.y1), dxfPoint(raw.x2, raw.y2), leaderText, leaderHeight);
        }
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("SOLID") || t == QStringLiteral("TRACE")) {
        QVector<QPointF> pts;
        pts << dxfPoint(raw.x1, raw.y1) << dxfPoint(raw.x2, raw.y2);
        if (std::abs(raw.x3) > 1e-12 || std::abs(raw.y3) > 1e-12) pts << dxfPoint(raw.x3, raw.y3);
        if (std::abs(raw.x4) > 1e-12 || std::abs(raw.y4) > 1e-12) pts << dxfPoint(raw.x4, raw.y4);
        for (const QPointF& p : raw.points) pts << p;
        if (pts.size() >= 3) entity = std::make_unique<CadHatch>(pts, QStringLiteral("SOLID"), 1.0, 0.0);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("3DFACE")) {
        QVector<QPointF> pts;
        pts << dxfPoint(raw.x1, raw.y1) << dxfPoint(raw.x2, raw.y2);
        if (std::abs(raw.x3) > 1e-12 || std::abs(raw.y3) > 1e-12) pts << dxfPoint(raw.x3, raw.y3);
        if (std::abs(raw.x4) > 1e-12 || std::abs(raw.y4) > 1e-12) pts << dxfPoint(raw.x4, raw.y4);
        for (const QPointF& p : raw.points) pts << p;
        if (pts.size() >= 3) entity = std::make_unique<CadPolyline>(pts, true);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("XLINE") || t == QStringLiteral("RAY")) {
        QPointF p = dxfPoint(raw.x1, raw.y1);
        QPointF v(raw.x2, raw.y2);
        if (std::hypot(v.x(), v.y()) < 1e-12) v = QPointF(1, 0);
        const double len = 100000.0;
        const double norm = std::hypot(v.x(), v.y());
        v = QPointF(v.x() / norm, v.y() / norm);
        entity = std::make_unique<CadLine>(t == QStringLiteral("RAY") ? p : p - v * len, p + v * len);
        if (usedFallback) *usedFallback = true;
    } else if (t == QStringLiteral("IMAGE") || t == QStringLiteral("VIEWPORT") || t == QStringLiteral("WIPEOUT")) {
        const QPointF p = dxfPoint(raw.x1, raw.y1);
        const double w = std::abs(raw.x2) > 1e-12 ? std::abs(raw.x2) : 100.0;
        const double h = std::abs(raw.y2) > 1e-12 ? std::abs(raw.y2) : 70.0;
        entity = std::make_unique<CadRectangle>(QRectF(p.x(), p.y(), w, h));
        if (usedFallback) *usedFallback = true;
    }

    if (entity) applyCommon(entity.get(), raw, layers);
    return entity;
}




void ensureLayerInDocument(const QString& name, const QMap<QString, LayerInfo>& layers, CadDocument& document);

bool isDefaultExtrusion(const DxfRawEntity& raw)
{
    return std::abs(raw.extrusionX) <= 1e-12 &&
           std::abs(raw.extrusionY) <= 1e-12 &&
           std::abs(raw.extrusionZ - 1.0) <= 1e-12;
}

QPointF ocsToWcs2D(double x, double y, double z, double ex, double ey, double ez)
{
    // DXF OCS arbitrary-axis algorithm, projected back to the viewer 2D plane.
    // This handles the common case where entities are authored in a rotated OCS.
    QVector3D az(ex, ey, ez);
    if (az.lengthSquared() <= 1e-20) az = QVector3D(0.0f, 0.0f, 1.0f);
    az.normalize();
    const QVector3D worldZ(0.0f, 0.0f, 1.0f);
    const QVector3D worldY(0.0f, 1.0f, 0.0f);
    QVector3D ax;
    if (std::abs(az.x()) < 1.0 / 64.0 && std::abs(az.y()) < 1.0 / 64.0)
        ax = QVector3D::crossProduct(worldY, az);
    else
        ax = QVector3D::crossProduct(worldZ, az);
    if (ax.lengthSquared() <= 1e-20) ax = QVector3D(1.0f, 0.0f, 0.0f);
    ax.normalize();
    QVector3D ay = QVector3D::crossProduct(az, ax);
    ay.normalize();
    const QVector3D w = ax * float(x) + ay * float(y) + az * float(z);
    return QPointF(double(w.x()), double(w.y()));
}

void applyOcsToRawEntity(DxfRawEntity& raw, DxfStats* stats)
{
    if (isDefaultExtrusion(raw)) return;
    auto tx = [&](double& x, double& y, double z) {
        const QPointF p = ocsToWcs2D(x, y, z, raw.extrusionX, raw.extrusionY, raw.extrusionZ);
        x = p.x();
        y = p.y();
    };
    tx(raw.x1, raw.y1, raw.z1);
    tx(raw.x2, raw.y2, raw.z2);
    tx(raw.x3, raw.y3, raw.z3);
    tx(raw.x4, raw.y4, raw.z4);
    for (QPointF& p : raw.points) {
        const QPointF w = ocsToWcs2D(p.x(), -p.y(), 0.0, raw.extrusionX, raw.extrusionY, raw.extrusionZ);
        p = dxfPoint(w.x(), w.y());
    }
    for (QPointF& p : raw.splineFitPoints) {
        const QPointF w = ocsToWcs2D(p.x(), -p.y(), 0.0, raw.extrusionX, raw.extrusionY, raw.extrusionZ);
        p = dxfPoint(w.x(), w.y());
    }
    for (QVector<QPointF>& loop : raw.loops) {
        for (QPointF& p : loop) {
            const QPointF w = ocsToWcs2D(p.x(), -p.y(), 0.0, raw.extrusionX, raw.extrusionY, raw.extrusionZ);
            p = dxfPoint(w.x(), w.y());
        }
    }
    raw.extrusionX = 0.0;
    raw.extrusionY = 0.0;
    raw.extrusionZ = 1.0;
    if (stats) ++stats->ocsTransformedEntities;
}

int estimatedVirtualEntityCount(const DxfRawEntity& insertRaw, const DxfBlockDef& block)
{
    const qint64 rows = std::max<qint64>(1, static_cast<qint64>(insertRaw.rowCount));
    const qint64 cols = std::max<qint64>(1, static_cast<qint64>(insertRaw.colCount));
    const qint64 entityCount = std::max<qint64>(1, static_cast<qint64>(block.rawEntities.size()));
    const qint64 estimate = rows * cols * entityCount;
    return estimate > std::numeric_limits<int>::max()
        ? std::numeric_limits<int>::max()
        : static_cast<int>(estimate);
}

bool shouldUseVirtualBlockInstance(const DxfRawEntity& insertRaw,
                                   const DxfBlockDef& block,
                                   const DxfStats& stats)
{
    if (qEnvironmentVariableIsSet("DWGVIEWER_DXF_FORCE_EXPLODE_BLOCKS")) return false;
    if (qEnvironmentVariableIsSet("DWGVIEWER_DXF_FORCE_VIRTUAL_BLOCKS")) return true;
    const int estimate = estimatedVirtualEntityCount(insertRaw, block);
    if (estimate >= kVirtualBlockPerInsertThreshold) return true;
    if (stats.imported + stats.expandedBlockEntities + estimate >= kVirtualBlockEntityThreshold) return true;
    if (insertRaw.rowCount * insertRaw.colCount > 128) return true;
    return false;
}

bool appendVirtualBlockReference(const DxfRawEntity& insertRaw,
                                 CadDocument& document,
                                 QMap<QString, LayerInfo>& layers,
                                 const DxfBlockDef& block,
                                 DxfStats& stats,
                                 QRectF& bounds)
{
    ensureLayerInDocument(insertRaw.layer, layers, document);
    QJsonArray preview = block.previewEntities;
    if (preview.size() > kMaxVirtualBlockPreviewEntities) {
        QJsonArray reduced;
        for (int i = 0; i < preview.size() && i < kMaxVirtualBlockPreviewEntities; ++i)
            reduced.append(preview.at(i));
        preview = reduced;
    }
    const double scale = (std::abs(insertRaw.scaleX) + std::abs(insertRaw.scaleY)) * 0.5;
    auto ref = std::make_unique<CadBlockReference>(
        insertRaw.blockName.trimmed().isEmpty() ? QStringLiteral("DXF_BLOCK") : insertRaw.blockName.trimmed(),
        block.basePoint,
        dxfPoint(insertRaw.x1, insertRaw.y1),
        preview,
        std::abs(scale) <= 1e-12 ? 1.0 : scale,
        insertRaw.rotation);
    applyCommon(ref.get(), insertRaw, layers);
    extendBoundsFromEntity(ref.get(), bounds);
    document.addEntity(std::move(ref));
    ++stats.imported;
    ++stats.virtualBlockRefs;
    stats.virtualBlockEntities += estimatedVirtualEntityCount(insertRaw, block);
    ++stats.importedByType[QStringLiteral("VIRTUAL_INSERT")];
    return true;
}

void ensureLayerInDocument(const QString& name, const QMap<QString, LayerInfo>& layers, CadDocument& document);

QPointF transformScenePointForInsert(const QPointF& p,
                                     const QPointF& base,
                                     const QPointF& insert,
                                     double sx,
                                     double sy,
                                     double rotationDeg)
{
    const double a = -rotationDeg * kPi / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    const double x = (p.x() - base.x()) * sx;
    const double y = (p.y() - base.y()) * sy;
    return QPointF(insert.x() + x * c - y * s,
                   insert.y() + x * s + y * c);
}

QPointF transformVectorForInsert(const QPointF& v, double sx, double sy, double rotationDeg)
{
    const double a = -rotationDeg * kPi / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    const double x = v.x() * sx;
    const double y = v.y() * sy;
    return QPointF(x * c - y * s, x * s + y * c);
}

void setRawXYFromScene(DxfRawEntity& raw, int index, const QPointF& p)
{
    // Raw x/y fields remain in DXF model coordinates because entityFromRaw() applies dxfPoint().
    const double modelY = -p.y();
    if (index == 1) { raw.x1 = p.x(); raw.y1 = modelY; }
    else if (index == 2) { raw.x2 = p.x(); raw.y2 = modelY; }
    else if (index == 3) { raw.x3 = p.x(); raw.y3 = modelY; }
    else if (index == 4) { raw.x4 = p.x(); raw.y4 = modelY; }
}

bool nearlyUniformScale(double sx, double sy)
{
    const double ax = std::abs(sx);
    const double ay = std::abs(sy);
    return std::abs(ax - ay) <= std::max(1e-9, std::max(ax, ay) * 1e-6);
}

QVector<QPointF> transformedCircleLikePoints(const DxfRawEntity& raw,
                                             const QPointF& base,
                                             const QPointF& insert,
                                             double sx,
                                             double sy,
                                             double rotationDeg,
                                             bool arcOnly)
{
    QVector<QPointF> pts;
    if (raw.radius <= 0.0) return pts;
    double start = arcOnly ? raw.startAngle : 0.0;
    double end = arcOnly ? raw.endAngle : 360.0;
    double span = arcOnly ? normalizePositiveSpan(end - start) : 360.0;
    const int segments = std::max(24, std::min(128, int(std::ceil(std::abs(span) / 6.0))));
    const QPointF c = dxfPoint(raw.x1, raw.y1);
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double deg = start + span * double(i) / double(segments);
        const double a = deg * kPi / 180.0;
        const QPointF local(cadPolarPoint(c, raw.radius, deg));
        pts.append(transformScenePointForInsert(local, base, insert, sx, sy, rotationDeg));
    }
    return pts;
}

DxfRawEntity transformRawForInsert(const DxfRawEntity& src,
                                   const DxfBlockDef& block,
                                   const DxfRawEntity& insertRaw,
                                   const QPointF& insertPoint)
{
    DxfRawEntity out = src;
    const double sx = std::abs(insertRaw.scaleX) <= 1e-12 ? 1.0 : insertRaw.scaleX;
    const double sy = std::abs(insertRaw.scaleY) <= 1e-12 ? 1.0 : insertRaw.scaleY;
    const double scaleAvg = (std::abs(sx) + std::abs(sy)) * 0.5;
    const bool uniform = nearlyUniformScale(sx, sy);

    if (out.layer.trimmed().isEmpty() || out.layer == QStringLiteral("0")) out.layer = cleanLayerName(insertRaw.layer);
    if (out.colorIndex == 0 || out.colorIndex == 256) {
        if (insertRaw.colorIndex != 256) out.colorIndex = insertRaw.colorIndex;
        if (insertRaw.trueColor >= 0) out.trueColor = insertRaw.trueColor;
    }
    if (out.lineType.compare(QStringLiteral("BYBLOCK"), Qt::CaseInsensitive) == 0 ||
        out.lineType.compare(QStringLiteral("BYLAYER"), Qt::CaseInsensitive) == 0) {
        out.lineType = insertRaw.lineType;
    }

    if (out.type == QStringLiteral("CIRCLE") && !uniform) {
        out.type = QStringLiteral("LWPOLYLINE");
        out.points = transformedCircleLikePoints(src, block.basePoint, insertPoint, sx, sy, insertRaw.rotation, false);
        out.bulges.clear();
        out.flags |= 1;
        return out;
    }
    if (out.type == QStringLiteral("ARC") && !uniform) {
        out.type = QStringLiteral("LWPOLYLINE");
        out.points = transformedCircleLikePoints(src, block.basePoint, insertPoint, sx, sy, insertRaw.rotation, true);
        out.bulges.clear();
        out.flags &= ~1;
        return out;
    }

    const QPointF p1 = transformScenePointForInsert(dxfPoint(src.x1, src.y1), block.basePoint, insertPoint, sx, sy, insertRaw.rotation);
    const QPointF p2 = transformScenePointForInsert(dxfPoint(src.x2, src.y2), block.basePoint, insertPoint, sx, sy, insertRaw.rotation);
    const QPointF p3 = transformScenePointForInsert(dxfPoint(src.x3, src.y3), block.basePoint, insertPoint, sx, sy, insertRaw.rotation);
    const QPointF p4 = transformScenePointForInsert(dxfPoint(src.x4, src.y4), block.basePoint, insertPoint, sx, sy, insertRaw.rotation);
    setRawXYFromScene(out, 1, p1);
    setRawXYFromScene(out, 2, p2);
    setRawXYFromScene(out, 3, p3);
    setRawXYFromScene(out, 4, p4);

    if (out.type == QStringLiteral("CIRCLE") || out.type == QStringLiteral("ARC")) out.radius *= scaleAvg;
    if (out.type == QStringLiteral("ARC")) {
        out.startAngle += insertRaw.rotation;
        out.endAngle += insertRaw.rotation;
    }
    if (out.type == QStringLiteral("TEXT") || out.type == QStringLiteral("MTEXT") || out.type == QStringLiteral("ATTRIB") || out.type == QStringLiteral("ATTDEF")) {
        out.height *= scaleAvg;
        out.rotation += insertRaw.rotation;
    }
    if (out.type == QStringLiteral("INSERT") || out.type == QStringLiteral("MINSERT")) {
        out.scaleX *= sx;
        out.scaleY *= sy;
        out.rotation += insertRaw.rotation;
    }

    if (out.type == QStringLiteral("ELLIPSE")) {
        const QPointF axis = transformVectorForInsert(dxfVector(src.axisX, src.axisY), sx, sy, insertRaw.rotation);
        out.axisX = axis.x();
        out.axisY = -axis.y();
        out.x2 = axis.x();
        out.y2 = -axis.y();
    }

    for (QPointF& p : out.points) p = transformScenePointForInsert(p, block.basePoint, insertPoint, sx, sy, insertRaw.rotation);
    for (QPointF& p : out.splineFitPoints) p = transformScenePointForInsert(p, block.basePoint, insertPoint, sx, sy, insertRaw.rotation);
    for (QVector<QPointF>& loop : out.loops) {
        for (QPointF& p : loop) p = transformScenePointForInsert(p, block.basePoint, insertPoint, sx, sy, insertRaw.rotation);
    }
    if (!uniform) out.bulges.clear();
    return out;
}

bool appendEntityFromRaw(const DxfRawEntity& raw,
                         CadDocument& document,
                         QMap<QString, LayerInfo>& layers,
                         const QHash<QString, DxfBlockDef>& blocks,
                         const DxfTables& tables,
                         DxfStats& stats,
                         QRectF& bounds,
                         bool fromExpandedBlock)
{
    DxfRawEntity drawableRaw = raw;
    applyOcsToRawEntity(drawableRaw, &stats);
    ensureLayerInDocument(drawableRaw.layer, layers, document);
    bool usedFallback = false;
    auto entity = entityFromRaw(drawableRaw, layers, blocks, &tables, &usedFallback);
    if (!entity) {
        ++stats.ignored;
        ++stats.invalidGeometry;
        return false;
    }
    extendBoundsFromEntity(entity.get(), bounds);
    document.addEntity(std::move(entity));
    ++stats.imported;
    ++stats.importedByType[drawableRaw.type.toUpper()];
    if (usedFallback) ++stats.fallback;
    if (fromExpandedBlock) ++stats.expandedBlockEntities;
    return true;
}

bool expandBlockInsert(const DxfRawEntity& insertRaw,
                       CadDocument& document,
                       QMap<QString, LayerInfo>& layers,
                       const QHash<QString, DxfBlockDef>& blocks,
                       const DxfTables& tables,
                       DxfStats& stats,
                       QRectF& bounds,
                       int depth)
{
    if (depth > kMaxBlockExpandDepth) {
        ++stats.blockDepthLimitHits;
        return false;
    }
    const QString blockName = insertRaw.blockName.trimmed();
    const QString blockKey = blockLookupKey(blockName, blocks);
    if (blockName.isEmpty() || blockKey.isEmpty()) return false;
    const DxfBlockDef block = blocks.value(blockKey);
    if (block.rawEntities.isEmpty()) return false;

    bool any = false;
    const int rows = std::max(1, insertRaw.rowCount);
    const int cols = std::max(1, insertRaw.colCount);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (stats.expandedBlockEntities >= kMaxExpandedBlockEntities) {
                ++stats.blockDepthLimitHits;
                return any;
            }
            DxfRawEntity adjustedInsert = insertRaw;
            const QPointF insertPoint = dxfPoint(insertRaw.x1 + double(col) * insertRaw.colSpacing,
                                                 insertRaw.y1 + double(row) * insertRaw.rowSpacing);
            ++stats.expandedBlockRefs;
            for (const DxfRawEntity& child : block.rawEntities) {
                DxfRawEntity transformed = transformRawForInsert(child, block, adjustedInsert, insertPoint);
                const QString childType = transformed.type.toUpper();
                if ((childType == QStringLiteral("INSERT") || childType == QStringLiteral("MINSERT")) &&
                    !blockLookupKey(transformed.blockName, blocks).isEmpty() &&
                    expandBlockInsert(transformed, document, layers, blocks, tables, stats, bounds, depth + 1)) {
                    any = true;
                } else if (appendEntityFromRaw(transformed, document, layers, blocks, tables, stats, bounds, true)) {
                    any = true;
                }
            }
        }
    }
    return any;
}

void ensureLayerInDocument(const QString& name, const QMap<QString, LayerInfo>& layers, CadDocument& document)
{
    const QString layerName = cleanLayerName(name);
    document.ensureLayer(layerName);
    if (layers.contains(layerName)) {
        document.layers()[layerName].color = layers.value(layerName).color;
        document.layers()[layerName].visible = layers.value(layerName).visible;
    }
}

void parseHeaderSection(DxfPairReader& reader, DxfHeader& header, CadDocument& document, DxfTables& tables)
{
    QString currentVar;
    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0 && p.value.compare(QStringLiteral("ENDSEC"), Qt::CaseInsensitive) == 0) break;
        if (p.code == 9) {
            currentVar = p.value.toUpper();
            continue;
        }
        if (currentVar == QStringLiteral("$ACADVER") && p.code == 1) header.acadVersion = p.value;
        else if (currentVar == QStringLiteral("$HANDSEED") && p.code == 5) header.handSeed = p.value;
        else if (currentVar == QStringLiteral("$MEASUREMENT") && p.code == 70) header.measurement = p.value.toInt();
        else if (currentVar == QStringLiteral("$PDMODE") && p.code == 70) header.pdMode = p.value.toInt();
        else if (currentVar == QStringLiteral("$PDSIZE") && p.code == 40) header.pdSize = p.value.toDouble();
        else if (currentVar == QStringLiteral("$INSUNITS") && p.code == 70) {
            header.insUnits = p.value.toInt();
            if (header.insUnits == 4) document.setUnit(CadDocument::Unit::Millimeter);
            else if (header.insUnits == 5) document.setUnit(CadDocument::Unit::Centimeter);
            else if (header.insUnits == 6) document.setUnit(CadDocument::Unit::Meter);
            else document.setUnit(CadDocument::Unit::Unitless);
        } else if (currentVar == QStringLiteral("$DIMSTYLE") && p.code == 2) {
            tables.headerDimStyle.name = p.value.trimmed().isEmpty() ? QStringLiteral("Standard") : p.value.trimmed();
        } else if (currentVar == QStringLiteral("$DIMSCALE") && p.code == 40) {
            const double value = std::abs(p.value.toDouble());
            tables.headerDimStyle.overallScale = value > 1.0e-12 ? value : 1.0;
        } else if (currentVar == QStringLiteral("$DIMASZ") && p.code == 40) {
            tables.headerDimStyle.arrowSize = std::abs(p.value.toDouble());
            tables.headerDimStyle.hasArrowSize = tables.headerDimStyle.arrowSize > 1.0e-12;
        } else if (currentVar == QStringLiteral("$DIMTXT") && p.code == 40) {
            tables.headerDimStyle.textHeight = std::abs(p.value.toDouble());
            tables.headerDimStyle.hasTextHeight = tables.headerDimStyle.textHeight > 1.0e-12;
        } else if (currentVar == QStringLiteral("$DIMEXO") && p.code == 40) {
            tables.headerDimStyle.extensionOffset = std::abs(p.value.toDouble());
        } else if (currentVar == QStringLiteral("$DIMEXE") && p.code == 40) {
            tables.headerDimStyle.extensionExtend = std::abs(p.value.toDouble());
        } else if (currentVar == QStringLiteral("$DIMLFAC") && p.code == 40) {
            const double value = p.value.toDouble();
            tables.headerDimStyle.linearScaleFactor = std::abs(value) > 1.0e-12 ? value : 1.0;
        } else if (currentVar == QStringLiteral("$DIMGAP") && p.code == 40) {
            tables.headerDimStyle.textGap = std::abs(p.value.toDouble());
        } else if (currentVar == QStringLiteral("$EXTMIN")) {
            if (p.code == 10) { header.extMin.setX(p.value.toDouble()); header.hasExtMin = true; }
            else if (p.code == 20) header.extMin.setY(dxfY(p.value.toDouble()));
        } else if (currentVar == QStringLiteral("$EXTMAX")) {
            if (p.code == 10) { header.extMax.setX(p.value.toDouble()); header.hasExtMax = true; }
            else if (p.code == 20) header.extMax.setY(dxfY(p.value.toDouble()));
        }
    }
    if (header.hasExtMin && header.hasExtMax) header.extents = QRectF(header.extMin, header.extMax).normalized();
}

void parseLayerTable(DxfPairReader& reader, QMap<QString, LayerInfo>& layers, CadDocument& document)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDTAB")) break;
        if (type != QStringLiteral("LAYER")) continue;

        LayerInfo info;
        info.name = QStringLiteral("0");
        info.color = QColor(Qt::white);
        info.visible = true;
        int flags = 0;
        int colorIndex = 7;
        int trueColor = -1;
        int lineWeight = -1;
        QString lineType = QStringLiteral("Continuous");

        while (reader.next(p)) {
            if (p.code == 0) { reader.unread(p); break; }
            if (p.code == 2) info.name = cleanLayerName(p.value);
            else if (p.code == 6) lineType = normalizeLineType(p.value);
            else if (p.code == 62) colorIndex = p.value.toInt();
            else if (p.code == 70) flags = p.value.toInt();
            else if (p.code == 370) lineWeight = p.value.toInt();
            else if (p.code == 420) trueColor = p.value.toInt();
        }

        info.color = trueColor >= 0 ? QColor((trueColor >> 16) & 0xff, (trueColor >> 8) & 0xff, trueColor & 0xff) : aciColor(colorIndex);
        info.visible = colorIndex >= 0 && (flags & 1) == 0;
        layers.insert(info.name, info);
        document.ensureLayer(info.name);
        document.layers()[info.name].color = info.color;
        document.layers()[info.name].visible = info.visible;
        document.layers()[info.name].lineType = lineType;
        if (lineWeight > 0) document.layers()[info.name].lineWeight = double(lineWeight) / 100.0;
    }
}


void parseClassesSection(DxfPairReader& reader, DxfStats& stats)
{
    // AutoCAD 2000+ DXF files may contain a CLASSES section before TABLES.
    // The viewer does not need to instantiate custom classes here, but the
    // parser must consume the section exactly so the following TABLES/BLOCKS/
    // ENTITIES sections remain aligned. Count CLASS records for diagnostics.
    DxfPair p;
    bool inClass = false;
    while (reader.next(p)) {
        if (p.code == 0) {
            const QString token = p.value.toUpper();
            if (token == QStringLiteral("ENDSEC")) {
                if (inClass) ++stats.classes;
                break;
            }
            if (token == QStringLiteral("CLASS")) {
                if (inClass) ++stats.classes;
                inClass = true;
                continue;
            }
            // Unknown records in CLASSES are skipped safely but do not leak
            // into the outer section parser.
            if (inClass) ++stats.classes;
            inClass = false;
        }
    }
}

void parseLineTypeTable(DxfPairReader& reader, DxfTables& tables)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDTAB")) break;
        if (type != QStringLiteral("LTYPE")) continue;
        DxfLineType lt;
        while (reader.next(p)) {
            if (p.code == 0) { reader.unread(p); break; }
            if (p.code == 2) lt.name = p.value;
            else if (p.code == 3) lt.description = p.value;
            else if (p.code == 49) lt.pattern.append(p.value.toDouble());
        }
        tables.lineTypes.insert(lt.name.toUpper(), lt);
    }
}

void parseStyleTable(DxfPairReader& reader, DxfTables& tables)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDTAB")) break;
        if (type != QStringLiteral("STYLE")) continue;
        DxfTextStyle st;
        while (reader.next(p)) {
            if (p.code == 0) { reader.unread(p); break; }
            if (p.code == 2) st.name = p.value;
            else if (p.code == 3) st.font = p.value;
            else if (p.code == 40) st.fixedHeight = p.value.toDouble();
            else if (p.code == 41) st.widthFactor = normalizedWidthFactor(p.value.toDouble());
            else if (p.code == 50) st.obliqueAngleDeg = p.value.toDouble();
            else if (p.code == 71) st.generationFlags = p.value.toInt();
            else if (p.code == 42 && st.fixedHeight <= 1.0e-9) st.fixedHeight = p.value.toDouble();
        }
        tables.textStyles.insert(st.name.toUpper(), st);
    }
}

void parseDimStyleTable(DxfPairReader& reader, DxfTables& tables)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDTAB")) break;
        if (type != QStringLiteral("DIMSTYLE")) continue;

        DxfDimStyle style = tables.headerDimStyle;
        style.name = QStringLiteral("Standard");
        while (reader.next(p)) {
            if (p.code == 0) { reader.unread(p); break; }
            const double d = p.value.toDouble();
            if (p.code == 2) style.name = p.value.trimmed().isEmpty() ? QStringLiteral("Standard") : p.value.trimmed();
            else if (p.code == 40) style.overallScale = std::abs(d) > 1.0e-12 ? std::abs(d) : 1.0;
            else if (p.code == 41) { style.arrowSize = std::abs(d); style.hasArrowSize = style.arrowSize > 1.0e-12; }
            else if (p.code == 42) style.extensionOffset = std::abs(d);
            else if (p.code == 44) style.extensionExtend = std::abs(d);
            else if (p.code == 140) { style.textHeight = std::abs(d); style.hasTextHeight = style.textHeight > 1.0e-12; }
            else if (p.code == 144) style.linearScaleFactor = std::abs(d) > 1.0e-12 ? d : 1.0;
            else if (p.code == 147) style.textGap = std::abs(d);
        }
        if (style.name.trimmed().isEmpty()) style.name = QStringLiteral("Standard");
        tables.dimStyles.insert(style.name.toUpper(), style);
    }
}

void parseAppIdTable(DxfPairReader& reader, DxfTables& tables)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDTAB")) break;
        if (type != QStringLiteral("APPID")) continue;
        QString name;
        while (reader.next(p)) {
            if (p.code == 0) { reader.unread(p); break; }
            if (p.code == 2) name = p.value.trimmed().toUpper();
        }
        if (!name.isEmpty()) tables.appIds.insert(name);
    }
}

void parseBlockRecordTable(DxfPairReader& reader, DxfStats* stats)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDTAB")) break;
        if (type != QStringLiteral("BLOCK_RECORD")) continue;
        if (stats) ++stats->blockRecords;
        while (reader.next(p)) {
            if (p.code == 0) { reader.unread(p); break; }
        }
    }
}

void parseViewLikeTable(DxfPairReader& reader)
{
    // VIEW, VPORT and UCS tables are important in AutoCAD 2012 DXF, but the current
    // 2D model does not store named views/UCS. We parse/skip them safely instead
    // of letting their group codes leak into the next table.
    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0 && p.value.compare(QStringLiteral("ENDTAB"), Qt::CaseInsensitive) == 0) break;
    }
}

void skipUntilEndTab(DxfPairReader& reader)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0 && p.value.compare(QStringLiteral("ENDTAB"), Qt::CaseInsensitive) == 0) break;
    }
}

void parseTablesSection(DxfPairReader& reader, QMap<QString, LayerInfo>& layers, CadDocument& document, DxfTables& tables, DxfStats* stats)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0 && p.value.compare(QStringLiteral("ENDSEC"), Qt::CaseInsensitive) == 0) break;
        if (p.code == 0 && p.value.compare(QStringLiteral("TABLE"), Qt::CaseInsensitive) == 0) {
            DxfPair namePair;
            if (!reader.next(namePair)) break;
            const QString tableName = namePair.value.toUpper();
            if (namePair.code == 2 && tableName == QStringLiteral("LAYER")) parseLayerTable(reader, layers, document);
            else if (namePair.code == 2 && tableName == QStringLiteral("LTYPE")) parseLineTypeTable(reader, tables);
            else if (namePair.code == 2 && tableName == QStringLiteral("STYLE")) parseStyleTable(reader, tables);
            else if (namePair.code == 2 && tableName == QStringLiteral("DIMSTYLE")) parseDimStyleTable(reader, tables);
            else if (namePair.code == 2 && tableName == QStringLiteral("APPID")) parseAppIdTable(reader, tables);
            else if (namePair.code == 2 && tableName == QStringLiteral("BLOCK_RECORD")) parseBlockRecordTable(reader, stats);
            else if (namePair.code == 2 && (tableName == QStringLiteral("VIEW") || tableName == QStringLiteral("VPORT") || tableName == QStringLiteral("UCS"))) parseViewLikeTable(reader);
            else skipUntilEndTab(reader);
        }
    }
}

DxfBlockDef parseBlock(DxfPairReader& reader, const QMap<QString, LayerInfo>& layers, const QHash<QString, DxfBlockDef>& knownBlocks)
{
    DxfBlockDef block;
    block.name = QStringLiteral("BlockDXF");
    DxfPair p;

    while (reader.next(p)) {
        if (p.code == 0) { reader.unread(p); break; }
        if (p.code == 5) block.handle = p.value.trimmed();
        else if (p.code == 8) block.layer = cleanLayerName(p.value);
        else if (p.code == 330) block.ownerHandle = p.value.trimmed();
        else if (p.code == 70) { block.flags = p.value.toInt(); block.xref = (block.flags & 4) || (block.flags & 8); }
        else if (p.code == 2 || p.code == 3) { block.name = p.value.trimmed(); block.anonymous = isAnonymousOrDynamicBlockName(block.name); }
        else if (p.code == 10) block.basePoint.setX(p.value.toDouble());
        else if (p.code == 20) block.basePoint.setY(dxfY(p.value.toDouble()));
    }

    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDBLK")) break;
        if (!isSupportedEntityType(type)) { parseEntityBody(type, reader); continue; }

        ++block.scannedEntities;
        DxfRawEntity raw;
        if (type == QStringLiteral("POLYLINE")) raw = parsePolylineEntity(reader);
        else if (type == QStringLiteral("HATCH")) raw = parseHatchEntity(reader);
        else raw = parseEntityBody(type, reader);

        // Mature block handling: keep the raw entities, not only a small preview.
        // ODA-converted DWG files often store most geometry in BLOCKS and reference
        // it from ENTITIES with INSERT/MINSERT. Expanding from raw entities preserves
        // layers, colors, bulges, text, and nested inserts much better than a preview.
        if (block.rawEntities.size() < kMaxBlockScannedEntities) block.rawEntities.append(raw);

        if (block.previewEntities.size() < kMaxBlockPreviewEntities) {
            bool fallback = false;
            auto entity = entityFromRaw(raw, layers, knownBlocks, nullptr, &fallback);
            if (entity) block.previewEntities.append(entity->toJson());
        }
        if (block.scannedEntities >= kMaxBlockScannedEntities) {
            while (reader.next(p)) {
                if (p.code == 0 && p.value.compare(QStringLiteral("ENDBLK"), Qt::CaseInsensitive) == 0) break;
            }
            break;
        }
    }
    return block;
}

void parseBlocksSection(DxfPairReader& reader, const QMap<QString, LayerInfo>& layers, QHash<QString, DxfBlockDef>& blocks, DxfDocumentModel& model)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0 && p.value.compare(QStringLiteral("ENDSEC"), Qt::CaseInsensitive) == 0) break;
        if (p.code == 0 && p.value.compare(QStringLiteral("BLOCK"), Qt::CaseInsensitive) == 0) {
            DxfBlockDef block = parseBlock(reader, layers, blocks);
            if (!block.name.trimmed().isEmpty()) { blocks.insert(block.name, block); model.registerBlock(block); }
        }
    }
}


bool isSystemDxfBlockName(const QString& name)
{
    const QString n = name.trimmed();
    return n.isEmpty() || n.compare(QStringLiteral("*Model_Space"), Qt::CaseInsensitive) == 0 ||
           n.compare(QStringLiteral("*Paper_Space"), Qt::CaseInsensitive) == 0;
}

QJsonArray convertedBlockDefinitionEntities(const DxfBlockDef& block,
                                            const QMap<QString, LayerInfo>& layers,
                                            const QHash<QString, DxfBlockDef>& blocks,
                                            const DxfTables& tables,
                                            DxfStats& stats)
{
    QJsonArray entities;
    const int maxEntities = qMax(1, kMaxBlockPreviewEntities * 4);
    for (const DxfRawEntity& sourceRaw : block.rawEntities) {
        if (entities.size() >= maxEntities) break;
        DxfRawEntity raw = sourceRaw;
        applyOcsToRawEntity(raw, &stats);
        bool fallback = false;
        std::unique_ptr<CadEntity> entity = entityFromRaw(raw, layers, blocks, &tables, &fallback);
        if (!entity) continue;
        if (fallback) ++stats.fallback;
        entities.append(entity->toJson());
    }

    // For very large blocks the preview path can still preserve a useful subset.
    if (entities.isEmpty() && !block.previewEntities.isEmpty()) entities = block.previewEntities;
    return entities;
}

void registerDxfBlockDefinitionsInDocument(CadDocument& document,
                                           const QMap<QString, LayerInfo>& layers,
                                           const QHash<QString, DxfBlockDef>& blocks,
                                           const DxfTables& tables,
                                           DxfStats& stats)
{
    for (auto it = blocks.constBegin(); it != blocks.constEnd(); ++it) {
        const DxfBlockDef& block = it.value();
        if (isSystemDxfBlockName(block.name)) continue;
        const QJsonArray entities = convertedBlockDefinitionEntities(block, layers, blocks, tables, stats);
        if (entities.isEmpty()) continue;
        document.upsertBlockDefinition(block.name, block.basePoint, entities, true, nullptr);
    }
}

bool shouldPreserveBlockReference(const DxfRawEntity& raw, const DxfBlockDef& block, const DxfLoadOptions& options)
{
    if (!options.preserveBlocks || options.forceExplodeBlocks) return false;
    if (qEnvironmentVariableIsSet("DWGVIEWER_DXF_FORCE_EXPLODE_BLOCKS")) return false;
    if (raw.type.compare(QStringLiteral("MINSERT"), Qt::CaseInsensitive) == 0) return false;
    if (std::max(1, raw.rowCount) > 1 || std::max(1, raw.colCount) > 1) return false;
    if (block.previewEntities.isEmpty()) return false;
    return true;
}

QVector<DxfRawEntity> readAttachedInsertAttributes(DxfPairReader& reader)
{
    QVector<DxfRawEntity> attrs;
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("SEQEND")) break;
        if (type == QStringLiteral("ATTRIB") || type == QStringLiteral("ATTDEF")) {
            attrs.append(parseEntityBody(type, reader));
            continue;
        }
        reader.unread(p);
        break;
    }
    return attrs;
}

void parseEntitiesSection(DxfPairReader& reader,
                          CadDocument& document,
                          QMap<QString, LayerInfo>& layers,
                          const QHash<QString, DxfBlockDef>& blocks,
                          const DxfTables& tables,
                          DxfStats& stats,
                          QRectF& bounds,
                          DxfDocumentModel& model,
                          const DxfLoadOptions& options)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code != 0) continue;
        const QString type = p.value.toUpper();
        if (type == QStringLiteral("ENDSEC")) break;
        if (!isSupportedEntityType(type)) {
            parseEntityBody(type, reader);
            ++stats.ignored;
            ++stats.unsupported;
            ++stats.unsupportedByType[type];
            continue;
        }

        DxfRawEntity raw;
        if (type == QStringLiteral("POLYLINE")) raw = parsePolylineEntity(reader);
        else if (type == QStringLiteral("HATCH")) raw = parseHatchEntity(reader);
        else raw = parseEntityBody(type, reader);

        QVector<DxfRawEntity> attachedAttributes;
        if ((type == QStringLiteral("INSERT") || type == QStringLiteral("MINSERT")) && raw.attrFollow != 0) {
            attachedAttributes = readAttachedInsertAttributes(reader);
            stats.attachedAttributes += attachedAttributes.size();
        }

        model.registerEntity(raw);
        if ((type == QStringLiteral("INSERT") || type == QStringLiteral("MINSERT"))) model.registerInsert(raw, attachedAttributes);
        for (const DxfRawEntity& attrRaw : attachedAttributes) model.registerEntity(attrRaw);

        if (raw.space != 0) stats.paperSpaceSeen = true;
        if (type == QStringLiteral("HATCH")) {
            stats.hatchLoops += raw.loops.size();
            if (raw.loops.size() > 1) stats.hatchInnerLoops += raw.loops.size() - 1;
        }

        bool handled = false;
        const QString blockKey = blockLookupKey(raw.blockName, blocks);
        if ((type == QStringLiteral("INSERT") || type == QStringLiteral("MINSERT")) && !blockKey.isEmpty()) {
            const DxfBlockDef block = blocks.value(blockKey);
            if (shouldPreserveBlockReference(raw, block, options)) {
                handled = appendVirtualBlockReference(raw, document, layers, block, stats, bounds);
            } else if (shouldUseVirtualBlockInstance(raw, block, stats)) {
                handled = appendVirtualBlockReference(raw, document, layers, block, stats, bounds);
            } else {
                handled = expandBlockInsert(raw, document, layers, blocks, tables, stats, bounds, 0);
            }
            if (!handled) {
                // If expansion cannot happen, keep a block-reference preview instead of dropping it.
                handled = appendEntityFromRaw(raw, document, layers, blocks, tables, stats, bounds, false);
            }
        } else {
            handled = appendEntityFromRaw(raw, document, layers, blocks, tables, stats, bounds, false);
        }

        for (DxfRawEntity attr : attachedAttributes) {
            if (attr.layer.trimmed().isEmpty() || attr.layer == QStringLiteral("0")) attr.layer = raw.layer;
            if (attr.colorIndex == 0 || attr.colorIndex == 256) {
                attr.colorIndex = raw.colorIndex;
                attr.trueColor = raw.trueColor;
            }
            appendEntityFromRaw(attr, document, layers, blocks, tables, stats, bounds, false);
        }

        if (!handled) ++stats.unsupportedByType[type];

        if ((stats.imported % 1000) == 0) QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
}

void parseObjectsSection(DxfPairReader& reader, DxfStats& stats, DxfDocumentModel& model)
{
    DxfPair p;
    QString currentType;
    DxfObjectRecord rec;
    auto flush = [&]() {
        if (!currentType.isEmpty()) {
            ++stats.objects;
            if (!rec.handle.trimmed().isEmpty()) model.objects.insert(rec.handle.trimmed().toUpper(), rec);
        }
        currentType.clear();
        rec = DxfObjectRecord();
    };
    while (reader.next(p)) {
        if (p.code == 0) {
            if (p.value.compare(QStringLiteral("ENDSEC"), Qt::CaseInsensitive) == 0) { flush(); break; }
            flush();
            currentType = p.value.toUpper();
            rec.type = currentType;
            continue;
        }
        if (currentType.isEmpty()) continue;
        ++rec.pairCount;
        if (p.code == 5) rec.handle = p.value.trimmed();
        else if (p.code == 330) rec.ownerHandle = p.value.trimmed();
        else if (p.code == 2 || p.code == 3) rec.name = p.value.trimmed();
        else if (p.code == 1001) model.xdataApps.insert(p.value.trimmed().toUpper());
    }
    stats.objectsSeen = stats.objects > 0;
}


void skipSection(DxfPairReader& reader)
{
    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0 && p.value.compare(QStringLiteral("ENDSEC"), Qt::CaseInsensitive) == 0) break;
    }
}
} // namespace

bool DxfLibrary::loadEditableWithReport(const QString& filePath,
                                       CadDocument& document,
                                       QMap<QString, LayerInfo>& layers,
                                       DxfLibraryReport* report,
                                       QString* errorMessage,
                                       const DxfLoadOptions& options)
{
    DxfPairReader reader(filePath);
    QString openError;
    if (!reader.open(&openError)) {
        if (errorMessage) *errorMessage = openError;
        DebugLogger::logError(QStringLiteral("DXF natif: ouverture impossible pour %1 - %2")
                              .arg(QFileInfo(filePath).fileName(), openError));
        return false;
    }

    CadDocument nativeDocument;
    QMap<QString, LayerInfo> nativeLayers;
    nativeDocument.ensureLayer(QStringLiteral("0"));
    LayerInfo defaultLayer;
    defaultLayer.name = QStringLiteral("0");
    defaultLayer.color = QColor(Qt::white);
    defaultLayer.visible = true;
    nativeLayers.insert(defaultLayer.name, defaultLayer);

    DxfHeader header;
    DxfTables tables;
    QHash<QString, DxfBlockDef> blocks;
    DxfStats stats;
    DxfDocumentModel model;
    QRectF bounds;

    DxfPair pair;
    while (reader.next(pair)) {
        if (pair.code != 0 || pair.value.compare(QStringLiteral("SECTION"), Qt::CaseInsensitive) != 0)
            continue;

        DxfPair sectionName;
        if (!reader.next(sectionName))
            break;
        if (sectionName.code != 2) {
            reader.unread(sectionName);
            skipSection(reader);
            continue;
        }

        const QString section = sectionName.value.trimmed().toUpper();
        if (section == QStringLiteral("HEADER")) {
            parseHeaderSection(reader, header, nativeDocument, tables);
            model.header = header;
        } else if (section == QStringLiteral("CLASSES")) {
            parseClassesSection(reader, stats);
        } else if (section == QStringLiteral("TABLES")) {
            parseTablesSection(reader, nativeLayers, nativeDocument, tables, &stats);
            model.tables = tables;
        } else if (section == QStringLiteral("BLOCKS")) {
            parseBlocksSection(reader, nativeLayers, blocks, model);
            stats.blocks = blocks.size();
        } else if (section == QStringLiteral("ENTITIES")) {
            parseEntitiesSection(reader, nativeDocument, nativeLayers, blocks, tables, stats, bounds, model, options);
        } else if (section == QStringLiteral("OBJECTS")) {
            parseObjectsSection(reader, stats, model);
        } else {
            skipSection(reader);
        }
    }

    if (options.preserveBlocks && !options.forceExplodeBlocks)
        registerDxfBlockDefinitionsInDocument(nativeDocument, nativeLayers, blocks, tables, stats);

    model.header = header;
    model.tables = tables;
    model.blocks = blocks;
    stats.entityDbHandles = model.entityDb.size();
    stats.layouts = model.layouts.size();
    stats.insertRefs = model.insertRefs.size();
    stats.anonymousBlocks = model.anonymousBlocks;
    stats.xrefBlocks = model.xrefBlocks;
    stats.xdataApps = model.xdataApps.size();
    stats.xdataPairs = model.preservedXDataPairs;
    stats.extensionDictionaries = model.extensionDictionaries;

    bool usedEntityBoundsInsteadOfHeader = false;

    auto fillReport = [&](bool ok, const QString& message) {
        if (!report) return;
        report->ok = ok;
        report->filePath = filePath;
        report->message = message;
        report->acadVersion = header.acadVersion;
        report->binaryDxf = reader.isBinary();
        report->insUnits = header.insUnits;
        report->entityCount = stats.imported;
        report->layerCount = nativeLayers.size();
        report->imported = stats.imported;
        report->ignored = stats.ignored;
        report->fallback = stats.fallback;
        report->unsupported = stats.unsupported;
        report->invalidGeometry = stats.invalidGeometry;
        report->blockDefinitionCount = blocks.size();
        report->insertReferenceCount = stats.insertRefs;
        report->preservedBlockReferenceCount = stats.virtualBlockRefs;
        report->expandedBlockReferenceCount = stats.expandedBlockRefs;
        report->virtualBlockEntityEstimate = stats.virtualBlockEntities;
        report->objectCount = stats.objects;
        report->layoutCount = stats.layouts;
        report->xrefBlockCount = stats.xrefBlocks;
        report->xdataAppCount = stats.xdataApps;
        report->xdataPairCount = stats.xdataPairs;
        report->paperSpaceSeen = stats.paperSpaceSeen;
        report->importedByType = stats.importedByType;
        report->unsupportedByType = stats.unsupportedByType;
        if (stats.blockDepthLimitHits > 0)
            report->warnings << QStringLiteral("Block expansion depth/size limit hit %1 time(s).").arg(stats.blockDepthLimitHits);
        if (stats.paperSpaceSeen && !options.loadPaperSpace)
            report->warnings << QStringLiteral("Paper-space entities were detected; current editable view focuses on model-space geometry.");
        if (stats.xrefBlocks > 0)
            report->warnings << QStringLiteral("%1 external reference block(s) detected. XREF file resolving is not native yet.").arg(stats.xrefBlocks);
        if (usedEntityBoundsInsteadOfHeader)
            report->warnings << QStringLiteral("DXF header extents looked stale; viewport limits were computed from real entity geometry.");
    };

    if (stats.imported == 0) {
        const QString msg = QStringLiteral(
            "Native DXF: no usable 2D entity found. "
            "Classes=%1, blocks=%2, objects=%3, ignored=%4, unsupported types=%5.")
            .arg(stats.classes)
            .arg(stats.blocks)
            .arg(stats.objects)
            .arg(stats.ignored)
            .arg(joinTopTypes(stats.unsupportedByType));
        fillReport(false, msg);
        if (errorMessage) *errorMessage = msg;
        DebugLogger::logError(QStringLiteral("Native DXF failed for: %1 - %2")
                              .arg(QFileInfo(filePath).fileName(), msg));
        return false;
    }

    auto isFiniteRect = [](const QRectF& r) {
        return r.isValid() && !r.isNull() &&
               qIsFinite(r.left()) && qIsFinite(r.right()) &&
               qIsFinite(r.top()) && qIsFinite(r.bottom()) &&
               qIsFinite(r.width()) && qIsFinite(r.height());
    };
    auto rectArea = [](const QRectF& r) {
        return std::max(1.0, std::abs(r.width() * r.height()));
    };

    const QRectF entityBounds = bounds.normalized();
    const QRectF headerBounds = header.extents.normalized();
    bool useHeaderExtents = false;

    if (header.hasExtMin && header.hasExtMax && isFiniteRect(headerBounds)) {
        if (isFiniteRect(entityBounds)) {
            const double pad = std::max({entityBounds.width(), entityBounds.height(), 1.0}) * 0.02 + 10.0;
            const QRectF paddedHeader = headerBounds.adjusted(-pad, -pad, pad, pad);
            const double headerArea = rectArea(headerBounds);
            const double entityArea = rectArea(entityBounds);
            const double widthRatio = headerBounds.width() / std::max(1.0, entityBounds.width());
            const double heightRatio = headerBounds.height() / std::max(1.0, entityBounds.height());

            // Many DXF files exported by third-party tools keep stale $EXTMIN/$EXTMAX
            // values. The attached ellipse_solid-style file is one example: the header
            // advertises a huge empty area, while all real entities live in a much
            // smaller model-space region. Using stale header extents makes the drawing
            // appear pushed to a corner or incorrectly zoomed. Prefer measured entity
            // bounds whenever the header does not contain the geometry or is wildly
            // larger than the real drawing.
            const bool headerContainsGeometry = paddedHeader.contains(entityBounds.topLeft()) &&
                                                paddedHeader.contains(entityBounds.bottomRight());
            const bool headerLooksStale = !headerContainsGeometry ||
                                          headerArea > entityArea * 12.0 ||
                                          widthRatio > 8.0 ||
                                          heightRatio > 8.0;
            useHeaderExtents = !headerLooksStale;
            usedEntityBoundsInsteadOfHeader = headerLooksStale;
        } else {
            useHeaderExtents = true;
        }
    }

    if (useHeaderExtents) {
        nativeDocument.setLimits(headerBounds);
    } else if (isFiniteRect(entityBounds)) {
        const double margin = std::max(entityBounds.width(), entityBounds.height()) * 0.02 + 10.0;
        nativeDocument.setLimits(entityBounds.adjusted(-margin, -margin, margin, margin));
        if (usedEntityBoundsInsteadOfHeader) {
            DebugLogger::logInfo(QStringLiteral("DXF header extents ignored: using measured entity bounds. header=[%1,%2 %3x%4] entities=[%5,%6 %7x%8]")
                                 .arg(headerBounds.x()).arg(headerBounds.y())
                                 .arg(headerBounds.width()).arg(headerBounds.height())
                                 .arg(entityBounds.x()).arg(entityBounds.y())
                                 .arg(entityBounds.width()).arg(entityBounds.height()));
        }
    } else if (header.hasExtMin && header.hasExtMax && isFiniteRect(headerBounds)) {
        nativeDocument.setLimits(headerBounds);
    }

    document = std::move(nativeDocument);
    layers = nativeLayers;

    const QString msg = QStringLiteral(
        "%1 DXF entities imported with the DWGView native parser. "
        "Layers=%2, blocks=%3, expanded inserts=%4, virtual blocks=%5, "
        "fallback=%6, ignored=%7, types=%8.")
        .arg(stats.imported)
        .arg(layers.size())
        .arg(stats.blocks)
        .arg(stats.expandedBlockRefs)
        .arg(stats.virtualBlockRefs)
        .arg(stats.fallback)
        .arg(stats.ignored)
        .arg(joinTopTypes(stats.importedByType));

    fillReport(true, msg);
    if (errorMessage) *errorMessage = msg;
    DebugLogger::logInfo(QStringLiteral("DXF imported with the DWGView native parser: %1 - %2")
                         .arg(QFileInfo(filePath).fileName(), msg));
    return true;
}


QString DxfScanInfo::summary() const
{
    return QStringLiteral("DXF scan: version=%1, %2, sections=%3, layers=%4, blocks=%5, entities=%6, inserts=%7, objects=%8")
        .arg(acadVersion.trimmed().isEmpty() ? QStringLiteral("unknown") : acadVersion)
        .arg(isBinary ? QStringLiteral("binary") : QStringLiteral("ASCII"))
        .arg(sections.join(QStringLiteral(", ")))
        .arg(layerCount)
        .arg(blockDefinitionCount)
        .arg(entityCount)
        .arg(insertReferenceCount)
        .arg(objectCount);
}

QString DxfLibraryReport::summary() const
{
    QString text = QStringLiteral("%1: imported=%2, layers=%3, blocks=%4, inserts=%5, preserved=%6, expanded=%7, fallback=%8, ignored=%9")
        .arg(backend)
        .arg(imported)
        .arg(layerCount)
        .arg(blockDefinitionCount)
        .arg(insertReferenceCount)
        .arg(preservedBlockReferenceCount)
        .arg(expandedBlockReferenceCount)
        .arg(fallback)
        .arg(ignored);
    if (!acadVersion.trimmed().isEmpty()) text += QStringLiteral(", ACADVER=%1").arg(acadVersion);
    if (!message.trimmed().isEmpty()) text += QStringLiteral("\n") + message.trimmed();
    if (!warnings.isEmpty()) text += QStringLiteral("\nWarnings: ") + warnings.join(QStringLiteral(" | "));
    return text;
}

bool DxfLibrary::scanFile(const QString& filePath, DxfScanInfo* info, QString* errorMessage)
{
    if (info) *info = DxfScanInfo();
    DxfPairReader reader(filePath);
    QString openError;
    if (!reader.open(&openError)) {
        if (errorMessage) *errorMessage = openError;
        return false;
    }

    DxfScanInfo local;
    local.filePath = filePath;
    local.isBinary = reader.isBinary();

    QString section;
    QString table;
    QString headerVar;
    DxfPair p;
    while (reader.next(p)) {
        if (p.code == 0 && p.value.compare(QStringLiteral("SECTION"), Qt::CaseInsensitive) == 0) {
            DxfPair name;
            if (!reader.next(name)) break;
            if (name.code == 2) {
                section = name.value.trimmed().toUpper();
                if (!local.sections.contains(section)) local.sections << section;
                local.hasHeader = local.hasHeader || section == QStringLiteral("HEADER");
                local.hasTables = local.hasTables || section == QStringLiteral("TABLES");
                local.hasBlocks = local.hasBlocks || section == QStringLiteral("BLOCKS");
                local.hasEntities = local.hasEntities || section == QStringLiteral("ENTITIES");
                local.hasObjects = local.hasObjects || section == QStringLiteral("OBJECTS");
                table.clear();
                headerVar.clear();
            }
            continue;
        }
        if (p.code == 0 && p.value.compare(QStringLiteral("ENDSEC"), Qt::CaseInsensitive) == 0) {
            section.clear();
            table.clear();
            headerVar.clear();
            continue;
        }

        if (section == QStringLiteral("HEADER")) {
            if (p.code == 9) { headerVar = p.value.toUpper(); continue; }
            if (headerVar == QStringLiteral("$ACADVER") && p.code == 1) local.acadVersion = p.value.trimmed();
            else if (headerVar == QStringLiteral("$INSUNITS") && p.code == 70) local.insUnits = p.value.toInt();
            continue;
        }

        if (section == QStringLiteral("TABLES")) {
            if (p.code == 0 && p.value.compare(QStringLiteral("TABLE"), Qt::CaseInsensitive) == 0) {
                DxfPair name;
                if (reader.next(name) && name.code == 2) table = name.value.trimmed().toUpper();
                continue;
            }
            if (p.code == 0 && p.value.compare(QStringLiteral("ENDTAB"), Qt::CaseInsensitive) == 0) {
                table.clear();
                continue;
            }
            if (p.code == 0 && table == QStringLiteral("LAYER") && p.value.compare(QStringLiteral("LAYER"), Qt::CaseInsensitive) == 0) ++local.layerCount;
            else if (p.code == 0 && table == QStringLiteral("LTYPE") && p.value.compare(QStringLiteral("LTYPE"), Qt::CaseInsensitive) == 0) ++local.lineTypeCount;
            else if (p.code == 0 && table == QStringLiteral("STYLE") && p.value.compare(QStringLiteral("STYLE"), Qt::CaseInsensitive) == 0) ++local.textStyleCount;
            continue;
        }

        if (section == QStringLiteral("BLOCKS")) {
            if (p.code == 0 && p.value.compare(QStringLiteral("BLOCK"), Qt::CaseInsensitive) == 0) ++local.blockDefinitionCount;
            continue;
        }

        if (section == QStringLiteral("ENTITIES")) {
            if (p.code == 0) {
                const QString type = p.value.trimmed().toUpper();
                if (!type.isEmpty() && type != QStringLiteral("ENDSEC")) {
                    ++local.entityCount;
                    if (type == QStringLiteral("INSERT") || type == QStringLiteral("MINSERT")) ++local.insertReferenceCount;
                }
            }
            continue;
        }

        if (section == QStringLiteral("OBJECTS")) {
            if (p.code == 0) {
                const QString type = p.value.trimmed().toUpper();
                if (!type.isEmpty() && type != QStringLiteral("ENDSEC")) ++local.objectCount;
            }
            continue;
        }
    }

    if (info) *info = local;
    if (errorMessage) *errorMessage = local.summary();
    return local.hasEntities || local.hasBlocks;
}

bool DxfLibrary::loadEditable(const QString& filePath,
                              CadDocument& document,
                              QMap<QString, LayerInfo>& layers,
                              QString* errorMessage)
{
    return loadEditableWithReport(filePath, document, layers, nullptr, errorMessage, DxfLoadOptions());
}

namespace {
QString cadEntityTypeName(CadEntity::Type type)
{
    switch (type) {
    case CadEntity::Type::Line: return QStringLiteral("LINE");
    case CadEntity::Type::Circle: return QStringLiteral("CIRCLE");
    case CadEntity::Type::Rectangle: return QStringLiteral("RECTANGLE");
    case CadEntity::Type::Polyline: return QStringLiteral("LWPOLYLINE");
    case CadEntity::Type::Arc: return QStringLiteral("ARC");
    case CadEntity::Type::Ellipse: return QStringLiteral("ELLIPSE");
    case CadEntity::Type::Polygon: return QStringLiteral("POLYGON");
    case CadEntity::Type::Text: return QStringLiteral("TEXT");
    case CadEntity::Type::LinearDimension: return QStringLiteral("DIMENSION");
    case CadEntity::Type::Leader: return QStringLiteral("LEADER");
    case CadEntity::Type::Hatch: return QStringLiteral("HATCH");
    case CadEntity::Type::BlockReference: return QStringLiteral("INSERT");
    }
    return QStringLiteral("ENTITY");
}
}

bool DxfLibrary::saveEditable(const QString& filePath,
                              const CadDocument& document,
                              DxfLibraryReport* report,
                              QString* errorMessage,
                              const DxfExportOptions& options)
{
    Q_UNUSED(options);
    QString message;
    const bool ok = document.saveAsDxf(filePath, &message);
    if (errorMessage) *errorMessage = message;

    if (report) {
        *report = DxfLibraryReport();
        report->ok = ok;
        report->filePath = filePath;
        report->message = message;
        report->entityCount = document.entityCount();
        report->layerCount = document.layers().size();
        report->imported = document.entityCount();
        report->blockDefinitionCount = document.blockNames().size();
        report->acadVersion = options.acadVersion;
        for (const auto& entity : document.entities()) {
            if (!entity) continue;
            ++report->importedByType[cadEntityTypeName(entity->type())];
            if (entity->type() == CadEntity::Type::BlockReference) ++report->insertReferenceCount;
        }
        if (!ok) report->warnings << QStringLiteral("Native DXF export failed.");
        else if (report->blockDefinitionCount > 0)
            report->warnings << QStringLiteral("Block definitions were exported in the native DXF BLOCKS section.");
    }

    return ok;
}
