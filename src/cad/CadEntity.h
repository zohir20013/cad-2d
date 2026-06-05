#pragma once

#include <QColor>
#include <QGraphicsItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <memory>
#include <QtGlobal>

class CadEntity
{
public:
    enum class Type {
        Line,
        Circle,
        Rectangle,
        Polyline,
        Arc,
        Ellipse,
        Polygon,
        Text,
        LinearDimension,
        Leader,
        Hatch,
        BlockReference
    };

    virtual ~CadEntity() = default;
    virtual Type type() const = 0;
    virtual QGraphicsItem* createGraphicsItem() const = 0;
    virtual QJsonObject toJson() const = 0;
    virtual std::unique_ptr<CadEntity> clone() const = 0;

    virtual void translate(const QPointF& delta) = 0;
    virtual void rotate(const QPointF& center, double angleDeg) = 0;
    virtual void scale(const QPointF& center, double factor) = 0;
    virtual void mirror(const QPointF& axisA, const QPointF& axisB) = 0;

    QString layer() const { return m_layer; }
    void setLayer(const QString& layer) { m_layer = layer; }

    QColor color() const { return m_color; }
    void setColor(const QColor& color) { m_color = color; }

    double lineWeight() const { return m_lineWeight; }
    void setLineWeight(double weight) { m_lineWeight = weight; }

    QString lineType() const { return m_lineType; }
    void setLineType(const QString& lineType) { m_lineType = lineType; }

    void copyStyleTo(CadEntity& target) const;

    static std::unique_ptr<CadEntity> fromJson(const QJsonObject& obj);

protected:
    QPen buildPen() const;
    void writeCommonJson(QJsonObject& obj) const;
    void readCommonJson(const QJsonObject& obj);

private:
    QString m_layer = "0";
    QColor  m_color = Qt::white;
    double  m_lineWeight = 0.0;
    QString m_lineType = "Continuous";
};

class CadLine final : public CadEntity
{
public:
    CadLine() = default;
    CadLine(const QPointF& start, const QPointF& end);

    Type type() const override { return Type::Line; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadLine> fromJsonObject(const QJsonObject& obj);

    QPointF start() const { return m_start; }
    QPointF end() const { return m_end; }
    void setStart(const QPointF& p) { m_start = p; }
    void setEnd(const QPointF& p) { m_end = p; }
    void setEndpoints(const QPointF& start, const QPointF& end) { m_start = start; m_end = end; }

private:
    QPointF m_start;
    QPointF m_end;
};

class CadCircle final : public CadEntity
{
public:
    CadCircle() = default;
    CadCircle(const QPointF& center, double radius);

    Type type() const override { return Type::Circle; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadCircle> fromJsonObject(const QJsonObject& obj);

    QPointF center() const { return m_center; }
    double radius() const { return m_radius; }

private:
    QPointF m_center;
    double m_radius = 0.0;
};

class CadRectangle final : public CadEntity
{
public:
    CadRectangle() = default;
    explicit CadRectangle(const QRectF& rect);

    Type type() const override { return Type::Rectangle; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadRectangle> fromJsonObject(const QJsonObject& obj);

    QRectF rect() const { return m_rect; }

private:
    QRectF m_rect;
};

class CadPolyline final : public CadEntity
{
public:
    CadPolyline() = default;
    CadPolyline(const QVector<QPointF>& points, bool closed = false);

    Type type() const override { return Type::Polyline; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadPolyline> fromJsonObject(const QJsonObject& obj);

    const QVector<QPointF>& points() const { return m_points; }
    bool closed() const { return m_closed; }
    void setPoints(const QVector<QPointF>& points) { m_points = points; }
    void setClosed(bool closed) { m_closed = closed; }

private:
    QVector<QPointF> m_points;
    bool m_closed = false;
};

class CadArc final : public CadEntity
{
public:
    CadArc() = default;
    CadArc(const QPointF& center, double radius, double startAngleDeg, double spanAngleDeg);

    Type type() const override { return Type::Arc; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadArc> fromJsonObject(const QJsonObject& obj);

    QPointF center() const { return m_center; }
    double radius() const { return m_radius; }
    double startAngleDeg() const { return m_startAngleDeg; }
    double spanAngleDeg() const { return m_spanAngleDeg; }
    void setStartAngleDeg(double angleDeg) { m_startAngleDeg = angleDeg; }
    void setSpanAngleDeg(double spanDeg) { m_spanAngleDeg = spanDeg; }

private:
    QPointF m_center;
    double m_radius = 0.0;
    double m_startAngleDeg = 0.0;
    double m_spanAngleDeg = 0.0;
};

class CadEllipse final : public CadEntity
{
public:
    CadEllipse() = default;
    explicit CadEllipse(const QRectF& rect);

    Type type() const override { return Type::Ellipse; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadEllipse> fromJsonObject(const QJsonObject& obj);

    QRectF rect() const { return m_rect; }

private:
    QRectF m_rect;
};

class CadPolygon final : public CadEntity
{
public:
    CadPolygon() = default;
    CadPolygon(const QPointF& center, double radius, int sides, double rotationDeg = -90.0);

    Type type() const override { return Type::Polygon; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadPolygon> fromJsonObject(const QJsonObject& obj);

    QPointF center() const { return m_center; }
    double radius() const { return m_radius; }
    int sides() const { return m_sides; }
    double rotationDeg() const { return m_rotationDeg; }

private:
    QPointF m_center;
    double m_radius = 0.0;
    int m_sides = 6;
    double m_rotationDeg = -90.0;
};




class CadHatch final : public CadEntity
{
public:
    CadHatch() = default;
    CadHatch(const QVector<QPointF>& boundary, const QString& pattern = QStringLiteral("ANSI31"),
             double scale = 1.0, double angleDeg = 45.0);
    CadHatch(const QVector<QVector<QPointF>>& loops, const QString& pattern = QStringLiteral("ANSI31"),
             double scale = 1.0, double angleDeg = 45.0);

    Type type() const override { return Type::Hatch; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadHatch> fromJsonObject(const QJsonObject& obj);

    const QVector<QPointF>& boundary() const { return m_boundary; }
    const QVector<QVector<QPointF>>& loops() const { return m_loops; }
    QString pattern() const { return m_pattern; }
    double hatchScale() const { return m_scale; }
    double angleDeg() const { return m_angleDeg; }
    void setPattern(const QString& pattern) { m_pattern = pattern.trimmed().isEmpty() ? QStringLiteral("ANSI31") : pattern.trimmed(); }
    void setHatchScale(double scale) { m_scale = qMax(0.01, scale); }
    void setAngleDeg(double angleDeg) { m_angleDeg = angleDeg; }

private:
    QVector<QPointF> m_boundary;
    QVector<QVector<QPointF>> m_loops;
    QString m_pattern = "ANSI31";
    double m_scale = 1.0;
    double m_angleDeg = 45.0;
};


class CadBlockReference final : public CadEntity
{
public:
    CadBlockReference() = default;
    CadBlockReference(const QString& name, const QPointF& basePoint, const QPointF& insertionPoint,
                      const QJsonArray& entities, double scaleFactor = 1.0, double rotationDeg = 0.0);

    Type type() const override { return Type::BlockReference; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadBlockReference> fromJsonObject(const QJsonObject& obj);

    QString blockName() const { return m_name; }
    QPointF basePoint() const { return m_basePoint; }
    QPointF insertionPoint() const { return m_insertionPoint; }
    double scaleFactor() const { return m_scaleFactor; }
    double rotationDeg() const { return m_rotationDeg; }
    QJsonArray definitionEntities() const { return m_entities; }

private:
    QString m_name = "Block";
    QPointF m_basePoint;
    QPointF m_insertionPoint;
    QJsonArray m_entities;
    double m_scaleFactor = 1.0;
    double m_rotationDeg = 0.0;
};

class CadText final : public CadEntity
{
public:
    CadText() = default;
    CadText(const QPointF& position, const QString& text, double height = 8.0, double rotationDeg = 0.0,
            double widthFactor = 1.0, double obliqueAngleDeg = 0.0, const QString& fontName = QStringLiteral("TXT"));

    Type type() const override { return Type::Text; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadText> fromJsonObject(const QJsonObject& obj);

    QPointF position() const { return m_position; }
    QString text() const { return m_text; }
    double height() const { return m_height; }
    double rotationDeg() const { return m_rotationDeg; }
    double widthFactor() const { return m_widthFactor; }
    double obliqueAngleDeg() const { return m_obliqueAngleDeg; }
    QString fontName() const { return m_fontName; }
    int horizontalJustification() const { return m_horizontalJustification; }
    int verticalJustification() const { return m_verticalJustification; }
    double boxWidth() const { return m_boxWidth; }
    double lineSpacingFactor() const { return m_lineSpacingFactor; }
    bool isMText() const { return m_isMText; }
    void setText(const QString& text) { m_text = text.trimmed().isEmpty() ? QStringLiteral("Text") : text; }
    void setHeight(double height) { m_height = qMax(1.0e-6, height); }
    void setRotationDeg(double rotationDeg) { m_rotationDeg = rotationDeg; }
    void setWidthFactor(double widthFactor) { m_widthFactor = qBound(0.01, widthFactor, 100.0); }
    void setObliqueAngleDeg(double angleDeg) { m_obliqueAngleDeg = qBound(-85.0, angleDeg, 85.0); }
    void setFontName(const QString& fontName) { m_fontName = fontName.trimmed().isEmpty() ? QStringLiteral("TXT") : fontName.trimmed(); }
    void setHorizontalJustification(int justification) { m_horizontalJustification = justification; }
    void setVerticalJustification(int justification) { m_verticalJustification = justification; }
    void setBoxWidth(double width) { m_boxWidth = qIsFinite(width) && width > 1.0e-9 ? width : 0.0; }
    void setLineSpacingFactor(double factor) { m_lineSpacingFactor = qBound(0.25, factor, 4.0); }
    void setMText(bool isMText) { m_isMText = isMText; }

private:
    QPointF m_position;
    QString m_text = "Text";
    double m_height = 8.0;
    double m_rotationDeg = 0.0;
    double m_widthFactor = 1.0;
    double m_obliqueAngleDeg = 0.0;
    QString m_fontName = "TXT";
    int m_horizontalJustification = 0;
    int m_verticalJustification = 0;
    double m_boxWidth = 0.0;
    double m_lineSpacingFactor = 1.0;
    bool m_isMText = false;
};

class CadLinearDimension final : public CadEntity
{
public:
    CadLinearDimension() = default;
    CadLinearDimension(const QPointF& first, const QPointF& second, const QPointF& dimensionPoint, double textHeight = 6.0, double arrowSize = 0.0);

    Type type() const override { return Type::LinearDimension; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadLinearDimension> fromJsonObject(const QJsonObject& obj);

    QPointF first() const { return m_first; }
    QPointF second() const { return m_second; }
    QPointF dimensionPoint() const { return m_dimensionPoint; }
    double textHeight() const { return m_textHeight; }
    double arrowSize() const { return m_arrowSize; }
    void setFirst(const QPointF& p) { m_first = p; }
    void setSecond(const QPointF& p) { m_second = p; }
    void setTextHeight(double height) { m_textHeight = qMax(1.0e-6, height); }
    void setArrowSize(double arrowSize) { m_arrowSize = qMax(0.0, arrowSize); }
    double measuredLength() const;

private:
    QPointF m_first;
    QPointF m_second;
    QPointF m_dimensionPoint;
    double m_textHeight = 6.0;
    double m_arrowSize = 0.0;
};

class CadLeader final : public CadEntity
{
public:
    CadLeader() = default;
    CadLeader(const QPointF& arrowPoint, const QPointF& textPoint, const QString& text, double textHeight = 6.0);

    Type type() const override { return Type::Leader; }
    QGraphicsItem* createGraphicsItem() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<CadEntity> clone() const override;
    void translate(const QPointF& delta) override;
    void rotate(const QPointF& center, double angleDeg) override;
    void scale(const QPointF& center, double factor) override;
    void mirror(const QPointF& axisA, const QPointF& axisB) override;
    static std::unique_ptr<CadLeader> fromJsonObject(const QJsonObject& obj);

    QPointF arrowPoint() const { return m_arrowPoint; }
    QPointF textPoint() const { return m_textPoint; }
    QString text() const { return m_text; }
    double textHeight() const { return m_textHeight; }
    void setArrowPoint(const QPointF& p) { m_arrowPoint = p; }
    void setTextPoint(const QPointF& p) { m_textPoint = p; }

private:
    QPointF m_arrowPoint;
    QPointF m_textPoint;
    QString m_text = "Leader";
    double m_textHeight = 6.0;
};
