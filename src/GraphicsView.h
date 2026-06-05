#pragma once
#include <QGraphicsView>
#include <QPointF>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QVector>
#include <QString>
#include <QPointer>
#include <memory>

class QGraphicsItem;
class QPainter;
class QTimer;
class QLineEdit;
class QEvent;
class QResizeEvent;
class CadDocument;
class CadEntity;
class CadText;

class GraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    enum class DrawingTool {
        Noe,
        Line,
        Polyline,
        Circle,
        CircleDiameter,
        Circle3Points,
        Arc3Points,
        Rectangle,
        Ellipse,
        Polygon,
        Spline,
        Text,
        LinearDimension,
        Leader,
        Hatch
    };
    Q_ENUM(DrawingTool)

    explicit GraphicsView(QWidget* parent = nullptr);

    void setDocument(CadDocument* document);
    CadDocument* document() const { return m_document; }

    void setDrawingTool(DrawingTool tool);
    DrawingTool drawingTool() const { return m_drawingTool; }
    static QString drawingToolName(DrawingTool tool);

    // QCAD Draw compatibility layer.
    // Origin reference: QCAD CE scripts/Draw/* action family.
    // DWGView does not copy QCAD ECMAScript code; it maps QCAD Draw commands
    // to native Qt/GraphicsView CAD tools and records the mapping in the DRAW console.
    void setFixedAngleConstraint(bool enabled, double angleDeg = 0.0, const QString& label = QString());
    void clearFixedAngleConstraint();
    QString fixedAngleConstraintLabel() const { return m_fixedAngleConstraintLabel; }
    bool processDrawConsoleCommand(const QString& command);
    QString currentDrawPrompt() const;
    QString activeCommandName() const;
    void repeatLastDrawCommand();

    void setPolygonSides(int sides);
    int polygonSides() const { return m_polygonSides; }

    void setAnnotationText(const QString& text);
    QString annotationText() const { return m_annotationText; }
    void setAnnotationTextHeight(double height);
    double annotationTextHeight() const { return m_annotationTextHeight; }
    void setAnnotationTextWidthFactor(double factor);
    double annotationTextWidthFactor() const { return m_annotationTextWidthFactor; }
    void setAnnotationTextObliqueAngle(double angleDeg);
    double annotationTextObliqueAngle() const { return m_annotationTextObliqueAngleDeg; }
    void setAnnotationFontName(const QString& fontName);
    QString annotationFontName() const { return m_annotationFontName; }

    void setHatchPattern(const QString& pattern);
    QString hatchPattern() const { return m_hatchPattern; }
    void setHatchScale(double scale);
    double hatchScale() const { return m_hatchScale; }
    void setHatchAngleDeg(double angleDeg);
    double hatchAngleDeg() const { return m_hatchAngleDeg; }

    void setGridEnabled(bool enabled);
    bool gridEnabled() const { return m_gridEnabled; }

    void setSnapEnabled(bool enabled);
    bool snapEnabled() const { return m_snapEnabled; }

    void setOrthoEnabled(bool enabled);
    bool orthoEnabled() const { return m_orthoEnabled; }

    enum class ObjectSnapMode {
        Endpoint,
        Midpoint,
        Center,
        Quadrant,
        Intersection,
        Tangent,
        Vertex
    };
    Q_ENUM(ObjectSnapMode)

    void setObjectSnapEnabled(bool enabled);
    bool objectSnapEnabled() const { return m_objectSnapEnabled; }

    void setObjectSnapModeEnabled(ObjectSnapMode mode, bool enabled);
    bool objectSnapModeEnabled(ObjectSnapMode mode) const;
    static QString objectSnapModeName(ObjectSnapMode mode);

    void setGridSpacing(double spacing);
    double gridSpacing() const { return m_gridSpacing; }

    QVector<int> selectedEntityIndices() const;
    bool hasCadSelection() const;

    // Mode Modifier type AutoCAD/QCAD: l'utilisateur choisit une commande,
    // sélectionne les objets dans la scène, puis valide les étapes avec Entrée.
    bool startInteractiveModifyCommand(const QString& command);
    bool interactiveModifyCommandActive() const { return !m_interactiveModifyCommand.isEmpty(); }
    bool commitInteractiveModifyStage();
    void cancelInteractiveModifyCommand();
    void cancelActiveAction();

    void deleteSelectedEntities();
    void copySelectedEntities(const QPointF& offset);
    void moveSelectedEntities(const QPointF& offset);
    void rotateSelectedEntities(double angleDeg);
    void scaleSelectedEntities(double factor);
    void mirrorSelectedEntitiesHorizontal();
    void mirrorSelectedEntitiesVertical();
    void moveSelectedEntitiesToLayer(const QString& layerName);
    void offsetSelectedEntities(double distance);
    bool trimSelectedLinesToIntersection();
    bool extendSelectedLinesToIntersection();
    bool chamferSelectedLines(double distance);
    bool filletSelectedLines(double radius);

    // P1/P3 CAD commands exposed to MainWindow / Command Line
    bool closeOrOpenSelectedPolylines(bool closed);
    bool smoothSelectedPolylines();
    bool editSelectedText(const QString& text, double height, double rotationDeg);
    bool editSelectedHatches(const QString& pattern, double scale, double angleDeg);
    bool createHatchFromSelectedClosedBoundaries();
    bool matchPropertiesFromFirstSelection();
    bool addCenterMarksToSelectedCircles(double size);
    bool convertSelectedCirclesToDiameterDimensions(double offset);
    bool convertSelectedCirclesToRadiusDimensions(double offset);
    void refreshFromDocumentPreservingSelection(const QVector<int>& indices = {});
    void prepareForSceneReset();

    // Navigation robuste pour gros fichiers DWG
    void zoomIn();
    void zoomOut();
    void zoomBy(double factor);
    void zoomByAt(double factor, const QPoint& viewportAnchor);
    void zoomToExtents();
    void zoomToImportExtents();
    void resetZoom();
    void startZoomWindowMode();
    QRectF contentBoundingRect() const;
    void updateSceneRectToContent();
    void configureForDocumentSize(int entityCount);
    void updateLevelOfDetail();
    void setPerformanceModeEnabled(bool enabled);
    bool performanceModeEnabled() const { return m_performanceModeEnabled; }

signals:
    void mouseScenePositionChanged(const QPointF& scenePos);
    void drawEventLogged(const QString& message);
    void drawPromptChanged(const QString& prompt);
    void interactiveModifyCommandReady(const QString& command);

protected:
    void wheelEvent(QWheelEvent* event)         override;
    void mousePressEvent(QMouseEvent* event)    override;
    void mouseMoveEvent(QMouseEvent* event)     override;
    void mouseReleaseEvent(QMouseEvent* event)  override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event)        override;
    void keyReleaseEvent(QKeyEvent* event)      override;
    void resizeEvent(QResizeEvent* event)       override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    QRectF normalizedRect(const QPointF& a, const QPointF& b) const;
    QRectF smartImportBoundingRect() const;
    QRectF circleRect(const QPointF& center, const QPointF& edge) const;
    QRectF circleDiameterRect(const QPointF& a, const QPointF& b) const;
    QVector<QPointF> regularPolygonPoints(const QPointF& center, const QPointF& edge) const;

    QPointF gridSnappedPoint(const QPointF& point) const;
    QPointF orthoConstrainedPoint(const QPointF& anchor, const QPointF& point) const;
    QPointF fixedAngleConstrainedPoint(const QPointF& anchor, const QPointF& point) const;
    struct SnapCandidate {
        QPointF point;
        ObjectSnapMode mode;
        int entityIndex = -1;
    };

    QPointF effectiveScenePoint(const QPointF& rawPoint, const QPointF* orthoAnchor = nullptr);
    bool nearestObjectSnapPoint(const QPointF& rawPoint, SnapCandidate& snappedCandidate, const QPointF* tangentAnchor = nullptr) const;
    void collectSnapCandidates(QVector<SnapCandidate>& candidates, const QPointF& rawPoint, const QPointF* tangentAnchor = nullptr) const;
    QVector<int> nearbyEntityIndices(const QPointF& rawPoint, const QPointF* tangentAnchor = nullptr) const;
    void setCurrentSnapMarker(const SnapCandidate* candidate);
    void drawObjectSnapMarker(QPainter* painter) const;
    void drawDimensionPickMarkers(QPainter* painter) const;
    void drawSelectionHighlight(QPainter* painter) const;
    void drawZoomWindowRubberBand(QPainter* painter) const;
    void finishZoomWindow(const QRect& viewportRect);
    void beginInteractiveNavigation();
    void endInteractiveNavigation();
    QGraphicsItem* cadItemAt(const QPoint& viewPos) const;
    int cadEntityIndexAt(const QPoint& viewPos, QGraphicsItem** hitItem = nullptr, const QString& commandFilter = QString()) const;
    bool entityAcceptedForInteractiveCommand(const QString& command, const CadEntity* entity) const;
    bool dimensionReferencePointAt(const QPoint& viewPos, const QPointF& rawScenePoint, QPointF& outPoint, QString* outLabel = nullptr, int* outEntityIndex = nullptr) const;
    void updateDimensionReferencePreview(const QPoint& viewPos, const QPointF& rawScenePoint);
    void clearDimensionReferencePreview();
    void addDimensionPickMarker(const QPointF& point);
    void clearDimensionPickMarkers();
    QVector<int> operationSelectionIndices() const;
    void clearValidatedModifySelection();
    int textEntityIndexAt(const QPoint& viewPos) const;
    void startTextInlineEdit(int entityIndex, QGraphicsItem* sourceItem);
    void finishTextInlineEdit(bool acceptChanges);
    void cancelTextInlineEdit();
    void updateTextInlineEditorGeometry();
    QRectF textEditorSceneRect(const CadText* text) const;
    QPointF selectionMovePoint(const QPoint& viewPos) const;

    void updatePreviewItem(const QPointF& scenePos);
    void logDrawEvent(const QString& message);
    void emitDrawPrompt();
    bool parseConsolePoint(const QString& text, QPointF& point, bool& relative) const;
    bool parseConsolePolar(const QString& text, double& distance, double& angleDeg) const;
    bool parseCommandOption(const QString& raw);
    bool processConsolePoint(const QPointF& scenePos);
    bool processConsoleDistance(double distance);
    bool processConsoleAngleDistance(double angleDeg, double distance);
    QPointF commandAnchorPoint() const;
    void undoLastCommandPoint();
    void finishDragDrawing(const QPointF& scenePos);
    void finishPolyline(bool closed);
    void finishClickSequenceIfReady();
    void addCadEntity(std::unique_ptr<CadEntity> cadEntity);
    std::unique_ptr<CadText> makeCurrentTextEntity(const QPointF& scenePos) const;
    QPointF selectedItemsCenter() const;
    void selectEntityIndicesInScene(const QVector<int>& indices);
    void clearCadSelection();
    void logInteractiveModifyPrompt();
    QString interactiveModifyStagePrompt() const;
    void clearPreviewItem();
    void cancelDrawing();
    QRectF safeRectForView(const QRectF& rect) const;
    void fitSceneRectStable(const QRectF& rect, double marginRatio = 0.04);
    QPointF viewportPointToScenePrecise(const QPoint& viewportPoint) const;
    void panByViewportDelta(const QPoint& delta);
    void setCameraTransformAt(const QPointF& sceneAnchor, const QPoint& viewportAnchor, double scaleX, double scaleY);
    void resetScrollbarsForStableCamera();

    bool   m_panning = false;
    bool   m_spacePanActive = false;
    QPoint m_lastPanPos;

    // ZOOM WINDOW natif: sélection rectangle en coordonnées viewport,
    // puis fit stable sur le rectangle scène correspondant.
    bool m_zoomWindowActive = false;
    bool m_zoomWindowDragging = false;
    QPoint m_zoomWindowStart;
    QPoint m_zoomWindowEnd;

    bool m_movingSelection = false;
    bool m_selectionDragMoved = false;
    QPointF m_lastSelectionMovePoint;
    QVector<int> m_movingSelectionIndices;

    // Select document native: pour les DXF/DWG externes en rendu batch,
    // certaines entités sont dessinées dans FastCadBatchItem et n’ont pas de
    // QGraphicsItem individuel. On garde donc les index selecteds ici pour
    // que sélection, Modify, Hatch, Move, Delete, etc. fonctionnent quand même.
    QVector<int> m_documentSelectionIndices;

    // Select par clic CAD: clic sur un objet = sélection; deuxième clic = désélection.
    // Si l'objet est glissé, le clic devient déplacement et ne désélectionne pas au relâchement.
    bool m_pendingSelectionClick = false;
    bool m_pendingSelectionWasSelected = false;
    int m_pendingSelectionIndex = -1;
    QGraphicsItem* m_pendingSelectionItem = nullptr;
    QPoint m_pendingSelectionPressPos;

    // Séquence Modifier: exemple TRIM/MATCHPROP/EXTEND.
    // Étape 0: sélectionner le ou les objets sources/limites, Entrée.
    // Étape 1: sélectionner le ou les objets cibles, Entrée -> exécution.
    QString m_interactiveModifyCommand;
    int m_interactiveModifyStage = 0;
    QVector<int> m_interactiveModifyFirstSelection;
    QVector<int> m_interactiveModifyCurrentSelection;
    QVector<int> m_validatedModifySelection;

    // EXTEND doit conserver le rôle des sélections:
    // 1) source ouverte à deux extrémités, 2) entité limite cible à respecter.
    QVector<int> m_pendingExtendSourceSelection;
    QVector<int> m_pendingExtendBoundarySelection;

    // MATCHPROP conserve également le rôle source/cibles, car selectedEntityIndices() trie
    // les index et ne peut pas représenter seul l'ordre de sélection utilisateur.
    QVector<int> m_pendingMatchPropSourceSelection;
    QVector<int> m_pendingMatchPropTargetSelection;

    DrawingTool    m_drawingTool = DrawingTool::Noe;
    bool           m_drawing = false;
    QPointF        m_drawStart;
    QPointF        m_lastCursorScenePos;
    bool           m_hasLastCursorScenePos = false;
    QString        m_keyboardInputBuffer;
    QString        m_lastDrawCommand;
    QVector<QPointF> m_points;
    QGraphicsItem* m_previewItem = nullptr;
    CadDocument*  m_document = nullptr;
    int m_polygonSides = 6;
    QString m_annotationText = "Text";
    double m_annotationTextHeight = 6.0;
    double m_annotationTextWidthFactor = 1.0;
    double m_annotationTextObliqueAngleDeg = 0.0;
    QString m_annotationFontName = "TXT";
    QString m_hatchPattern = "ANSI31";
    double m_hatchScale = 1.0;
    double m_hatchAngleDeg = 45.0;

    bool m_gridEnabled = true;
    bool m_snapEnabled = false;
    bool m_orthoEnabled = false;
    bool m_objectSnapEnabled = true;

    bool m_fixedAngleConstraintEnabled = false;
    double m_fixedAngleConstraintDeg = 0.0;
    QString m_fixedAngleConstraintLabel;
    bool m_snapEndpointEnabled = true;
    bool m_snapMidpointEnabled = true;
    bool m_snapCenterEnabled = true;
    bool m_snapQuadrantEnabled = true;
    bool m_snapIntersectionEnabled = true;
    bool m_snapTangentEnabled = true;
    bool m_snapVertexEnabled = true;
    bool m_hasCurrentSnapMarker = false;
    SnapCandidate m_currentSnapCandidate;

    // Marqueurs propres aux cotations: ils confirment une référence détectée
    // par le moteur auto-référence de cotation. Ce moteur reprend la logique
    // OSNAP, mais fonctionne même si F3/F9 sont disableds.
    QVector<QPointF> m_dimensionPickMarkers;
    bool m_hasDimensionReferencePreview = false;
    QPointF m_dimensionReferencePreviewPoint;
    QString m_dimensionReferencePreviewLabel;

    double m_gridSpacing = 10.0;
    double m_objectSnapTolerancePx = 12.0;
    bool m_largeDocumentMode = false;
    QRectF m_cachedContentBounds;
    double m_lastLodScale = -1.0;
    bool m_performanceModeEnabled = true;
    bool m_interactiveNavigation = false;
    QTimer* m_navigationRestoreTimer = nullptr;

    // Edition directe des textes: double-clic sur CadText -> QLineEdit superposé.
    // Origin fonctionnelle: comportement CAD type QCAD/DDEDIT, implémentation native DWGView.
    QPointer<QLineEdit> m_textInlineEditor;
    QTimer* m_textEditBlinkTimer = nullptr;
    int m_editingTextIndex = -1;
    QGraphicsItem* m_editingTextItem = nullptr;
    qreal m_editingTextOriginalOpacity = 1.0;
    bool m_editingTextBlinkVisible = true;
};
