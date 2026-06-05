#pragma once

#include "cad/CadEntity.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <memory>

namespace CadModify {

enum class LengthenAnchor {
    Start,
    End,
    Center
};

struct ModifyReport {
    bool success = false;
    QString message;
    int createdCount = 0;
    QVector<QPointF> constructionPoints;
};

struct BreakResult {
    bool valid = false;
    QVector<CadLine> pieces;
};

struct DivideResult {
    bool valid = false;
    QVector<QPointF> points;
};

QVector<QPointF> gripPoints(const CadEntity& entity, int curveSegments = 64);
std::unique_ptr<CadEntity> stretchByCrossingWindow(const CadEntity& entity, const QRectF& crossingWindow,
                                                   const QPointF& delta, int curveSegments = 64);

CadLine lengthenLineToTotal(const CadLine& line, double targetLength, LengthenAnchor anchor = LengthenAnchor::End);
CadLine lengthenLineByDelta(const CadLine& line, double deltaLength, LengthenAnchor anchor = LengthenAnchor::End);
BreakResult breakLineAtPoints(const CadLine& line, const QVector<QPointF>& breakPoints, double gap = 0.0);
DivideResult divideLineByCount(const CadLine& line, int divisions);
DivideResult measureLineBySpacing(const CadLine& line, double spacing, bool includeRemainderPoint = false);

std::unique_ptr<CadPolyline> joinConnectedLinesToPolyline(const QVector<CadLine>& lines, double tolerance = 1.0e-6);
std::vector<std::unique_ptr<CadEntity>> explodeToPrimitives(const CadEntity& entity, int curveSegments = 64);

std::unique_ptr<CadEntity> alignByTwoPoints(const CadEntity& entity,
                                            const QPointF& sourceA, const QPointF& sourceB,
                                            const QPointF& targetA, const QPointF& targetB,
                                            bool scaleToTarget = false);
std::vector<std::unique_ptr<CadEntity>> rectangularArray(const CadEntity& entity, int rows, int columns,
                                                     double rowSpacing, double columnSpacing);
std::vector<std::unique_ptr<CadEntity>> polarArray(const CadEntity& entity, const QPointF& center,
                                               int count, double totalAngleDeg, bool rotateItems = true);
std::vector<std::unique_ptr<CadEntity>> copyAlongPolyline(const CadEntity& entity, const QVector<QPointF>& path,
                                                      bool closed, double spacing, bool alignToPath = true);

ModifyReport joinSummary(const QVector<CadLine>& lines, double tolerance = 1.0e-6);

} // namespace CadModify
