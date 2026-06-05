#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace CadSettings {

enum class ObjectSnapMode {
    Endpoint      = 1 << 0,
    Midpoint      = 1 << 1,
    Center        = 1 << 2,
    Quadrant      = 1 << 3,
    Intersection  = 1 << 4,
    Extension     = 1 << 5,
    Perpendicular = 1 << 6,
    Tangent       = 1 << 7,
    Nearest       = 1 << 8,
    Apparent      = 1 << 9
};

struct GridSettings {
    bool visible = true;
    bool adaptive = true;
    double majorSpacing = 10.0;
    int minorDivisions = 5;
    QColor majorColor = QColor(70, 70, 70);
    QColor minorColor = QColor(38, 38, 38);

    QJsonObject toJson() const;
    static GridSettings fromJson(const QJsonObject& obj);
};

struct SnapSettings {
    bool enabled = true;
    bool snapToGrid = false;
    double snapSpacingX = 1.0;
    double snapSpacingY = 1.0;
    int objectSnapMask = static_cast<int>(ObjectSnapMode::Endpoint)
                       | static_cast<int>(ObjectSnapMode::Midpoint)
                       | static_cast<int>(ObjectSnapMode::Center)
                       | static_cast<int>(ObjectSnapMode::Intersection)
                       | static_cast<int>(ObjectSnapMode::Nearest);
    double aperturePixels = 10.0;
    bool snapTracking = true;

    QJsonObject toJson() const;
    static SnapSettings fromJson(const QJsonObject& obj);
    bool hasMode(ObjectSnapMode mode) const;
    void setMode(ObjectSnapMode mode, bool on);
};

struct DraftingSettings {
    bool orthoMode = false;
    bool polarTracking = true;
    QVector<double> polarAnglesDeg = {0.0, 30.0, 45.0, 60.0, 90.0, 120.0, 135.0, 150.0, 180.0, 210.0, 225.0, 240.0, 270.0, 300.0, 315.0, 330.0};
    double angleToleranceDeg = 2.0;
    bool dynamicInput = true;
    bool commandLineAutocomplete = true;

    QJsonObject toJson() const;
    static DraftingSettings fromJson(const QJsonObject& obj);
};

struct UnitSettings {
    QString lengthUnit = QStringLiteral("mm");
    int lengthPrecision = 3;
    int anglePrecision = 2;
    bool decimalComma = false;
    double drawingScale = 1.0;

    QJsonObject toJson() const;
    static UnitSettings fromJson(const QJsonObject& obj);
};

struct DisplaySettings {
    QColor backgroundColor = QColor(25, 25, 25);
    QColor selectionColor = QColor(255, 180, 0);
    QColor highlightColor = QColor(90, 180, 255);
    bool showLineWeights = true;
    bool antiAliasing = true;
    bool largeDrawingMode = false;
    int curveTessellationSegments = 96;

    QJsonObject toJson() const;
    static DisplaySettings fromJson(const QJsonObject& obj);
};

class CadUserPreferences
{
public:
    GridSettings grid;
    SnapSettings snap;
    DraftingSettings drafting;
    UnitSettings units;
    DisplaySettings display;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);
    bool saveToFile(const QString& path, QString* error = nullptr) const;
    bool loadFromFile(const QString& path, QString* error = nullptr);

    void applyClassicAutoCADProfile();
    void applyDarkModernProfile();
    QStringList validate() const;

    QPointF snapGridPoint(const QPointF& point) const;
    double nearestPolarAngle(double angleDeg, bool* snapped = nullptr) const;
    static QString defaultPreferencesPath(const QString& applicationName = QStringLiteral("DWGViewerAdvanced"));

private:
    static double normalizeAngle(double angleDeg);
};

} // namespace CadSettings
