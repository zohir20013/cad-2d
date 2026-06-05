#pragma once

#include <QJsonObject>
#include <QPageSize>
#include <QSizeF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CadPrint {

struct PlotStamp {
    QString projectName;
    QString drawingName;
    QString revision;
    QString author;
    QString dateFormat = QStringLiteral("yyyy-MM-dd HH:mm");
    bool enabled = false;
    QJsonObject toJson() const;
    static PlotStamp fromJson(const QJsonObject& obj);
};

struct SheetDefinition {
    QString name = QStringLiteral("Layout1");
    QString paperName = QStringLiteral("A3");
    QSizeF paperSizeMm = QSizeF(420.0, 297.0);
    QRectF printableAreaMm = QRectF(10.0, 10.0, 400.0, 277.0);
    QRectF modelWindow;
    double plotScale = 1.0;
    bool centerPlot = true;
    bool monochrome = false;
    bool plotLineweights = true;
    bool landscape = true;
    PlotStamp stamp;
    QJsonObject toJson() const;
    static SheetDefinition fromJson(const QJsonObject& obj);
};

struct PlotJob {
    QString outputPath;
    QString format = QStringLiteral("PDF");
    QVector<SheetDefinition> sheets;
    bool combineIntoSingleFile = true;
    bool openAfterPlot = false;
    QJsonObject toJson() const;
    static PlotJob fromJson(const QJsonObject& obj);
};

class CadSheetSetManager
{
public:
    CadSheetSetManager();

    void resetDefaultSheets();
    bool addSheet(const SheetDefinition& sheet, QString* error = nullptr);
    bool removeSheet(const QString& name);
    bool renameSheet(const QString& oldName, const QString& newName);
    bool setCurrentSheet(const QString& name);

    QString currentSheetName() const { return m_currentSheet; }
    SheetDefinition currentSheet() const;
    SheetDefinition sheet(const QString& name) const;
    QStringList sheetNames() const;
    QVector<SheetDefinition> sheets() const;

    PlotJob createPdfPlotJob(const QString& outputPath, bool currentSheetOnly = false) const;
    QRectF scaledModelWindowOnPaper(const SheetDefinition& sheet) const;

    static SheetDefinition makeSheet(const QString& name, const QString& paperName, double widthMm, double heightMm);
    static QVector<SheetDefinition> standardMetricSheets();
    static QString scaleToText(double plotScale);
    static double parseScaleText(const QString& text, bool* ok = nullptr);

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

private:
    QVector<SheetDefinition> m_sheets;
    QString m_currentSheet = QStringLiteral("Layout1");
};

} // namespace CadPrint
