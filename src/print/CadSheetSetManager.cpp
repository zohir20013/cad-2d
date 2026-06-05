#include "print/CadSheetSetManager.h"

#include <QDateTime>
#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace CadPrint {

QJsonObject PlotStamp::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("projectName")] = projectName;
    obj[QStringLiteral("drawingName")] = drawingName;
    obj[QStringLiteral("revision")] = revision;
    obj[QStringLiteral("author")] = author;
    obj[QStringLiteral("dateFormat")] = dateFormat;
    obj[QStringLiteral("enabled")] = enabled;
    return obj;
}

PlotStamp PlotStamp::fromJson(const QJsonObject& obj)
{
    PlotStamp stamp;
    stamp.projectName = obj.value(QStringLiteral("projectName")).toString();
    stamp.drawingName = obj.value(QStringLiteral("drawingName")).toString();
    stamp.revision = obj.value(QStringLiteral("revision")).toString();
    stamp.author = obj.value(QStringLiteral("author")).toString();
    stamp.dateFormat = obj.value(QStringLiteral("dateFormat")).toString(stamp.dateFormat);
    stamp.enabled = obj.value(QStringLiteral("enabled")).toBool(false);
    return stamp;
}

QJsonObject SheetDefinition::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("paperName")] = paperName;
    obj[QStringLiteral("paperWidthMm")] = paperSizeMm.width();
    obj[QStringLiteral("paperHeightMm")] = paperSizeMm.height();
    obj[QStringLiteral("printableX")] = printableAreaMm.x();
    obj[QStringLiteral("printableY")] = printableAreaMm.y();
    obj[QStringLiteral("printableWidth")] = printableAreaMm.width();
    obj[QStringLiteral("printableHeight")] = printableAreaMm.height();
    obj[QStringLiteral("modelX")] = modelWindow.x();
    obj[QStringLiteral("modelY")] = modelWindow.y();
    obj[QStringLiteral("modelWidth")] = modelWindow.width();
    obj[QStringLiteral("modelHeight")] = modelWindow.height();
    obj[QStringLiteral("plotScale")] = plotScale;
    obj[QStringLiteral("centerPlot")] = centerPlot;
    obj[QStringLiteral("monochrome")] = monochrome;
    obj[QStringLiteral("plotLineweights")] = plotLineweights;
    obj[QStringLiteral("landscape")] = landscape;
    obj[QStringLiteral("stamp")] = stamp.toJson();
    return obj;
}

SheetDefinition SheetDefinition::fromJson(const QJsonObject& obj)
{
    SheetDefinition sheet;
    sheet.name = obj.value(QStringLiteral("name")).toString(sheet.name);
    sheet.paperName = obj.value(QStringLiteral("paperName")).toString(sheet.paperName);
    sheet.paperSizeMm = QSizeF(obj.value(QStringLiteral("paperWidthMm")).toDouble(sheet.paperSizeMm.width()),
                               obj.value(QStringLiteral("paperHeightMm")).toDouble(sheet.paperSizeMm.height()));
    sheet.printableAreaMm = QRectF(obj.value(QStringLiteral("printableX")).toDouble(sheet.printableAreaMm.x()),
                                   obj.value(QStringLiteral("printableY")).toDouble(sheet.printableAreaMm.y()),
                                   obj.value(QStringLiteral("printableWidth")).toDouble(sheet.printableAreaMm.width()),
                                   obj.value(QStringLiteral("printableHeight")).toDouble(sheet.printableAreaMm.height()));
    sheet.modelWindow = QRectF(obj.value(QStringLiteral("modelX")).toDouble(),
                               obj.value(QStringLiteral("modelY")).toDouble(),
                               obj.value(QStringLiteral("modelWidth")).toDouble(),
                               obj.value(QStringLiteral("modelHeight")).toDouble());
    sheet.plotScale = obj.value(QStringLiteral("plotScale")).toDouble(sheet.plotScale);
    sheet.centerPlot = obj.value(QStringLiteral("centerPlot")).toBool(sheet.centerPlot);
    sheet.monochrome = obj.value(QStringLiteral("monochrome")).toBool(sheet.monochrome);
    sheet.plotLineweights = obj.value(QStringLiteral("plotLineweights")).toBool(sheet.plotLineweights);
    sheet.landscape = obj.value(QStringLiteral("landscape")).toBool(sheet.landscape);
    sheet.stamp = PlotStamp::fromJson(obj.value(QStringLiteral("stamp")).toObject());
    return sheet;
}

QJsonObject PlotJob::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("outputPath")] = outputPath;
    obj[QStringLiteral("format")] = format;
    obj[QStringLiteral("combineIntoSingleFile")] = combineIntoSingleFile;
    obj[QStringLiteral("openAfterPlot")] = openAfterPlot;
    QJsonArray array;
    for (const SheetDefinition& sheet : sheets) array.append(sheet.toJson());
    obj[QStringLiteral("sheets")] = array;
    return obj;
}

PlotJob PlotJob::fromJson(const QJsonObject& obj)
{
    PlotJob job;
    job.outputPath = obj.value(QStringLiteral("outputPath")).toString();
    job.format = obj.value(QStringLiteral("format")).toString(job.format);
    job.combineIntoSingleFile = obj.value(QStringLiteral("combineIntoSingleFile")).toBool(job.combineIntoSingleFile);
    job.openAfterPlot = obj.value(QStringLiteral("openAfterPlot")).toBool(job.openAfterPlot);
    for (const QJsonValue& value : obj.value(QStringLiteral("sheets")).toArray()) job.sheets << SheetDefinition::fromJson(value.toObject());
    return job;
}

CadSheetSetManager::CadSheetSetManager()
{
    resetDefaultSheets();
}

void CadSheetSetManager::resetDefaultSheets()
{
    m_sheets = standardMetricSheets();
    m_currentSheet = m_sheets.isEmpty() ? QString() : m_sheets.first().name;
}

bool CadSheetSetManager::addSheet(const SheetDefinition& sheet, QString* error)
{
    if (sheet.name.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Sheet name is empty.");
        return false;
    }
    for (const SheetDefinition& existing : m_sheets) {
        if (existing.name.compare(sheet.name, Qt::CaseInsensitive) == 0) {
            if (error) *error = QStringLiteral("Sheet already exists: %1").arg(sheet.name);
            return false;
        }
    }
    m_sheets << sheet;
    if (m_currentSheet.isEmpty()) m_currentSheet = sheet.name;
    return true;
}

bool CadSheetSetManager::removeSheet(const QString& name)
{
    for (int i = 0; i < m_sheets.size(); ++i) {
        if (m_sheets[i].name == name) {
            m_sheets.removeAt(i);
            if (m_currentSheet == name) m_currentSheet = m_sheets.isEmpty() ? QString() : m_sheets.first().name;
            return true;
        }
    }
    return false;
}

bool CadSheetSetManager::renameSheet(const QString& oldName, const QString& newName)
{
    if (newName.trimmed().isEmpty()) return false;
    for (const SheetDefinition& sheet : m_sheets) {
        if (sheet.name == newName) return false;
    }
    for (SheetDefinition& sheet : m_sheets) {
        if (sheet.name == oldName) {
            sheet.name = newName;
            if (m_currentSheet == oldName) m_currentSheet = newName;
            return true;
        }
    }
    return false;
}

bool CadSheetSetManager::setCurrentSheet(const QString& name)
{
    for (const SheetDefinition& sheet : m_sheets) {
        if (sheet.name == name) {
            m_currentSheet = name;
            return true;
        }
    }
    return false;
}

SheetDefinition CadSheetSetManager::currentSheet() const
{
    return sheet(m_currentSheet);
}

SheetDefinition CadSheetSetManager::sheet(const QString& name) const
{
    for (const SheetDefinition& sheet : m_sheets) {
        if (sheet.name == name) return sheet;
    }
    return m_sheets.isEmpty() ? SheetDefinition{} : m_sheets.first();
}

QStringList CadSheetSetManager::sheetNames() const
{
    QStringList names;
    for (const SheetDefinition& sheet : m_sheets) names << sheet.name;
    return names;
}

QVector<SheetDefinition> CadSheetSetManager::sheets() const
{
    return m_sheets;
}

PlotJob CadSheetSetManager::createPdfPlotJob(const QString& outputPath, bool currentSheetOnly) const
{
    PlotJob job;
    job.outputPath = outputPath;
    job.format = QStringLiteral("PDF");
    job.sheets = currentSheetOnly ? QVector<SheetDefinition>{currentSheet()} : m_sheets;
    job.combineIntoSingleFile = true;
    return job;
}

QRectF CadSheetSetManager::scaledModelWindowOnPaper(const SheetDefinition& sheet) const
{
    const QSizeF modelSize(sheet.modelWindow.width() * sheet.plotScale, sheet.modelWindow.height() * sheet.plotScale);
    QPointF origin = sheet.printableAreaMm.topLeft();
    if (sheet.centerPlot) {
        origin += QPointF((sheet.printableAreaMm.width() - modelSize.width()) * 0.5,
                          (sheet.printableAreaMm.height() - modelSize.height()) * 0.5);
    }
    return QRectF(origin, modelSize).normalized();
}

SheetDefinition CadSheetSetManager::makeSheet(const QString& name, const QString& paperName, double widthMm, double heightMm)
{
    SheetDefinition sheet;
    sheet.name = name;
    sheet.paperName = paperName;
    sheet.paperSizeMm = QSizeF(widthMm, heightMm);
    sheet.landscape = widthMm >= heightMm;
    sheet.printableAreaMm = QRectF(10.0, 10.0, qMax(1.0, widthMm - 20.0), qMax(1.0, heightMm - 20.0));
    sheet.modelWindow = QRectF(0.0, 0.0, sheet.printableAreaMm.width(), sheet.printableAreaMm.height());
    return sheet;
}

QVector<SheetDefinition> CadSheetSetManager::standardMetricSheets()
{
    return {
        makeSheet(QStringLiteral("Layout1"), QStringLiteral("A3"), 420.0, 297.0),
        makeSheet(QStringLiteral("A4"), QStringLiteral("A4"), 297.0, 210.0),
        makeSheet(QStringLiteral("A2"), QStringLiteral("A2"), 594.0, 420.0),
        makeSheet(QStringLiteral("A1"), QStringLiteral("A1"), 841.0, 594.0),
        makeSheet(QStringLiteral("A0"), QStringLiteral("A0"), 1189.0, 841.0)
    };
}

QString CadSheetSetManager::scaleToText(double plotScale)
{
    if (plotScale <= 0.0) return QStringLiteral("Fit");
    const double denominator = 1.0 / plotScale;
    if (qAbs(denominator - std::round(denominator)) < 1.0e-6) {
        return QStringLiteral("1:%1").arg(static_cast<int>(std::round(denominator)));
    }
    return QStringLiteral("%1:1").arg(QString::number(plotScale, 'f', 4));
}

double CadSheetSetManager::parseScaleText(const QString& text, bool* ok)
{
    QString trimmed = text.trimmed();
    if (trimmed.compare(QStringLiteral("Fit"), Qt::CaseInsensitive) == 0) {
        if (ok) *ok = true;
        return 0.0;
    }
    const QStringList parts = trimmed.split(QLatin1Char(':'));
    if (parts.size() == 2) {
        bool okA = false;
        bool okB = false;
        const double a = parts[0].toDouble(&okA);
        const double b = parts[1].toDouble(&okB);
        if (okA && okB && a > 0.0 && b > 0.0) {
            if (ok) *ok = true;
            return a / b;
        }
    }
    bool localOk = false;
    const double v = trimmed.toDouble(&localOk);
    if (ok) *ok = localOk;
    return localOk ? v : 1.0;
}

QJsonObject CadSheetSetManager::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("currentSheet")] = m_currentSheet;
    QJsonArray array;
    for (const SheetDefinition& sheet : m_sheets) array.append(sheet.toJson());
    obj[QStringLiteral("sheets")] = array;
    return obj;
}

bool CadSheetSetManager::fromJson(const QJsonObject& obj, QString* error)
{
    Q_UNUSED(error)
    m_sheets.clear();
    for (const QJsonValue& value : obj.value(QStringLiteral("sheets")).toArray()) m_sheets << SheetDefinition::fromJson(value.toObject());
    if (m_sheets.isEmpty()) resetDefaultSheets();
    m_currentSheet = obj.value(QStringLiteral("currentSheet")).toString(m_sheets.first().name);
    return true;
}

} // namespace CadPrint
