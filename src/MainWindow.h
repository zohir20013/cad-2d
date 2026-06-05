#pragma once
#include <QMainWindow>
#include <QMap>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QSizeF>
#include "cad/LayerInfo.h"
#include "cad/CadDocument.h"

class GraphicsView;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QTableWidget;
class QDockWidget;
class QPainter;
class QPrinter;
class QLineEdit;
class QEvent;
class QDockWidget;
class QPlainTextEdit;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void newDrawing();
    void openFile();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void exportSvg();
    void exportDxf();
    void exportDwgViaOda();
    void configurePageLayout();
    void exportPdf();
    void printDrawing();
    void configureDrawing();
    void changeBackground();
    void onLayerToggled(QTreeWidgetItem* item, int column);
    void onCurrentLayerComboChanged(const QString& name);
    void createLayer();
    void deleteLayer();
    void renameLayer();
    void setSelectedLayerCurrent();
    void changeSelectedLayerColor();
    void changeSelectedLayerLineWeight();
    void changeSelectedLayerLineType();
    void moveSelectionToCurrentLayer();
    void updatePropertiesPanel();
    void applySelectedObjectProperties();
    void changeSelectedObjectColor();
    void createBlockFromSelection();
    void createEmptyBlock();
    void insertBlock();
    void renameBlock();
    void duplicateBlock();
    void removeBlock();
    void purgeUnusedBlocks();
    void explodeSelectedBlocks();
    void showAllBlocks();
    void hideAllBlocks();
    void selectBlockReferences();
    void deselectBlockReferences();
    void showBlockManager();
    void undoCommand();
    void redoCommand();
    void showCommandLine();
    void executeCommandLine();
    void appendCommandConsoleMessage(const QString& message);
    void executeCadCommand(const QString& command);
    void attachRasterImage();
    void toggleCleanScreen();
    void showWorkspaceManager();
    void applyWorkspacePreset(const QString& preset);
    void showAllCadPalettes();
    void showNamedViewsManager();
    void showViewportManager();
    void attachDwgReference();
    void attachDwfUnderlay();
    void attachDgnUnderlay();
    void loadCadApplication();
    void newWindowFromCurrentDrawing();
    void cascadeWindows();
    void tileWindows();
    void toggleQtCadScene(bool checked);
    void refreshCadScene();

private:
    void buildLayersPanel();
    void buildDocumentLayersPanel();
    void rebuildDocumentLayersPanelLater();
    void buildPropertiesPanel();
    void optimizeDrawingWorkspaceLayout();
    void fillGeometryProperties(int entityIndex);
    void applyLayerVisibility();
    void updateWindowTitle();
    QSizeF layoutPageSizeMm() const;
    double drawingUnitToMillimeters() const;
    void renderLayoutPage(QPainter* painter, const QRectF& pageRectMm) const;
    bool saveProjectToPath(const QString& path);
    QString selectedDocumentLayerName() const;
    void setCurrentDrawingLayer(const QString& name, bool makeDrawable = true);
    void pushUndoState(const QString& label = QString());
    void rollbackUndoIfNeeded();
    void restoreDocumentSnapshot(const QJsonObject& snapshot);
    void clearUndoHistory();
    bool attachExternalCadFile(const QString& path, const QString& kind);
    void registerExternalReference(const QString& kind, const QString& path, const QString& note = QString());
    void showTextStyleDialog();
    void showHatchStyleDialog(bool editSelectedHatches = false, bool startHatchAfterOk = false);
    void applyBlockVisibility();

    GraphicsView*  m_view;
    QTreeWidget*   m_layersTree;
    QComboBox*     m_currentLayerCombo = nullptr;
    QLabel*        m_coordLabel;

    QDockWidget*    m_layersDock = nullptr;
    QDockWidget*    m_propertiesDock = nullptr;
    QLabel*         m_selectionInfoLabel = nullptr;
    QComboBox*      m_objectLayerCombo = nullptr;
    QPushButton*    m_objectColorButton = nullptr;
    QDoubleSpinBox* m_objectLineWeightSpin = nullptr;
    QComboBox*      m_objectLineTypeCombo = nullptr;
    QTableWidget*   m_geometryTable = nullptr;

    CadDocument   m_document;
    QMap<QString, LayerInfo> m_layers;
    QString        m_currentFile;
    QString        m_projectFile;

    QString        m_layoutPageFormat = "A4";
    bool           m_layoutLandscape = true;
    double         m_layoutMarginMm = 10.0;
    double         m_layoutTitleBlockHeightMm = 25.0;
    bool           m_layoutFitToPage = true;
    double         m_layoutScaleDenominator = 100.0;
    QString        m_layoutTitle = "2D CAD Drawing";

    QStringList    m_textStyleNames = {"Legend", "ROMANS", "STANDARD"};
    QString        m_currentTextStyleName = "STANDARD";
    QString        m_currentTextFontName = "TXT";
    QString        m_currentTextFontStyle = "Regular";
    bool           m_currentTextAnnotative = true;
    double         m_currentTextWidthFactor = 1.0;
    double         m_currentTextObliqueAngle = 0.0;

    QVector<QJsonObject> m_undoStack;
    QVector<QJsonObject> m_redoStack;
    QStringList          m_undoLabels;
    QStringList          m_redoLabels;
    QDockWidget*         m_commandDock = nullptr;
    QLineEdit*           m_commandLineEdit = nullptr;
    QPlainTextEdit*      m_commandHistoryText = nullptr;
    bool                 m_cleanScreenActive = false;
    bool                 m_blocksVisible = true;

    // Vrai uniquement pendant l'exécution réelle d'une commande Modify déjà validée
    // par le mode séquentiel. Empêche executeCadCommand() de relancer la sélection
    // interactive au lieu d'appliquer l'opération.
    bool                 m_executingValidatedModifyCommand = false;

    bool                 m_rebuildingLayersPanel = false;
    bool                 m_pendingLayersPanelRebuild = false;
    QMap<QString, QRectF> m_namedViews;
    QJsonArray            m_externalReferences;
};
