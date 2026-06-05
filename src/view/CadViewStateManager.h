#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>

namespace CadView {

struct CadViewState {
    QString name;
    QPointF center;
    double scale = 1.0;
    double rotationDeg = 0.0;
    QRectF modelWindow;
    bool hasModelWindow = false;

    QJsonObject toJson() const;
    static CadViewState fromJson(const QJsonObject& obj);
};

class CadViewStateManager
{
public:
    CadViewStateManager();

    CadViewState currentView() const { return m_current; }
    void setCurrentView(const CadViewState& view);
    void reset();

    CadViewState zoomExtents(const QRectF& modelBounds, const QSizeF& viewportPixels, double marginFactor = 0.08) const;
    CadViewState zoomWindow(const QRectF& modelWindow, const QSizeF& viewportPixels) const;
    CadViewState zoomScale(double factor, const QPointF& fixedModelPoint = QPointF()) const;
    CadViewState panByModelDelta(const QPointF& delta) const;
    CadViewState rotateView(double angleDeg, const QPointF& center = QPointF()) const;

    bool saveNamedView(const QString& name, const CadViewState& view);
    bool removeNamedView(const QString& name);
    bool hasNamedView(const QString& name) const;
    CadViewState namedView(const QString& name, bool* found = nullptr) const;
    QStringList namedViewNames() const;

    void pushPreviousView(const CadViewState& view);
    bool canGoBack() const;
    bool canGoForward() const;
    CadViewState previousView(bool* ok = nullptr);
    CadViewState nextView(bool* ok = nullptr);
    void clearNavigationHistory();

    QTransform modelToViewTransform(const QSizeF& viewportPixels) const;
    QTransform viewToModelTransform(const QSizeF& viewportPixels) const;
    QPointF modelToView(const QPointF& modelPoint, const QSizeF& viewportPixels) const;
    QPointF viewToModel(const QPointF& viewPoint, const QSizeF& viewportPixels) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& obj, QString* error = nullptr);

    static CadViewState makeViewFromWindow(const QRectF& modelWindow, const QSizeF& viewportPixels);
    static double scaleToFit(const QRectF& modelBounds, const QSizeF& viewportPixels, double marginFactor = 0.08);
    static QRectF normalizedWindow(const QRectF& rect);

private:
    CadViewState m_current;
    QMap<QString, CadViewState> m_namedViews;
    QVector<CadViewState> m_backStack;
    QVector<CadViewState> m_forwardStack;
    int m_maxNavigation = 50;
};

} // namespace CadView
