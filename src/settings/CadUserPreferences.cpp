#include "CadUserPreferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtMath>
#include <algorithm>

namespace CadSettings {

namespace {
QJsonObject colorToJson(const QColor& c)
{
    QJsonObject o;
    o["r"] = c.red();
    o["g"] = c.green();
    o["b"] = c.blue();
    o["a"] = c.alpha();
    return o;
}

QColor colorFromJson(const QJsonObject& o, const QColor& fallback)
{
    if (o.isEmpty()) return fallback;
    return QColor(o.value("r").toInt(fallback.red()),
                  o.value("g").toInt(fallback.green()),
                  o.value("b").toInt(fallback.blue()),
                  o.value("a").toInt(fallback.alpha()));
}

QJsonArray anglesToJson(const QVector<double>& values)
{
    QJsonArray arr;
    for (double v : values) arr.append(v);
    return arr;
}

QVector<double> anglesFromJson(const QJsonArray& arr, const QVector<double>& fallback)
{
    if (arr.isEmpty()) return fallback;
    QVector<double> values;
    for (const QJsonValue& v : arr) values.push_back(v.toDouble());
    return values;
}
}

QJsonObject GridSettings::toJson() const
{
    QJsonObject obj;
    obj["visible"] = visible;
    obj["adaptive"] = adaptive;
    obj["majorSpacing"] = majorSpacing;
    obj["minorDivisions"] = minorDivisions;
    obj["majorColor"] = colorToJson(majorColor);
    obj["minorColor"] = colorToJson(minorColor);
    return obj;
}

GridSettings GridSettings::fromJson(const QJsonObject& obj)
{
    GridSettings s;
    s.visible = obj.value("visible").toBool(s.visible);
    s.adaptive = obj.value("adaptive").toBool(s.adaptive);
    s.majorSpacing = obj.value("majorSpacing").toDouble(s.majorSpacing);
    s.minorDivisions = obj.value("minorDivisions").toInt(s.minorDivisions);
    s.majorColor = colorFromJson(obj.value("majorColor").toObject(), s.majorColor);
    s.minorColor = colorFromJson(obj.value("minorColor").toObject(), s.minorColor);
    return s;
}

QJsonObject SnapSettings::toJson() const
{
    QJsonObject obj;
    obj["enabled"] = enabled;
    obj["snapToGrid"] = snapToGrid;
    obj["snapSpacingX"] = snapSpacingX;
    obj["snapSpacingY"] = snapSpacingY;
    obj["objectSnapMask"] = objectSnapMask;
    obj["aperturePixels"] = aperturePixels;
    obj["snapTracking"] = snapTracking;
    return obj;
}

SnapSettings SnapSettings::fromJson(const QJsonObject& obj)
{
    SnapSettings s;
    s.enabled = obj.value("enabled").toBool(s.enabled);
    s.snapToGrid = obj.value("snapToGrid").toBool(s.snapToGrid);
    s.snapSpacingX = obj.value("snapSpacingX").toDouble(s.snapSpacingX);
    s.snapSpacingY = obj.value("snapSpacingY").toDouble(s.snapSpacingY);
    s.objectSnapMask = obj.value("objectSnapMask").toInt(s.objectSnapMask);
    s.aperturePixels = obj.value("aperturePixels").toDouble(s.aperturePixels);
    s.snapTracking = obj.value("snapTracking").toBool(s.snapTracking);
    return s;
}

bool SnapSettings::hasMode(ObjectSnapMode mode) const
{
    return (objectSnapMask & static_cast<int>(mode)) != 0;
}

void SnapSettings::setMode(ObjectSnapMode mode, bool on)
{
    const int bit = static_cast<int>(mode);
    if (on) objectSnapMask |= bit;
    else objectSnapMask &= ~bit;
}

QJsonObject DraftingSettings::toJson() const
{
    QJsonObject obj;
    obj["orthoMode"] = orthoMode;
    obj["polarTracking"] = polarTracking;
    obj["polarAnglesDeg"] = anglesToJson(polarAnglesDeg);
    obj["angleToleranceDeg"] = angleToleranceDeg;
    obj["dynamicInput"] = dynamicInput;
    obj["commandLineAutocomplete"] = commandLineAutocomplete;
    return obj;
}

DraftingSettings DraftingSettings::fromJson(const QJsonObject& obj)
{
    DraftingSettings s;
    s.orthoMode = obj.value("orthoMode").toBool(s.orthoMode);
    s.polarTracking = obj.value("polarTracking").toBool(s.polarTracking);
    s.polarAnglesDeg = anglesFromJson(obj.value("polarAnglesDeg").toArray(), s.polarAnglesDeg);
    s.angleToleranceDeg = obj.value("angleToleranceDeg").toDouble(s.angleToleranceDeg);
    s.dynamicInput = obj.value("dynamicInput").toBool(s.dynamicInput);
    s.commandLineAutocomplete = obj.value("commandLineAutocomplete").toBool(s.commandLineAutocomplete);
    return s;
}

QJsonObject UnitSettings::toJson() const
{
    QJsonObject obj;
    obj["lengthUnit"] = lengthUnit;
    obj["lengthPrecision"] = lengthPrecision;
    obj["anglePrecision"] = anglePrecision;
    obj["decimalComma"] = decimalComma;
    obj["drawingScale"] = drawingScale;
    return obj;
}

UnitSettings UnitSettings::fromJson(const QJsonObject& obj)
{
    UnitSettings s;
    s.lengthUnit = obj.value("lengthUnit").toString(s.lengthUnit);
    s.lengthPrecision = obj.value("lengthPrecision").toInt(s.lengthPrecision);
    s.anglePrecision = obj.value("anglePrecision").toInt(s.anglePrecision);
    s.decimalComma = obj.value("decimalComma").toBool(s.decimalComma);
    s.drawingScale = obj.value("drawingScale").toDouble(s.drawingScale);
    return s;
}

QJsonObject DisplaySettings::toJson() const
{
    QJsonObject obj;
    obj["backgroundColor"] = colorToJson(backgroundColor);
    obj["selectionColor"] = colorToJson(selectionColor);
    obj["highlightColor"] = colorToJson(highlightColor);
    obj["showLineWeights"] = showLineWeights;
    obj["antiAliasing"] = antiAliasing;
    obj["largeDrawingMode"] = largeDrawingMode;
    obj["curveTessellationSegments"] = curveTessellationSegments;
    return obj;
}

DisplaySettings DisplaySettings::fromJson(const QJsonObject& obj)
{
    DisplaySettings s;
    s.backgroundColor = colorFromJson(obj.value("backgroundColor").toObject(), s.backgroundColor);
    s.selectionColor = colorFromJson(obj.value("selectionColor").toObject(), s.selectionColor);
    s.highlightColor = colorFromJson(obj.value("highlightColor").toObject(), s.highlightColor);
    s.showLineWeights = obj.value("showLineWeights").toBool(s.showLineWeights);
    s.antiAliasing = obj.value("antiAliasing").toBool(s.antiAliasing);
    s.largeDrawingMode = obj.value("largeDrawingMode").toBool(s.largeDrawingMode);
    s.curveTessellationSegments = obj.value("curveTessellationSegments").toInt(s.curveTessellationSegments);
    return s;
}

QJsonObject CadUserPreferences::toJson() const
{
    QJsonObject obj;
    obj["grid"] = grid.toJson();
    obj["snap"] = snap.toJson();
    obj["drafting"] = drafting.toJson();
    obj["units"] = units.toJson();
    obj["display"] = display.toJson();
    return obj;
}

bool CadUserPreferences::fromJson(const QJsonObject& obj, QString* error)
{
    grid = GridSettings::fromJson(obj.value("grid").toObject());
    snap = SnapSettings::fromJson(obj.value("snap").toObject());
    drafting = DraftingSettings::fromJson(obj.value("drafting").toObject());
    units = UnitSettings::fromJson(obj.value("units").toObject());
    display = DisplaySettings::fromJson(obj.value("display").toObject());
    const QStringList issues = validate();
    if (!issues.isEmpty()) {
        if (error) *error = issues.join("\n");
        return false;
    }
    return true;
}

bool CadUserPreferences::saveToFile(const QString& path, QString* error) const
{
    QFile file(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool CadUserPreferences::loadFromFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("Preferences file is not a JSON object.");
        return false;
    }
    return fromJson(doc.object(), error);
}

void CadUserPreferences::applyClassicAutoCADProfile()
{
    grid.visible = true;
    grid.adaptive = true;
    grid.majorSpacing = 10.0;
    grid.minorDivisions = 5;
    display.backgroundColor = QColor(0, 0, 0);
    display.selectionColor = QColor(255, 255, 0);
    display.highlightColor = QColor(0, 255, 255);
    snap.enabled = true;
    snap.snapTracking = true;
    drafting.orthoMode = false;
    drafting.polarTracking = true;
    units.lengthUnit = QStringLiteral("mm");
    units.lengthPrecision = 3;
}

void CadUserPreferences::applyDarkModernProfile()
{
    grid.visible = true;
    grid.adaptive = true;
    grid.majorColor = QColor(70, 70, 70);
    grid.minorColor = QColor(38, 38, 38);
    display.backgroundColor = QColor(25, 25, 25);
    display.selectionColor = QColor(255, 180, 0);
    display.highlightColor = QColor(90, 180, 255);
    display.antiAliasing = true;
}

QStringList CadUserPreferences::validate() const
{
    QStringList issues;
    if (grid.majorSpacing <= 0.0) issues << QStringLiteral("Grid major spacing must be positive.");
    if (grid.minorDivisions < 1) issues << QStringLiteral("Grid minor divisions must be at least 1.");
    if (snap.snapSpacingX <= 0.0 || snap.snapSpacingY <= 0.0) issues << QStringLiteral("Snap spacing must be positive.");
    if (snap.aperturePixels < 0.0) issues << QStringLiteral("Snap aperture cannot be negative.");
    if (drafting.angleToleranceDeg < 0.0) issues << QStringLiteral("Polar angle tolerance cannot be negative.");
    if (units.lengthPrecision < 0 || units.anglePrecision < 0) issues << QStringLiteral("Precision cannot be negative.");
    if (units.drawingScale <= 0.0) issues << QStringLiteral("Drawing scale must be positive.");
    if (display.curveTessellationSegments < 8) issues << QStringLiteral("Curve tessellation should be at least 8 segments.");
    return issues;
}

QPointF CadUserPreferences::snapGridPoint(const QPointF& point) const
{
    if (!snap.snapToGrid || snap.snapSpacingX <= 0.0 || snap.snapSpacingY <= 0.0) return point;
    const double x = qRound(point.x() / snap.snapSpacingX) * snap.snapSpacingX;
    const double y = qRound(point.y() / snap.snapSpacingY) * snap.snapSpacingY;
    return QPointF(x, y);
}

double CadUserPreferences::nearestPolarAngle(double angleDeg, bool* snapped) const
{
    if (snapped) *snapped = false;
    const double a = normalizeAngle(angleDeg);
    double best = a;
    double bestDelta = 360.0;
    for (double candidate : drafting.polarAnglesDeg) {
        const double c = normalizeAngle(candidate);
        double delta = qAbs(a - c);
        if (delta > 180.0) delta = 360.0 - delta;
        if (delta < bestDelta) {
            bestDelta = delta;
            best = c;
        }
    }
    if (bestDelta <= drafting.angleToleranceDeg) {
        if (snapped) *snapped = true;
        return best;
    }
    return a;
}

QString CadUserPreferences::defaultPreferencesPath(const QString& applicationName)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString dir = base.isEmpty() ? QDir::homePath() + QStringLiteral("/.config/") + applicationName : base;
    return QDir(dir).filePath(QStringLiteral("preferences.json"));
}

double CadUserPreferences::normalizeAngle(double angleDeg)
{
    while (angleDeg >= 360.0) angleDeg -= 360.0;
    while (angleDeg < 0.0) angleDeg += 360.0;
    return angleDeg;
}

} // namespace CadSettings
