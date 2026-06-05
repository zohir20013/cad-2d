#include "MainWindow.h"
#include "GraphicsView.h"
#include "cad/LayerInfo.h"
#include "DxfLoader.h"
#include "cad_import/CadImportLibrary.h"
#include "cad_import/OdaFileConverter.h"
#include "DebugLogger.h"

#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QDockWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStatusBar>
#include <QLabel>
#include <QColorDialog>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include <QIcon>
#include <QActionGroup>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QInputDialog>
#include <QPainter>
#include <QSvgGenerator>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSplitter>
#include <QHBoxLayout>
#include <QWidget>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QKeyEvent>
#include <QEvent>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTimer>
#include <QFormLayout>
#include <QBrush>
#include <QPrinter>
#include <QPrintDialog>
#include <QPageSize>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QProgressDialog>
#include <QEventLoop>
#include <QPointer>
#include <QThread>
#include <QScrollBar>
#include <QSet>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QListWidget>
#include <QFontComboBox>
#include <QCheckBox>
#include <QGridLayout>
#include <QSizePolicy>
#include <QTabWidget>
#include <QFrame>
#include <QRadioButton>

#include <utility>
#include <memory>
#include <QClipboard>
#include <QToolButton>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QLibrary>
#include <QGraphicsRectItem>
#include <QScreen>
#include <cmath>
#include <QtMath>

// Icon policy: all SVG icons in resources/icons are DWGView-original.
// QCAD names are used only as origin/mapping labels for menu compatibility.

namespace {
constexpr double kCadPi = 3.141592653589793238462643383279502884;

struct AsyncCadImportResult
{
    bool ok = false;
    QString importMessage;
    CadImportReport importReport;
    CadDocument document;
    QMap<QString, LayerInfo> layers;
};

QString canonicalInteractiveModifyCommand(QString command)
{
    command = command.trimmed().toUpper();
    if (command == QStringLiteral("TR")) return QStringLiteral("TRIM");
    if (command == QStringLiteral("EX")) return QStringLiteral("EXTEND");
    if (command == QStringLiteral("CHA")) return QStringLiteral("CHAMFER");
    if (command == QStringLiteral("F")) return QStringLiteral("FILLET");
    if (command == QStringLiteral("M") || command == QStringLiteral("TRANSLATE")) return QStringLiteral("MOVE");
    if (command == QStringLiteral("CO") || command == QStringLiteral("CP") || command == QStringLiteral("COPYCLIP")) return QStringLiteral("COPY");
    if (command == QStringLiteral("RO")) return QStringLiteral("ROTATE");
    if (command == QStringLiteral("SC")) return QStringLiteral("SCALE");
    if (command == QStringLiteral("MI")) return QStringLiteral("MIRROR");
    if (command == QStringLiteral("MIRRORH")) return QStringLiteral("FLIPHORIZONTAL");
    if (command == QStringLiteral("MIRRORV")) return QStringLiteral("FLIPVERTICAL");
    if (command == QStringLiteral("O")) return QStringLiteral("OFFSET");
    if (command == QStringLiteral("E") || command == QStringLiteral("DELETE") || command == QStringLiteral("CUTCLIP")) return QStringLiteral("ERASE");
    if (command == QStringLiteral("TEXTEDIT")) return QStringLiteral("DDEDIT");
    if (command == QStringLiteral("HATCHFROMSELECTION")) return QStringLiteral("HATCHSELECT");
    return command;
}

bool isInteractiveModifySelectionCommand(const QString& command)
{
    static const QSet<QString> commands = {
        QStringLiteral("MATCHPROP"),
        QStringLiteral("TRIM"), QStringLiteral("TR"),
        QStringLiteral("EXTEND"), QStringLiteral("EX"),
        QStringLiteral("CHAMFER"), QStringLiteral("CHA"),
        QStringLiteral("FILLET"), QStringLiteral("F"),
        QStringLiteral("MOVE"), QStringLiteral("M"), QStringLiteral("TRANSLATE"), QStringLiteral("STRETCH"),
        QStringLiteral("COPY"), QStringLiteral("CO"), QStringLiteral("CP"), QStringLiteral("COPYBASE"), QStringLiteral("COPYCLIP"),
        QStringLiteral("ROTATE"), QStringLiteral("RO"), QStringLiteral("ROTATE2"),
        QStringLiteral("SCALE"), QStringLiteral("SC"), QStringLiteral("LENGTHEN"),
        QStringLiteral("MIRROR"), QStringLiteral("MI"), QStringLiteral("MIRRORH"), QStringLiteral("MIRRORV"),
        QStringLiteral("FLIPHORIZONTAL"), QStringLiteral("FLIPVERTICAL"),
        QStringLiteral("OFFSET"), QStringLiteral("O"),
        QStringLiteral("ARRAY"),
        QStringLiteral("ERASE"), QStringLiteral("E"), QStringLiteral("DELETE"), QStringLiteral("CUTCLIP"),
        QStringLiteral("PEDIT"), QStringLiteral("HATCHEDIT"), QStringLiteral("HATCHSELECT"), QStringLiteral("HATCHFROMSELECTION"), QStringLiteral("DDEDIT"), QStringLiteral("TEXTEDIT"),
        QStringLiteral("DIMCENTER"), QStringLiteral("DIMDIAMETER"), QStringLiteral("DIMRADIUS")
    };
    return commands.contains(command.trimmed().toUpper());
}

QString interactiveModifyStartMessage(const QString& command)
{
    const QString cmd = canonicalInteractiveModifyCommand(command);
    if (cmd == QStringLiteral("EXTEND"))
        return QStringLiteral("EXTEND: select the open source entity, press Enter, then select the target boundary and press Enter. Esc cancels.");
    if (cmd == QStringLiteral("TRIM"))
        return QStringLiteral("TRIM: select the boundary/first object and press Enter, then select the object to trim and press Enter. Esc cancels.");
    if (cmd == QStringLiteral("CHAMFER"))
        return QStringLiteral("CHAMFER: select the first line and press Enter, then select the second line and press Enter. Esc cancels.");
    if (cmd == QStringLiteral("FILLET"))
        return QStringLiteral("FILLET: select the first line and press Enter, then select the second line and press Enter. Esc cancels.");
    if (cmd == QStringLiteral("MATCHPROP"))
        return QStringLiteral("MATCHPROP: select the source object and press Enter, then select target objects and press Enter. Esc cancels.");
    if (cmd == QStringLiteral("HATCHSELECT") || cmd == QStringLiteral("HATCHFROMSELECTION"))
        return QStringLiteral("HATCH: select one or more closed boundaries, then press Enter to create the hatch. Esc cancels.");
    return QStringLiteral("%1: select the objects, then press Enter to execute. Esc cancels.").arg(cmd);
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    updateWindowTitle();

    // Start inside the available screen area instead of forcing a large
    // fixed-size window. This keeps the UI usable on laptop displays
    // such as 1366x768 while still giving the drawing scene priority.
    const QRect availableScreen = QApplication::primaryScreen()
        ? QApplication::primaryScreen()->availableGeometry()
        : QRect(0, 0, 1280, 720);
    const int initialWidth = qMin(1280, qMin(availableScreen.width(), qMax(760, static_cast<int>(availableScreen.width() * 0.90))));
    const int initialHeight = qMin(820, qMin(availableScreen.height(), qMax(520, static_cast<int>(availableScreen.height() * 0.86))));
    resize(initialWidth, initialHeight);
    move(availableScreen.center() - rect().center());
    setMinimumSize(qMin(760, availableScreen.width()), qMin(520, availableScreen.height()));

    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging |
                   QMainWindow::VerticalTabs);

    // ── Vue centrale : moteur Qt uniquement ─────────────
    // Qt/Qt a ete supprime du flux d'affichage et d'edition.
    // Toutes les fonctions CAD passent par GraphicsView: dessin, snap, selection,
    // zoom/pan et rafraichissement apres import.
    m_view = new GraphicsView(this);
    m_view->setDocument(&m_document);
    m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_view->setMinimumSize(480, 320);
    setCentralWidget(m_view);
    m_view->show();

    refreshCadScene();

    // ── Coordonnées souris dans la status bar ────────────
    m_coordLabel = new QLabel("X: 0.00   Y: 0.00", this);
    statusBar()->addPermanentWidget(m_coordLabel);
    statusBar()->showMessage("Ready");

    connect(m_view, &QGraphicsView::rubberBandChanged,
            [=](QRect, QPointF, QPointF) {});   // placeholder
    connect(m_view, &GraphicsView::drawEventLogged,
            this, &MainWindow::appendCommandConsoleMessage);
    connect(m_view, &GraphicsView::drawPromptChanged,
            this, [this](const QString& prompt) {
                appendCommandConsoleMessage(QStringLiteral("PROMPT: %1").arg(prompt));
                if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(prompt);
            });
    connect(m_view, &GraphicsView::interactiveModifyCommandReady,
            this, [this](const QString& command) {
                appendCommandConsoleMessage(QStringLiteral("MODIFY READY: %1 -> executing on the validated selection.").arg(command));

                if (command == QStringLiteral("EXTEND")) {
                    pushUndoState(tr("Extend"));
                    if (!m_view->extendSelectedLinesToIntersection()) {
                        if (!m_undoStack.isEmpty()) m_undoStack.removeLast();
                        if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast();
                        QMessageBox::information(
                            this,
                            tr("Extend"),
                            tr("Select an open source entity with two endpoints, confirm with Enter, then select a compatible target boundary entity. Closed entities cannot be sources.")
                        );
                        return;
                    }
                    statusBar()->showMessage(tr("EXTEND applied: only the open source was extended; the target boundary remains unchanged."), 3500);
                    return;
                }

                m_executingValidatedModifyCommand = true;
                executeCadCommand(command);
                m_executingValidatedModifyCommand = false;
            });

    // ── Actions ──────────────────────────────────────────
    auto mkAction = [&](const QString& label,
                         const QString& icon,
                         QKeySequence   key) -> QAction*
    {
        auto* a = new QAction(label, this);
        if (!icon.isEmpty()) {
            const QIcon resolvedIcon(icon);
            if (!resolvedIcon.isNull()) {
                a->setIcon(resolvedIcon);
            } else {
                // Fallback keeps toolbar spacing stable if an icon resource is missing.
                QPixmap pm(16, 16);
                pm.fill(Qt::transparent);
                a->setIcon(QIcon(pm));
            }
        }
        if (!key.isEmpty()) a->setShortcut(key);
        return a;
    };


    auto cadIconKey = [](QString key) -> QString {
        QString out;
        key = key.trimmed().toLower();
        for (const QChar ch : key) {
            if (ch.isLetterOrNumber()) {
                out.append(ch);
            } else if (!out.endsWith(QLatin1Char('_')) && !out.isEmpty()) {
                out.append(QLatin1Char('_'));
            }
        }
        while (out.endsWith(QLatin1Char('_'))) out.chop(1);
        return out;
    };

    auto cadCommandIcon = [&](const QString& commandOrKey) -> QString {
        const QString key = cadIconKey(commandOrKey);
        if (key.isEmpty()) return QString();
        // Icons are DWGView-original SVG resources. Menu/function names mirror QCAD command families.
        return QStringLiteral(":/icons/cad/") + key + QStringLiteral(".svg");
    };

    auto setCadIcon = [&](QAction* action, const QString& commandOrKey) {
        if (!action) return;
        const QString iconPath = cadCommandIcon(commandOrKey);
        if (!iconPath.isEmpty()) action->setIcon(QIcon(iconPath));
    };

    auto setCadMenuIcon = [&](QMenu* menu, const QString& commandOrKey) {
        Q_UNUSED(menu);
        Q_UNUSED(commandOrKey);
        // Les menus restent sans icônes pour conserver la présentation précédente.
        // Les icônes sont appliquées uniquement aux actions/fonctions.
    };

    QAction* newAction     = mkAction("New drawing", "", QKeySequence::New);
    QAction* openAction    = mkAction("Open DWG/DXF via ODA Converter",     "", QKeySequence::Open);
    QAction* openProjectAction = mkAction("Open CAD project...", "", {});
    QAction* saveAction    = mkAction("Save project", "", QKeySequence::Save);
    QAction* saveAsAction  = mkAction("Save as...", "", QKeySequence::SaveAs);
    QAction* exportSvgAction = mkAction("Export SVG...", "", {});
    QAction* exportDxfAction = mkAction("Export editable DXF...", "", {});
    QAction* exportDwgOdaAction = mkAction("Save DWG via ODA Converter...", "", {});
    QAction* pageSetupAction = mkAction("Page setup...", "", {});
    QAction* exportPdfAction = mkAction("Export PDF...", "", QKeySequence(Qt::CTRL | Qt::Key_P));
    QAction* printAction = mkAction("Print...", "", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    QAction* setupAction   = mkAction("Drawing settings...", "", {});
    QAction* exitAction    = mkAction("Exit",     "", QKeySequence::Quit);
    QAction* bgAction      = mkAction("Background...",       "", {});
    QAction* selectAction  = mkAction("Select",   "", QKeySequence(Qt::Key_Escape));
    QAction* lineAction    = mkAction("Line",       ":/icons/draw/line/line_2p.svg", QKeySequence(Qt::Key_L));
    QAction* polylineAction = mkAction("Polyline",  "", QKeySequence(Qt::Key_P));
    QAction* circleAction  = mkAction("Circle center/radius", "", QKeySequence(Qt::Key_C));
    QAction* circleDiameterAction = mkAction("Circle by diameter", "", {});
    QAction* circle3PointsAction = mkAction("Circle 3 points", "", {});
    QAction* arc3PointsAction = mkAction("Arc 3 points", "", QKeySequence(Qt::Key_A));
    QAction* rectAction    = mkAction("Rectangle",   "", QKeySequence(Qt::Key_T));
    QAction* ellipseAction = mkAction("Ellipse",     "", QKeySequence(Qt::Key_E));
    QAction* polygonAction = mkAction("Polygon",    "", {});
    QAction* textAction = mkAction("Text", "", QKeySequence(Qt::Key_X));
    QAction* dimLinearAction = mkAction("Linear dimension", "", QKeySequence(Qt::Key_D));
    QAction* leaderAction = mkAction("Leader line", "", QKeySequence(Qt::Key_N));
    QAction* hatchAction = mkAction("Hatch / fill", "", QKeySequence(Qt::Key_H));
    QAction* zoomInAction  = mkAction("Zoom +",      "", QKeySequence(Qt::Key_Plus));
    QAction* zoomOutAction = mkAction("Zoom −",      "", QKeySequence(Qt::Key_Minus));
    QAction* zoomWindowAction = mkAction("Zoom window", "", QKeySequence(Qt::Key_Z));
    QAction* fitAction     = mkAction("Fit",     "", QKeySequence(Qt::Key_F));
    QAction* resetAction   = mkAction("Reset","",QKeySequence(Qt::Key_R));
    QAction* gridAction    = mkAction("Grid", "", QKeySequence(Qt::Key_F7));
    QAction* snapAction    = mkAction("Grid snap", "", QKeySequence(Qt::Key_F9));
    QAction* orthoAction   = mkAction("Ortho", "", QKeySequence(Qt::Key_F8));
    QAction* osnapAction   = mkAction("Object snap", "", QKeySequence(Qt::Key_F3));
    QAction* gridSpacingAction = mkAction("Grid spacing...", "", {});

    QAction* deleteAction = mkAction("Delete selection", "", QKeySequence::Delete);
    QAction* copyAction = mkAction("Copy with offset...", "", QKeySequence::Copy);
    QAction* moveAction = mkAction("Move...", "", QKeySequence(Qt::Key_M));
    QAction* rotateAction = mkAction("Rotate...", "", {});
    QAction* scaleAction = mkAction("Scale...", "", {});
    QAction* mirrorHAction = mkAction("Mirror horizontal", "", {});
    QAction* mirrorVAction = mkAction("Mirror vertical", "", {});

    QAction* layerNewAction = mkAction("New layer...", "", {});
    QAction* layerDeleteAction = mkAction("Delete layer", "", {});
    QAction* layerRenameAction = mkAction("Rename layer...", "", {});
    QAction* layerCurrentAction = mkAction("Set current layer", "", {});
    QAction* layerColorAction = mkAction("Layer color...", "", {});
    QAction* layerWeightAction = mkAction("Layer weight...", "", {});
    QAction* layerTypeAction = mkAction("Layer line type...", "", {});
    QAction* layerMoveSelectionAction = mkAction("Move selection to current layer", "", {});

    QAction* createBlockAction = mkAction("Create Block from Selection...", "", QKeySequence(Qt::CTRL | Qt::Key_B));
    QAction* addEmptyBlockAction = mkAction("Add Empty Block...", "", {});
    QAction* insertBlockAction = mkAction("Insert Block...", "", QKeySequence(Qt::Key_B));
    QAction* renameBlockAction = mkAction("Rename Block...", "", {});
    QAction* duplicateBlockAction = mkAction("Duplicate Block...", "", {});
    QAction* removeBlockAction = mkAction("Remove Block", "", {});
    QAction* purgeBlocksAction = mkAction("Purge Unused Blocks", "", {});
    QAction* explodeBlockAction = mkAction("Explode Selected Block(s)", "", {});
    QAction* showAllBlocksAction = mkAction("Show All Blocks", "", {});
    QAction* hideAllBlocksAction = mkAction("Hide All Blocks", "", {});
    QAction* selectBlockRefsAction = mkAction("Select Block References", "", {});
    QAction* deselectBlockRefsAction = mkAction("Deselect Block References", "", {});
    QAction* blockManagerAction = mkAction("Block Manager...", "", {});

    QAction* showLogPathAction = mkAction("Show log path", "", {});
    QAction* openLogFolderAction = mkAction("Open log folder", "", {});
    QAction* copyLogPathAction = mkAction("Copy log path", "", {});
    QAction* testLogAction = mkAction("Write test message", "", {});

    QAction* snapEndpointAction = mkAction("Endpoint", "", {});
    QAction* snapMidpointAction = mkAction("Midpoint", "", {});
    QAction* snapCenterAction = mkAction("Center", "", {});
    QAction* snapQuadrantAction = mkAction("Quadrant", "", {});
    QAction* snapIntersectionAction = mkAction("Intersection", "", {});
    QAction* snapTangentAction = mkAction("Tangent", "", {});
    QAction* snapVertexAction = mkAction("Vertex", "", {});

    for (QAction* a : {snapEndpointAction, snapMidpointAction, snapCenterAction, snapQuadrantAction, snapIntersectionAction, snapTangentAction, snapVertexAction}) {
        a->setCheckable(true);
        a->setChecked(true);
    }

    for (QAction* a : {gridAction, snapAction, orthoAction, osnapAction}) {
        a->setCheckable(true);
    }
    gridAction->setChecked(m_view->gridEnabled());
    snapAction->setChecked(m_view->snapEnabled());
    orthoAction->setChecked(m_view->orthoEnabled());
    osnapAction->setChecked(m_view->objectSnapEnabled());

    auto* drawGroup = new QActionGroup(this);
    drawGroup->setExclusive(true);
    for (QAction* a : {selectAction, lineAction, polylineAction, circleAction, circleDiameterAction, circle3PointsAction, arc3PointsAction, rectAction, ellipseAction, polygonAction, textAction, dimLinearAction, leaderAction, hatchAction}) {
        a->setCheckable(true);
        drawGroup->addAction(a);
    }
    selectAction->setChecked(true);


    // ── Actions AutoCAD 2008 classic placeholders ──────────
    // Les actions non encore implémentees restent visibles dans les menus/toolbars,
    // comme dans l'interface AutoCAD classique, mais affichent un message clair.
    auto cadPlaceholderAction = [&](const QString& label, const QString& command = QString()) -> QAction* {
        const QString cmd = command.isEmpty() ? label.toUpper().remove('&') : command;
        QAction* a = mkAction(label, cadCommandIcon(cmd), {});
        a->setData(cmd);
        connect(a, &QAction::triggered, this, [this, label, command]() {
            const QString cmd = command.isEmpty() ? label.toUpper().remove('&') : command;
            appendCommandConsoleMessage(tr("MENU_ACTION: %1 -> %2").arg(label, cmd));
            executeCadCommand(cmd);
        });
        return a;
    };

    QAction* closeAction = cadPlaceholderAction("&Close", "CLOSE");
    QAction* publishAction = cadPlaceholderAction("P&ublish...", "PUBLISH");
    QAction* drawingPropsAction = cadPlaceholderAction("Drawing &Properties...", "DWGPROPS");
    QAction* undoAction = cadPlaceholderAction("&Undo", "U");
    QAction* redoAction = cadPlaceholderAction("&Redo", "REDO");
    QAction* cutAction = cadPlaceholderAction("Cu&t", "CUTCLIP");
    QAction* copyClipAction = cadPlaceholderAction("&Copy", "COPYCLIP");
    QAction* copyBaseAction = cadPlaceholderAction("Copy with &Base Point", "COPYBASE");
    QAction* pasteAction = cadPlaceholderAction("&Paste", "PASTECLIP");
    QAction* pasteBlockAction = cadPlaceholderAction("Paste as &Block", "PASTEBLOCK");
    QAction* pasteOrigAction = cadPlaceholderAction("Paste to Original &Coordinates", "PASTEORIG");
    QAction* clearAction = cadPlaceholderAction("C&lear", "ERASE");
    QAction* selectAllAction = cadPlaceholderAction("Select &All", "SELECTALL");
    QAction* findAction = cadPlaceholderAction("&Find...", "FIND");
    QAction* redrawAction = cadPlaceholderAction("&Redraw", "REDRAW");
    QAction* regenAction = cadPlaceholderAction("Re&gen", "REGEN");
    QAction* panRealtimeAction = cadPlaceholderAction("&Realtime Pan", "PAN");
    QAction* namedViewsAction = cadPlaceholderAction("Named &Views...", "VIEW");
    QAction* viewportsAction = cadPlaceholderAction("&Viewports", "VPORTS");
    QAction* visualStylesAction = cadPlaceholderAction("&Visual Styles", "VISUALSTYLES");
    QAction* cleanScreenAction = cadPlaceholderAction("Clean &Screen", "CLEANSCREEN");
    cleanScreenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    QAction* qtCadSceneAction = cadPlaceholderAction("&Qt 2D Scene", "QTSCENE");
    qtCadSceneAction->setCheckable(true);
    qtCadSceneAction->setChecked(true);
    QAction* refreshCadSceneAction = cadPlaceholderAction("Refresh Qt 2D Scene", "QTREGEN");
    QAction* insertXrefAction = cadPlaceholderAction("DWG &Reference...", "XREF");
    QAction* attachDwfAction = cadPlaceholderAction("D&WF Underlay...", "DWFATTACH");
    QAction* attachDgnAction = cadPlaceholderAction("D&GN Underlay...", "DGNATTACH");
    QAction* attachImageAction = cadPlaceholderAction("Raster &Image Reference...", "IMAGEATTACH");
    attachImageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    QAction* layoutAction = cadPlaceholderAction("&Layout / Page Setup...", "LAYOUT");
    QAction* fieldAction = cadPlaceholderAction("&Field...", "FIELD");
    QAction* oleAction = cadPlaceholderAction("OLE &Object...", "INSERTOBJ");
    QAction* layerManagerAction = cadPlaceholderAction("&Layer...", "LAYER");
    QAction* colorFormatAction = cadPlaceholderAction("&Color...", "COLOR");
    QAction* linetypeFormatAction = cadPlaceholderAction("&Linetype...", "LINETYPE");
    QAction* lineweightFormatAction = cadPlaceholderAction("Line&weight...", "LINEWEIGHT");
    QAction* textStyleAction = cadPlaceholderAction("Text &Style...", "STYLE");
    QAction* hatchStyleAction = cadPlaceholderAction("&Hatch Style...", "HATCHSTYLE");
    QAction* dimStyleAction = cadPlaceholderAction("Dimension St&yle...", "DIMSTYLE");
    QAction* tableStyleAction = cadPlaceholderAction("Table St&yle...", "TABLESTYLE");
    QAction* pointStyleAction = cadPlaceholderAction("&Point Style...", "DDPTYPE");
    QAction* unitsAction = cadPlaceholderAction("&Units...", "UNITS");
    QAction* limitsAction = cadPlaceholderAction("Drawing &Limits", "LIMITS");
    QAction* renameAction = cadPlaceholderAction("&Rename...", "RENAME");
    QAction* workspaceAction = cadPlaceholderAction("&Workspaces", "WORKSPACE");
    QAction* palettesAction = cadPlaceholderAction("&Palettes", "PALETTES");
    QAction* commandLineAction = cadPlaceholderAction("Command &Line", "COMMANDLINE");
    commandLineAction->setShortcut(QKeySequence(Qt::Key_F2));
    QAction* inquiryDistanceAction = cadPlaceholderAction("&Distance", "DIST");
    QAction* inquiryAreaAction = cadPlaceholderAction("&Area", "AREA");
    QAction* inquiryListAction = cadPlaceholderAction("&List", "LIST");
    QAction* draftingSettingsAction = cadPlaceholderAction("&Drafting Settings...", "DSETTINGS");
    QAction* ucsAction = cadPlaceholderAction("&UCS", "UCS");
    QAction* customizeAction = cadPlaceholderAction("&Customize...", "CUI");
    QAction* optionsAction = cadPlaceholderAction("&Options...", "OPTIONS");
    QAction* loadAppAction = cadPlaceholderAction("Load &Application...", "APPLOAD");
    QAction* rayAction = cadPlaceholderAction("&Ray", "RAY");
    QAction* xlineAction = cadPlaceholderAction("Construction &Line", "XLINE");
    QAction* donutAction = cadPlaceholderAction("&Donut", "DONUT");
    QAction* splineAction = cadPlaceholderAction("&Spline", "SPLINE");
    QAction* gradientAction = cadPlaceholderAction("&Gradient...", "GRADIENT");
    QAction* boundaryAction = cadPlaceholderAction("&Boundary...", "BOUNDARY");
    QAction* regionAction = cadPlaceholderAction("&Region", "REGION");
    QAction* tableAction = cadPlaceholderAction("&Table...", "TABLE");
    QAction* mtextAction = cadPlaceholderAction("&Multiline Text", "MTEXT");
    QAction* pointAction = cadPlaceholderAction("&Point", "POINT");
    QAction* dimAlignedAction = cadPlaceholderAction("&Aligned", "DIMALIGNED");
    QAction* dimArcLengthAction = cadPlaceholderAction("Arc &Length", "DIMARC");
    QAction* dimOrdinateAction = cadPlaceholderAction("&Ordinate", "DIMORDINATE");
    QAction* dimRadiusAction = cadPlaceholderAction("&Radius", "DIMRADIUS");
    QAction* dimJoggedAction = cadPlaceholderAction("&Jogged", "DIMJOGGED");
    QAction* dimDiameterAction = cadPlaceholderAction("&Diameter", "DIMDIAMETER");
    QAction* dimAngularAction = cadPlaceholderAction("A&ngular", "DIMANGULAR");
    QAction* dimBaselineAction = cadPlaceholderAction("&Baseline", "DIMBASELINE");
    QAction* dimContinueAction = cadPlaceholderAction("&Continue", "DIMCONTINUE");
    QAction* toleranceAction = cadPlaceholderAction("&Tolerance...", "TOLERANCE");
    QAction* centerMarkAction = cadPlaceholderAction("C&enter Mark", "DIMCENTER");
    QAction* dimUpdateAction = cadPlaceholderAction("&Update", "DIMUPDATE");
    QAction* offsetAction = cadPlaceholderAction("&Offset", "OFFSET");
    QAction* arrayAction = cadPlaceholderAction("&Array...", "ARRAY");
    QAction* stretchAction = cadPlaceholderAction("&Stretch", "STRETCH");
    QAction* lengthenAction = cadPlaceholderAction("Len&gthen", "LENGTHEN");
    QAction* trimAction = cadPlaceholderAction("&Trim", "TRIM");
    QAction* extendAction = cadPlaceholderAction("&Extend", "EXTEND");
    QAction* breakAction = cadPlaceholderAction("&Break", "BREAK");
    QAction* joinAction = cadPlaceholderAction("&Join", "JOIN");
    QAction* chamferAction = cadPlaceholderAction("C&hamfer", "CHAMFER");
    QAction* filletAction = cadPlaceholderAction("&Fillet", "FILLET");
    QAction* explodeAction = cadPlaceholderAction("E&xplode", "EXPLODE");
    QAction* editPolylineAction = cadPlaceholderAction("&Polyline", "PEDIT");
    QAction* editHatchAction = cadPlaceholderAction("&Hatch", "HATCHEDIT");
    QAction* editTextAction = cadPlaceholderAction("Te&xt", "DDEDIT");
    QAction* propertiesAction = cadPlaceholderAction("&Properties", "PROPERTIES");
    QAction* matchPropsAction = cadPlaceholderAction("Ma&tch Properties", "MATCHPROP");
    QAction* newWindowAction = cadPlaceholderAction("&New Window", "NEWWINDOW");
    QAction* cascadeAction = cadPlaceholderAction("&Cascade", "CASCADE");
    QAction* tileHorizAction = cadPlaceholderAction("Tile &Horizontally", "TILEHORIZ");
    QAction* tileVertAction = cadPlaceholderAction("Tile &Vertically", "TILEVERT");
    QAction* helpAction = cadPlaceholderAction("&Help", "HELP");
    QAction* roadmapAction = mkAction("Command roadmap...", "", {});
    connect(roadmapAction, &QAction::triggered, this, [this]() {
        const QStringList candidates = {
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("ROADMAP_MENU_COMMANDES_AUTOCAD2008.md"),
            QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../ROADMAP_MENU_COMMANDES_AUTOCAD2008.md"),
            QDir::current().absoluteFilePath("ROADMAP_MENU_COMMANDES_AUTOCAD2008.md"),
            QDir::current().absoluteFilePath("../ROADMAP_MENU_COMMANDES_AUTOCAD2008.md")
        };
        for (const QString& path : candidates) {
            if (QFileInfo::exists(path)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                statusBar()->showMessage(tr("Command roadmap opened: %1").arg(path), 5000);
                return;
            }
        }
        QMessageBox::information(this, tr("Command roadmap"),
            tr("The command roadmap is documented in README.md at the project root.\n"
               "Copy README.md next to the executable if you want to open it directly from the menu."));
    });
    QAction* newFeaturesAction = cadPlaceholderAction("New Features &Workshop", "NEWFEATURES");
    QAction* aboutAction = cadPlaceholderAction("&About DWGViewer Advanced...", "ABOUT");

    // ── Icônes DWGView originales pour toutes les actions principales ─────
    // Les noms de fonctions/menus suivent les familles QCAD, mais les SVG sont créés dans DWGView.
    setCadIcon(newAction, "new");
    setCadIcon(openAction, "open");
    setCadIcon(openProjectAction, "open_project");
    setCadIcon(saveAction, "save");
    setCadIcon(saveAsAction, "save_as");
    setCadIcon(exportSvgAction, "export_svg");
    setCadIcon(exportDxfAction, "export_dxf");
    setCadIcon(exportDwgOdaAction, "export_dwg");
    setCadIcon(pageSetupAction, "page_setup");
    setCadIcon(exportPdfAction, "export_pdf");
    setCadIcon(printAction, "print");
    setCadIcon(setupAction, "settings");
    setCadIcon(exitAction, "exit");
    setCadIcon(bgAction, "background");
    setCadIcon(selectAction, "select");
    setCadIcon(lineAction, "line");
    setCadIcon(polylineAction, "polyline");
    setCadIcon(circleAction, "circle");
    setCadIcon(circleDiameterAction, "circle_diameter");
    setCadIcon(circle3PointsAction, "circle_3p");
    setCadIcon(arc3PointsAction, "arc_3p");
    setCadIcon(rectAction, "rectangle");
    setCadIcon(ellipseAction, "ellipse");
    setCadIcon(polygonAction, "polygon");
    setCadIcon(textAction, "text");
    setCadIcon(dimLinearAction, "dimension_linear");
    setCadIcon(leaderAction, "leader");
    setCadIcon(hatchAction, "hatch");
    setCadIcon(zoomInAction, "zoom_in");
    setCadIcon(zoomOutAction, "zoom_out");
    setCadIcon(zoomWindowAction, "zoom_window");
    setCadIcon(fitAction, "fit");
    setCadIcon(resetAction, "reset");
    setCadIcon(gridAction, "grid");
    setCadIcon(snapAction, "snap_grid");
    setCadIcon(orthoAction, "ortho");
    setCadIcon(osnapAction, "osnap");
    setCadIcon(gridSpacingAction, "grid_spacing");
    setCadIcon(deleteAction, "delete");
    setCadIcon(copyAction, "copy");
    setCadIcon(moveAction, "move");
    setCadIcon(rotateAction, "rotate");
    setCadIcon(scaleAction, "scale");
    setCadIcon(mirrorHAction, "mirror_h");
    setCadIcon(mirrorVAction, "mirror_v");
    setCadIcon(layerNewAction, "layer_new");
    setCadIcon(layerDeleteAction, "layer_delete");
    setCadIcon(layerRenameAction, "layer_rename");
    setCadIcon(layerCurrentAction, "layer_current");
    setCadIcon(layerColorAction, "layer_color");
    setCadIcon(layerWeightAction, "layer_weight");
    setCadIcon(layerTypeAction, "layer_type");
    setCadIcon(layerMoveSelectionAction, "layer_move_selection");
    setCadIcon(createBlockAction, "block_create");
    setCadIcon(addEmptyBlockAction, "block_create");
    setCadIcon(insertBlockAction, "block_insert");
    setCadIcon(renameBlockAction, "rename");
    setCadIcon(duplicateBlockAction, "copy");
    setCadIcon(removeBlockAction, "delete");
    setCadIcon(purgeBlocksAction, "cleanscreen");
    setCadIcon(explodeBlockAction, "explode");
    setCadIcon(showAllBlocksAction, "display");
    setCadIcon(hideAllBlocksAction, "display");
    setCadIcon(selectBlockRefsAction, "select");
    setCadIcon(deselectBlockRefsAction, "select");
    setCadIcon(blockManagerAction, "block_insert");
    setCadIcon(showLogPathAction, "log_path");
    setCadIcon(openLogFolderAction, "open_log_folder");
    setCadIcon(copyLogPathAction, "copy_log_path");
    setCadIcon(testLogAction, "test_log");
    setCadIcon(snapEndpointAction, "snap_endpoint");
    setCadIcon(snapMidpointAction, "snap_midpoint");
    setCadIcon(snapCenterAction, "snap_center");
    setCadIcon(snapQuadrantAction, "snap_quadrant");
    setCadIcon(snapIntersectionAction, "snap_intersection");
    setCadIcon(snapTangentAction, "snap_tangent");
    setCadIcon(snapVertexAction, "snap_vertex");
    setCadIcon(roadmapAction, "roadmap");

    // ── Menus : AutoCAD 2008 Classic ───────────────────────
    QMenuBar* mb = new QMenuBar(this);
    setMenuBar(mb);

    QMenu* fileMenu      = mb->addMenu("&File");
    QMenu* editMenu      = mb->addMenu("&Edit");
    QMenu* viewMenu      = mb->addMenu("&View");
    QMenu* insertMenu    = mb->addMenu("&Insert");
    QMenu* formatMenu    = mb->addMenu("F&ormat");
    QMenu* toolsMenu     = mb->addMenu("&Tools");
    QMenu* drawMenu      = mb->addMenu("&Draw");
    QMenu* dimensionMenu = mb->addMenu("&Dimension");
    QMenu* modifyMenu    = mb->addMenu("&Modify");
    QMenu* blockMenu     = mb->addMenu("&Block");
    QMenu* windowMenu    = mb->addMenu("&Window");
    QMenu* helpMenu      = mb->addMenu("&Help");

    setCadMenuIcon(fileMenu, "file");
    setCadMenuIcon(editMenu, "edit");
    setCadMenuIcon(viewMenu, "view");
    setCadMenuIcon(insertMenu, "insert");
    setCadMenuIcon(formatMenu, "format");
    setCadMenuIcon(toolsMenu, "tools");
    setCadMenuIcon(drawMenu, "draw");
    setCadMenuIcon(dimensionMenu, "dimension");
    setCadMenuIcon(modifyMenu, "modify");
    setCadMenuIcon(blockMenu, "block_insert");
    setCadMenuIcon(windowMenu, "window");
    setCadMenuIcon(helpMenu, "help");

    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addAction(closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(openProjectAction);
    fileMenu->addAction(exportSvgAction);
    fileMenu->addAction(exportDxfAction);
    fileMenu->addAction(exportDwgOdaAction);
    fileMenu->addSeparator();
    fileMenu->addAction(pageSetupAction);
    fileMenu->addAction(exportPdfAction);
    fileMenu->addAction(printAction);
    fileMenu->addAction(publishAction);
    fileMenu->addSeparator();
    fileMenu->addAction(drawingPropsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction(cutAction);
    editMenu->addAction(copyClipAction);
    editMenu->addAction(copyBaseAction);
    editMenu->addAction(pasteAction);
    editMenu->addAction(pasteBlockAction);
    editMenu->addAction(pasteOrigAction);
    editMenu->addSeparator();
    editMenu->addAction(clearAction);
    editMenu->addAction(selectAllAction);
    editMenu->addSeparator();
    editMenu->addAction(findAction);

    QMenu* zoomMenu = viewMenu->addMenu("&Zoom");
    setCadMenuIcon(zoomMenu, "zoom");
    zoomMenu->addAction(zoomInAction);
    zoomMenu->addAction(zoomOutAction);
    zoomMenu->addAction(zoomWindowAction);
    zoomMenu->addAction(fitAction);
    zoomMenu->addAction(resetAction);
    viewMenu->addAction(panRealtimeAction);
    viewMenu->addAction(redrawAction);
    viewMenu->addAction(regenAction);
    viewMenu->addSeparator();
    viewMenu->addAction(namedViewsAction);
    viewMenu->addAction(viewportsAction);
    viewMenu->addAction(visualStylesAction);
    viewMenu->addSeparator();
    QMenu* displayMenu = viewMenu->addMenu("&Display");
    setCadMenuIcon(displayMenu, "display");
    displayMenu->addAction(gridAction);
    displayMenu->addAction(snapAction);
    displayMenu->addAction(orthoAction);
    displayMenu->addAction(osnapAction);
    displayMenu->addAction(gridSpacingAction);
    viewMenu->addSeparator();
    viewMenu->addAction(qtCadSceneAction);
    viewMenu->addAction(refreshCadSceneAction);
    viewMenu->addAction(cleanScreenAction);

    insertMenu->addAction(insertBlockAction);
    insertMenu->addAction(createBlockAction);
    insertMenu->addSeparator();
    insertMenu->addAction(insertXrefAction);
    insertMenu->addAction(attachDwfAction);
    insertMenu->addAction(attachDgnAction);
    insertMenu->addAction(attachImageAction);
    insertMenu->addSeparator();
    insertMenu->addAction(layoutAction);
    insertMenu->addAction(fieldAction);
    insertMenu->addAction(oleAction);

    blockMenu->addAction(explodeBlockAction);
    blockMenu->addSeparator();
    blockMenu->addAction(showAllBlocksAction);
    blockMenu->addAction(hideAllBlocksAction);
    blockMenu->addSeparator();
    blockMenu->addAction(addEmptyBlockAction);
    blockMenu->addAction(createBlockAction);
    blockMenu->addAction(insertBlockAction);
    blockMenu->addAction(renameBlockAction);
    blockMenu->addAction(duplicateBlockAction);
    blockMenu->addAction(removeBlockAction);
    blockMenu->addAction(purgeBlocksAction);
    blockMenu->addSeparator();
    blockMenu->addAction(selectBlockRefsAction);
    blockMenu->addAction(deselectBlockRefsAction);
    blockMenu->addSeparator();
    blockMenu->addAction(blockManagerAction);

    QMenu* layerSubMenu = formatMenu->addMenu("&Layer");
    setCadMenuIcon(layerSubMenu, "layer");
    layerSubMenu->addAction(layerManagerAction);
    layerSubMenu->addAction(layerNewAction);
    layerSubMenu->addAction(layerDeleteAction);
    layerSubMenu->addAction(layerRenameAction);
    layerSubMenu->addSeparator();
    layerSubMenu->addAction(layerCurrentAction);
    layerSubMenu->addAction(layerColorAction);
    layerSubMenu->addAction(layerWeightAction);
    layerSubMenu->addAction(layerTypeAction);
    layerSubMenu->addSeparator();
    layerSubMenu->addAction(layerMoveSelectionAction);
    formatMenu->addAction(colorFormatAction);
    formatMenu->addAction(linetypeFormatAction);
    formatMenu->addAction(lineweightFormatAction);
    formatMenu->addSeparator();
    formatMenu->addAction(textStyleAction);
    formatMenu->addAction(hatchStyleAction);
    formatMenu->addAction(dimStyleAction);
    formatMenu->addAction(tableStyleAction);
    formatMenu->addAction(pointStyleAction);
    formatMenu->addSeparator();
    formatMenu->addAction(unitsAction);
    formatMenu->addAction(limitsAction);
    formatMenu->addAction(renameAction);

    toolsMenu->addAction(workspaceAction);
    toolsMenu->addAction(palettesAction);
    toolsMenu->addAction(commandLineAction);
    toolsMenu->addSeparator();
    QMenu* inquiryMenu = toolsMenu->addMenu("&Inquiry");
    setCadMenuIcon(inquiryMenu, "inquiry");
    inquiryMenu->addAction(inquiryDistanceAction);
    inquiryMenu->addAction(inquiryAreaAction);
    inquiryMenu->addAction(inquiryListAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(draftingSettingsAction);
    QMenu* osnapOptionsMenu = toolsMenu->addMenu("Object Snap &Settings");
    setCadMenuIcon(osnapOptionsMenu, "osnap_menu");
    osnapOptionsMenu->addAction(osnapAction);
    osnapOptionsMenu->addSeparator();
    osnapOptionsMenu->addAction(snapEndpointAction);
    osnapOptionsMenu->addAction(snapMidpointAction);
    osnapOptionsMenu->addAction(snapCenterAction);
    osnapOptionsMenu->addAction(snapQuadrantAction);
    osnapOptionsMenu->addAction(snapIntersectionAction);
    osnapOptionsMenu->addAction(snapTangentAction);
    osnapOptionsMenu->addAction(snapVertexAction);
    toolsMenu->addAction(ucsAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(customizeAction);
    toolsMenu->addAction(optionsAction);
    toolsMenu->addAction(loadAppAction);
    toolsMenu->addSeparator();
    QMenu* diagnosticsMenu = toolsMenu->addMenu("&Diagnostic");
    setCadMenuIcon(diagnosticsMenu, "diagnostics");
    diagnosticsMenu->addAction(showLogPathAction);
    diagnosticsMenu->addAction(openLogFolderAction);
    diagnosticsMenu->addAction(copyLogPathAction);
    diagnosticsMenu->addSeparator();
    diagnosticsMenu->addAction(testLogAction);

    // -------------------------------------------------------------------------
    // QCAD Draw clone for DWGView.
    // Origin reference: QCAD CE `scripts/Draw/*` menu/action families.
    // No QCAD ECMAScript implementation is copied here. Every QAction below is
    // a DWGView-native mapping to GraphicsView tools; if the exact QCAD tool
    // requires QCAD's RDocument/REntity engine, the action is mapped to the
    // closest functional native tool and the DRAW console prints the origin.
    // See QCAD_DRAW_ORIGIN_MAP.md at project root for the full mapping.
    // -------------------------------------------------------------------------
    auto addQcadDrawCommand = [&](QMenu* menu,
                                  const QString& label,
                                  const QString& qcadOrigin,
                                  const QString& command,
                                  const QString& behaviour = QString(),
                                  const QString& iconPath = QString()) -> QAction* {
        const QString resolvedIcon = iconPath.isEmpty() ? cadCommandIcon(command) : iconPath;
        QAction* action = mkAction(label, resolvedIcon, {});
        action->setData(qcadOrigin);
        action->setToolTip(tr("QCAD origin: %1\nDWGView command: %2%3")
                               .arg(qcadOrigin, command, behaviour.isEmpty() ? QString() : QStringLiteral("\n") + behaviour));
        connect(action, &QAction::triggered, this, [this, label, qcadOrigin, command, behaviour]() {
            appendCommandConsoleMessage(tr("QCAD_ORIGIN: %1").arg(qcadOrigin));
            appendCommandConsoleMessage(tr("DRAW_ACTION: %1 -> %2").arg(label, command));
            if (!behaviour.isEmpty()) appendCommandConsoleMessage(behaviour);
            if (command.startsWith(QStringLiteral("PROMPT_LINEANGLE"))) {
                bool ok = false;
                const double angle = QInputDialog::getDouble(this, tr("QCAD Draw - angle"),
                                                             tr("Angle in degrees:"), 0.0, -360.0, 360.0, 4, &ok);
                if (!ok) return;
                if (m_view) m_view->processDrawConsoleCommand(QStringLiteral("LINEANGLE %1").arg(angle, 0, 'f', 6));
            } else if (command.startsWith(QStringLiteral("PROMPT_POLYGON_SIDES"))) {
                bool ok = false;
                const int sides = QInputDialog::getInt(this, tr("QCAD Draw - polygon"),
                                                       tr("Number of sides:"), m_view ? m_view->polygonSides() : 6, 3, 256, 1, &ok);
                if (!ok) return;
                if (m_view) { m_view->setPolygonSides(sides); m_view->processDrawConsoleCommand(QStringLiteral("POLYGON")); }
            } else if (command.startsWith(QStringLiteral("PROMPT_TEXT"))) {
                bool ok = false;
                const QString text = QInputDialog::getText(this, tr("QCAD Draw - text"), tr("Text:"),
                                                           QLineEdit::Normal, m_view ? m_view->annotationText() : QStringLiteral("Text"), &ok);
                if (!ok) return;
                if (m_view) { m_view->setAnnotationText(text); m_view->processDrawConsoleCommand(QStringLiteral("TEXT")); }
            } else if (m_view && m_view->processDrawConsoleCommand(command)) {
                // handled by GraphicsView
            } else {
                executeCadCommand(command);
            }
            if (m_commandLineEdit && m_view) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
            if (m_view) m_view->setFocus(Qt::ShortcutFocusReason);
            statusBar()->showMessage(tr("Draw: %1").arg(QString(label).remove('&')), 3000);
        });
        menu->addAction(action);
        return action;
    };

    auto addQcadDrawMenu = [&](QMenu* parent, const QString& title, const QString& origin) -> QMenu* {
        QMenu* menu = parent->addMenu(title);
        const QString iconKey = origin.section(QLatin1Char('/'), -1, -1);
        setCadMenuIcon(menu, iconKey.isEmpty() ? title : iconKey);
        menu->setToolTip(tr("QCAD origin: %1").arg(origin));
        return menu;
    };

    // -------------------------------------------------------------------------
    // QCAD Dimension clone for DWGView.
    // Origin reference: QCAD CE `scripts/Draw/Dimension/*` action families.
    // No QCAD ECMAScript source is copied. Every QAction maps to a native
    // DWGView command where possible; unsupported QCAD dimension variants are
    // routed to a visible proxy and the command console logs the exact origin.
    // See QCAD_DIMENSION_ORIGIN_MAP.md.
    // -------------------------------------------------------------------------
    auto addQcadDimensionCommand = [&](QMenu* menu,
                                       const QString& label,
                                       const QString& qcadOrigin,
                                       const QString& command,
                                       const QString& behaviour = QString()) -> QAction* {
        QAction* action = mkAction(label, cadCommandIcon(command), {});
        action->setData(qcadOrigin);
        action->setToolTip(tr("QCAD origin: %1\nDWGView command: %2%3")
                               .arg(qcadOrigin, command, behaviour.isEmpty() ? QString() : QStringLiteral("\n") + behaviour));
        connect(action, &QAction::triggered, this, [this, label, qcadOrigin, command, behaviour]() {
            appendCommandConsoleMessage(tr("QCAD_ORIGIN: %1").arg(qcadOrigin));
            appendCommandConsoleMessage(tr("DIMENSION_ACTION: %1 -> %2").arg(label, command));
            if (!behaviour.isEmpty()) appendCommandConsoleMessage(behaviour);
            if (m_view && m_view->processDrawConsoleCommand(command)) {
                // handled by GraphicsView draw console
            } else {
                executeCadCommand(command);
            }
            if (m_commandLineEdit && m_view) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
            if (m_view) m_view->setFocus(Qt::ShortcutFocusReason);
            statusBar()->showMessage(tr("Dimension: %1").arg(QString(label).remove('&')), 3000);
        });
        menu->addAction(action);
        return action;
    };

    auto addQcadDimensionMenu = [&](QMenu* parent, const QString& title, const QString& origin) -> QMenu* {
        QMenu* menu = parent->addMenu(title);
        const QString iconKey = origin.section(QLatin1Char('/'), -1, -1);
        setCadMenuIcon(menu, iconKey.isEmpty() ? title : iconKey);
        menu->setToolTip(tr("QCAD origin: %1").arg(origin));
        return menu;
    };

    // -------------------------------------------------------------------------
    // QCAD Modify clone for DWGView.
    // Origin reference: QCAD CE `scripts/Modify/*` action families.
    // No QCAD ECMAScript code is copied. Each QAction stores/logs its origin;
    // exact operations use native DWGView functions, and missing advanced QCAD
    // variants are mapped to the nearest safe DWGView command with a console note.
    // See QCAD_MODIFY_ORIGIN_MAP.md.
    // -------------------------------------------------------------------------
    auto addQcadModifyCommand = [&](QMenu* menu,
                                    const QString& label,
                                    const QString& qcadOrigin,
                                    const QString& command,
                                    QAction* nativeAction = nullptr,
                                    const QString& behaviour = QString()) -> QAction* {
        QAction* action = mkAction(label, cadCommandIcon(command), {});
        action->setData(qcadOrigin);
        action->setToolTip(tr("QCAD origin: %1\nDWGView command: %2%3")
                               .arg(qcadOrigin, command, behaviour.isEmpty() ? QString() : QStringLiteral("\n") + behaviour));
        connect(action, &QAction::triggered, this, [this, label, qcadOrigin, command, nativeAction, behaviour]() {
            appendCommandConsoleMessage(tr("QCAD_ORIGIN: %1").arg(qcadOrigin));
            appendCommandConsoleMessage(tr("MODIFY_ACTION: %1 -> %2").arg(label, command));
            if (!behaviour.isEmpty()) appendCommandConsoleMessage(behaviour);
            const QString interactiveCommand = canonicalInteractiveModifyCommand(command);

            // DELETE/ERASE must be immediate when objects are already selected.
            // Previously the menu/toolbar action always started the interactive
            // selection mode, which cleared the current selection first; the user
            // saw nothing being deleted. Keep interactive selection only for the
            // case where no entity is selected yet.
            if (interactiveCommand == QStringLiteral("ERASE") && m_view && m_view->hasCadSelection()) {
                if (nativeAction) nativeAction->trigger();
                else executeCadCommand(interactiveCommand);
                if (m_commandLineEdit && m_view) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
                if (m_view) m_view->setFocus(Qt::ShortcutFocusReason);
                statusBar()->showMessage(tr("Selection deleted."), 3000);
                return;
            }

            if (m_view && isInteractiveModifySelectionCommand(interactiveCommand)) {
                appendCommandConsoleMessage(interactiveModifyStartMessage(interactiveCommand));
                m_view->startInteractiveModifyCommand(interactiveCommand);
                if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
                m_view->setFocus(Qt::ShortcutFocusReason);
                statusBar()->showMessage(interactiveModifyStartMessage(interactiveCommand), 5000);
                return;
            }
            if (nativeAction) {
                nativeAction->trigger();
            } else {
                executeCadCommand(command);
            }
            if (m_commandLineEdit && m_view) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
            if (m_view) m_view->setFocus(Qt::ShortcutFocusReason);
            statusBar()->showMessage(tr("Modify: %1").arg(QString(label).remove('&')), 3000);
        });
        menu->addAction(action);
        return action;
    };

    auto addQcadModifyMenu = [&](QMenu* parent, const QString& title, const QString& origin) -> QMenu* {
        QMenu* menu = parent->addMenu(title);
        const QString iconKey = origin.section(QLatin1Char('/'), -1, -1);
        setCadMenuIcon(menu, iconKey.isEmpty() ? title : iconKey);
        menu->setToolTip(tr("QCAD origin: %1").arg(origin));
        return menu;
    };

    auto qcadLineIcon = [](const QString& fileName) -> QString {
        // Icons are DWGView-original SVGs. Action origins mirror QCAD CE scripts/Draw/Line/*.
        return QStringLiteral(":/icons/draw/line/") + fileName;
    };

    QMenu* qcadLineMenu = addQcadDrawMenu(drawMenu, "&Line", "scripts/Draw/Line");
    addQcadDrawCommand(qcadLineMenu, "&2 Points", "scripts/Draw/Line/Line2P", "LINE2P", QString(), qcadLineIcon(QStringLiteral("line_2p.svg")));
    addQcadDrawCommand(qcadLineMenu, "&Horizontal", "scripts/Draw/Line/LineHorizontal", "LINEHORIZONTAL", tr("Angle locked at 0°. Mouse and keyboard remain usable."), qcadLineIcon(QStringLiteral("line_horizontal.svg")));
    addQcadDrawCommand(qcadLineMenu, "&Vertical", "scripts/Draw/Line/LineVertical", "LINEVERTICAL", tr("Angle locked at 90°. Mouse and keyboard remain usable."), qcadLineIcon(QStringLiteral("line_vertical.svg")));
    addQcadDrawCommand(qcadLineMenu, "By &Angle...", "scripts/Draw/Line/LineAngle", "PROMPT_LINEANGLE", tr("Prompts for an angle, then starts LINE with a constraint."), qcadLineIcon(QStringLiteral("line_angle.svg")));
    addQcadDrawCommand(qcadLineMenu, "Relative &Angle...", "scripts/Draw/Line/LineRelativeAngle", "PROMPT_LINEANGLE", tr("DWGView mapping: fixed angle from the first point."), qcadLineIcon(QStringLiteral("line_relative_angle.svg")));
    addQcadDrawCommand(qcadLineMenu, "&Freehand", "scripts/Draw/Line/LineFreehand", "LINEFREEHAND", tr("DWGView mapping: polyline/2-point line depending on input."), qcadLineIcon(QStringLiteral("line_freehand.svg")));
    addQcadDrawCommand(qcadLineMenu, "&Ray", "scripts/Draw/Line/Ray", "RAY", tr("Functional proxy: 2-point LINE."), qcadLineIcon(QStringLiteral("ray.svg")));
    addQcadDrawCommand(qcadLineMenu, "&Construction Line", "scripts/Draw/Line/XLine", "XLINE", tr("Functional proxy: 2-point LINE."), qcadLineIcon(QStringLiteral("xline.svg")));
    qcadLineMenu->addSeparator();
    addQcadDrawCommand(qcadLineMenu, "&Parallel", "scripts/Draw/Line/LineParallel", "LINEPARALLEL", tr("Proxy: LINE with object snaps."), qcadLineIcon(QStringLiteral("line_parallel.svg")));
    addQcadDrawCommand(qcadLineMenu, "Parallel through &Point", "scripts/Draw/Line/LineParallelThrough", "LINEPARALLELTHROUGH", tr("Proxy: LINE with object snaps."), qcadLineIcon(QStringLiteral("line_parallel_through.svg")));
    addQcadDrawCommand(qcadLineMenu, "&Orthogonal", "scripts/Draw/Line/LineOrthogonal", "LINEORTHOGONAL", tr("Proxy: LINE with ORTHO/F8 or locked angle."), qcadLineIcon(QStringLiteral("line_orthogonal.svg")));
    addQcadDrawCommand(qcadLineMenu, "&Bisector", "scripts/Draw/Line/LineBisector", "LINEBISECTOR", tr("Proxy: LINE 2 points."), qcadLineIcon(QStringLiteral("line_bisector.svg")));
    addQcadDrawCommand(qcadLineMenu, "Tangent from &Point", "scripts/Draw/Line/LineTangent1", "LINETANGENT1", tr("Proxy: LINE with tangent object snap."), qcadLineIcon(QStringLiteral("line_tangent1.svg")));
    addQcadDrawCommand(qcadLineMenu, "Tangent to &Two Circles", "scripts/Draw/Line/LineTangent2", "LINETANGENT2", tr("Proxy: LINE with tangent object snap."), qcadLineIcon(QStringLiteral("line_tangent2.svg")));

    drawMenu->addAction(polylineAction);

    QMenu* qcadArcMenu = addQcadDrawMenu(drawMenu, "&Arc", "scripts/Draw/Arc");
    addQcadDrawCommand(qcadArcMenu, "&3 Points", "scripts/Draw/Arc/Arc3P", "ARC3P");
    addQcadDrawCommand(qcadArcMenu, "&Center, Point, Angle", "scripts/Draw/Arc/ArcCPA", "ARCCPA", tr("Mapping: ARC 3 points."));
    addQcadDrawCommand(qcadArcMenu, "&2 Points and Angle", "scripts/Draw/Arc/Arc2PA", "ARC2PA", tr("Mapping: ARC 3 points."));
    addQcadDrawCommand(qcadArcMenu, "2 Points and &Height", "scripts/Draw/Arc/Arc2PH", "ARC2PH", tr("Mapping: ARC 3 points."));
    addQcadDrawCommand(qcadArcMenu, "2 Points and &Length", "scripts/Draw/Arc/Arc2PL", "ARC2PL", tr("Mapping: ARC 3 points."));
    addQcadDrawCommand(qcadArcMenu, "2 Points and &Radius", "scripts/Draw/Arc/Arc2PR", "ARC2PR", tr("Mapping: ARC 3 points."));
    addQcadDrawCommand(qcadArcMenu, "&Tangential", "scripts/Draw/Arc/ArcTangential", "ARCTANGENTIAL", tr("Mapping: 3-point ARC with tangent snaps."));
    addQcadDrawCommand(qcadArcMenu, "Concentric", "scripts/Draw/Arc/ArcConcentric", "ARCCONCENTRIC", tr("Mapping: ARC 3 points."));

    QMenu* qcadCircleMenu = addQcadDrawMenu(drawMenu, "&Circle", "scripts/Draw/Circle");
    addQcadDrawCommand(qcadCircleMenu, "Center, &Point", "scripts/Draw/Circle/CircleCP", "CIRCLECP");
    addQcadDrawCommand(qcadCircleMenu, "Center, &Radius", "scripts/Draw/Circle/CircleCR", "CIRCLECR");
    addQcadDrawCommand(qcadCircleMenu, "&2 Points", "scripts/Draw/Circle/Circle2P", "CIRCLE2P");
    addQcadDrawCommand(qcadCircleMenu, "&3 Points", "scripts/Draw/Circle/Circle3P", "CIRCLE3P");
    addQcadDrawCommand(qcadCircleMenu, "2 Points and &Radius", "scripts/Draw/Circle/Circle2PR", "CIRCLE2PR", tr("Mapping: center/radius CIRCLE."));
    addQcadDrawCommand(qcadCircleMenu, "2 &Tangents and Point", "scripts/Draw/Circle/Circle2TP", "CIRCLE2TP", tr("Mapping: CIRCLE with tangent object snap."));
    addQcadDrawCommand(qcadCircleMenu, "2 Tangents and &Radius", "scripts/Draw/Circle/Circle2TR", "CIRCLE2TR", tr("Mapping: CIRCLE with tangent object snap."));
    addQcadDrawCommand(qcadCircleMenu, "&3 Tangents", "scripts/Draw/Circle/Circle3T", "CIRCLE3T", tr("Mapping: CIRCLE with tangent object snap."));
    addQcadDrawCommand(qcadCircleMenu, "Concentric", "scripts/Draw/Circle/CircleConcentric", "CIRCLECONCENTRIC", tr("Mapping: center/radius CIRCLE."));

    QMenu* qcadEllipseMenu = addQcadDrawMenu(drawMenu, "&Ellipse", "scripts/Draw/Ellipse");
    addQcadDrawCommand(qcadEllipseMenu, "Ellipse by &Bounding Box", "scripts/Draw/Ellipse/EllipseDD", "ELLIPSE");
    addQcadDrawCommand(qcadEllipseMenu, "Center, Point, Point", "scripts/Draw/Ellipse/EllipseCPP", "ELLIPSECPP", tr("Mapping: ellipse by bounding box."));
    addQcadDrawCommand(qcadEllipseMenu, "Radii", "scripts/Draw/Ellipse/EllipseRR", "ELLIPSERR", tr("Mapping: ellipse by bounding box."));
    addQcadDrawCommand(qcadEllipseMenu, "Ellipse &Arc", "scripts/Draw/Ellipse/EllipseArcCPPA", "ELLIPSEARCCPPA", tr("Mapping: full ellipse."));

    QMenu* qcadShapeMenu = addQcadDrawMenu(drawMenu, "&Shape", "scripts/Draw/Shape");
    addQcadDrawCommand(qcadShapeMenu, "&Rectangle 2 Points", "scripts/Draw/Shape/ShapeRectanglePP", "SHAPERECTANGLEPP");
    addQcadDrawCommand(qcadShapeMenu, "Rectangle &Size", "scripts/Draw/Shape/ShapeRectangleSize", "SHAPERECTANGLESIZE", tr("Mapping: 2-point rectangle; width/height through @dx,dy."));
    addQcadDrawCommand(qcadShapeMenu, "&Polygon Center/Corner", "scripts/Draw/Shape/ShapePolygonCP", "PROMPT_POLYGON_SIDES");
    addQcadDrawCommand(qcadShapeMenu, "Polygon &2 Points", "scripts/Draw/Shape/ShapePolygonPP", "POLYGON");
    addQcadDrawCommand(qcadShapeMenu, "Polygon Area/Center/Point", "scripts/Draw/Shape/ShapePolygonAFCP", "PROMPT_POLYGON_SIDES", tr("Mapping: polygon by center/radius."));

    drawMenu->addAction(splineAction);
    drawMenu->addAction(hatchAction);
    drawMenu->addAction(createBlockAction);
    drawMenu->addSeparator();
    addQcadDrawCommand(drawMenu, "&Point", "scripts/Draw/Point/Point1P", "POINT");
    addQcadDrawCommand(drawMenu, "&Text", "scripts/Draw/Text/Text", "PROMPT_TEXT");
    addQcadDrawCommand(drawMenu, "&Multiline Text", "scripts/Draw/Text/TextDialog", "PROMPT_TEXT", tr("Mapping: simple CadText; line breaks are preserved when entered."));
    addQcadDrawCommand(drawMenu, "&Image", "scripts/Draw/Image", "IMAGE", tr("Use File/Insert image; the action is logged in the console."));
    drawMenu->addSeparator();
    addQcadDrawCommand(drawMenu, "&Linear Dimension", "scripts/Draw/Dimension/DimRotated", "DIMLINEAR");
    addQcadDrawCommand(drawMenu, "&Aligned Dimension", "scripts/Draw/Dimension/DimAligned", "DIMLINEAR", tr("Mapping: 3-point linear dimension."));
    addQcadDrawCommand(drawMenu, "&Leader", "scripts/Draw/Dimension/Leader", "LEADER");

    // Dimension menu cloned from QCAD Draw/Dimension families.
    // Every item stores/logs its QCAD origin path. Exact QCAD ECMAScript code is
    // not copied; unsupported exact variants are routed to visible native proxies.
    QMenu* qcadDimLinearMenu = addQcadDimensionMenu(dimensionMenu, "&Linear", "scripts/Draw/Dimension/DimLinear");
    addQcadDimensionCommand(qcadDimLinearMenu, "&Horizontal / Vertical", "scripts/Draw/Dimension/DimLinear", "DIMLINEAR", tr("Click near endpoints/vertices: small square = reference detected without snap, then place the dimension."));
    addQcadDimensionCommand(qcadDimLinearMenu, "&Rotated", "scripts/Draw/Dimension/DimRotated", "DIMROTATED", tr("Native proxy: CadLinearDimension; angle controlled by dimension placement."));
    addQcadDimensionCommand(qcadDimLinearMenu, "&Aligned", "scripts/Draw/Dimension/DimAligned", "DIMALIGNED", tr("DWGView native: aligned dimension; references detected without snap with a small square."));
    addQcadDimensionCommand(qcadDimLinearMenu, "&Baseline", "scripts/Draw/Dimension/DimBaseline", "DIMBASELINE", tr("Proxy: new linear dimension with the same manual origin."));
    addQcadDimensionCommand(qcadDimLinearMenu, "&Continue", "scripts/Draw/Dimension/DimContinue", "DIMCONTINUE", tr("Proxy: new consecutive linear dimension."));

    QMenu* qcadDimCircleMenu = addQcadDimensionMenu(dimensionMenu, "&Circle / Arc", "scripts/Draw/Dimension/CircleArc");
    addQcadDimensionCommand(qcadDimCircleMenu, "&Radius", "scripts/Draw/Dimension/DimRadial", "DIMRADIUS", tr("Click the circle: entity detection without snap, small square at center, then press Enter."));
    addQcadDimensionCommand(qcadDimCircleMenu, "&Diameter", "scripts/Draw/Dimension/DimDiametric", "DIMDIAMETER", tr("Click the circle: entity detection without snap, small square at center, then press Enter."));
    addQcadDimensionCommand(qcadDimCircleMenu, "&Arc Length", "scripts/Draw/Dimension/DimArcLength", "DIMARC", tr("Proxy: visible linear dimension between arc points."));
    addQcadDimensionCommand(qcadDimCircleMenu, "&Jogged Radius", "scripts/Draw/Dimension/DimRadialJogged", "DIMJOGGED", tr("Proxy: visible radius/linear dimension."));
    addQcadDimensionCommand(qcadDimCircleMenu, "C&enter Mark", "scripts/Draw/Dimension/DimCenter", "DIMCENTER", tr("Click the circle: entity detection without snap, small square at center, then press Enter."));

    QMenu* qcadDimAngularMenu = addQcadDimensionMenu(dimensionMenu, "&Angular", "scripts/Draw/Dimension/DimAngular");
    addQcadDimensionCommand(qcadDimAngularMenu, "&2 Lines", "scripts/Draw/Dimension/DimAngular2L", "DIMANGULAR", tr("Proxy: visible linear dimension until native CadAngularDimension is implemented."));
    addQcadDimensionCommand(qcadDimAngularMenu, "&3 Points", "scripts/Draw/Dimension/DimAngular3P", "DIMANGULAR3P", tr("Proxy: visible linear dimension until native CadAngularDimension is implemented."));
    addQcadDimensionCommand(qcadDimAngularMenu, "&Arc", "scripts/Draw/Dimension/DimAngularArc", "DIMANGULARARC", tr("Visible proxy: linear dimension between extreme points."));

    QMenu* qcadDimOrdinateMenu = addQcadDimensionMenu(dimensionMenu, "&Ordinate", "scripts/Draw/Dimension/DimOrdinate");
    addQcadDimensionCommand(qcadDimOrdinateMenu, "&X Datum", "scripts/Draw/Dimension/DimOrdinateX", "DIMORDINATEX", tr("Proxy: leader/text for X coordinate."));
    addQcadDimensionCommand(qcadDimOrdinateMenu, "&Y Datum", "scripts/Draw/Dimension/DimOrdinateY", "DIMORDINATEY", tr("Proxy: leader/text for Y coordinate."));
    addQcadDimensionCommand(qcadDimOrdinateMenu, "&Free", "scripts/Draw/Dimension/DimOrdinate", "DIMORDINATE", tr("Proxy: leader/text for coordinate."));

    dimensionMenu->addSeparator();
    addQcadDimensionCommand(dimensionMenu, "&Leader", "scripts/Draw/Dimension/Leader", "LEADER", tr("DWGView native: arrow then text."));
    addQcadDimensionCommand(dimensionMenu, "&Tolerance / Feature Control", "scripts/Draw/Dimension/Tolerance", "TOLERANCE", tr("Proxy: text/tolerance placed as a visible annotation."));
    addQcadDimensionCommand(dimensionMenu, "&Dimension Style...", "scripts/Modify/Dimension/DimStyle", "DIMSTYLE", tr("Opens text/dimension height settings."));
    addQcadDimensionCommand(dimensionMenu, "&Update", "scripts/Modify/Dimension/DimUpdate", "DIMUPDATE", tr("Refreshes the Qt scene and properties."));

    // Modify menu cloned from QCAD Modify families.
    // Every item stores/logs its QCAD origin path. Exact QCAD ECMAScript code is
    // not copied; unsupported exact variants are routed to native DWGView proxies.
    addQcadModifyCommand(modifyMenu, "&Delete", "scripts/Edit/Delete", "ERASE", deleteAction,
                         tr("DWGView native: deletes the selection."));
    addQcadModifyCommand(modifyMenu, "&Duplicate", "scripts/Edit/Duplicate", "COPY", copyAction,
                         tr("DWGView native: copies with the entered offset."));
    addQcadModifyCommand(modifyMenu, "Copy with &Reference", "scripts/Edit/CopyWithReference", "COPYBASE", nullptr,
                         tr("Proxy: COPY with offset; the exact QCAD reference point is not interactive yet."));
    modifyMenu->addSeparator();

    addQcadModifyCommand(modifyMenu, "&Move / Translate", "scripts/Modify/Translate", "MOVE", moveAction,
                         tr("DWGView native: moves the selection by X/Y."));
    addQcadModifyCommand(modifyMenu, "&Rotate", "scripts/Modify/Rotate", "ROTATE", rotateAction,
                         tr("DWGView native: rotates around the selection center."));
    addQcadModifyCommand(modifyMenu, "Rotate &Two", "scripts/Modify/Rotate2", "ROTATE2", nullptr,
                         tr("Proxy: simple rotation; QCAD Rotate2 duplication is not separated yet."));
    addQcadModifyCommand(modifyMenu, "&Scale", "scripts/Modify/Scale", "SCALE", scaleAction,
                         tr("DWGView native: scales around the selection center."));
    addQcadModifyCommand(modifyMenu, "&Mirror", "scripts/Modify/Mirror", "MIRROR", nullptr,
                         tr("Proxy: demande axe horizontal/vertical puis applique miroir DWGView."));
    QMenu* qcadModifyFlipMenu = addQcadModifyMenu(modifyMenu, "&Flip", "scripts/Modify/FlipHorizontal + FlipVertical");
    addQcadModifyCommand(qcadModifyFlipMenu, "Flip &Horizontal", "scripts/Modify/FlipHorizontal", "FLIPHORIZONTAL", mirrorHAction,
                         tr("DWGView native: horizontal mirror around the selection center."));
    addQcadModifyCommand(qcadModifyFlipMenu, "Flip &Vertical", "scripts/Modify/FlipVertical", "FLIPVERTICAL", mirrorVAction,
                         tr("DWGView native: vertical mirror around the selection center."));
    addQcadModifyCommand(modifyMenu, "Translate / &Rotate", "scripts/Modify/TranslateRotate", "TRANSLATEROTATE", nullptr,
                         tr("Proxy: MOVE then ROTATE are available separately."));
    modifyMenu->addSeparator();

    addQcadModifyCommand(modifyMenu, "&Offset", "scripts/Modify/Offset", "OFFSET", offsetAction,
                         tr("DWGView native: duplicates the selection with an offset distance."));
    addQcadModifyCommand(modifyMenu, "Offset &Through", "scripts/Modify/OffsetThrough", "OFFSETTHROUGH", nullptr,
                         tr("Proxy: OFFSET distance; le point-through QCAD exact n'est pas encore natif."));
    addQcadModifyCommand(modifyMenu, "&Stretch", "scripts/Modify/Stretch", "STRETCH", nullptr,
                         tr("Proxy: MOVE on the full selection; QCAD partial window to be added later."));
    addQcadModifyCommand(modifyMenu, "Len&gthen", "scripts/Modify/Lengthen", "LENGTHEN", nullptr,
                         tr("Proxy: SCALE on selection; exact endpoint length editing to be added."));
    addQcadModifyCommand(modifyMenu, "&Array", "scripts/Modify/Array", "ARRAY", nullptr,
                         tr("Proxy: COPY with offset; exact multiple array to be added."));
    modifyMenu->addSeparator();

    addQcadModifyCommand(modifyMenu, "&Trim", "scripts/Modify/Trim", "TRIM", trimAction,
                         tr("DWGView native: trims two lines to their intersection."));
    addQcadModifyCommand(modifyMenu, "Trim &Both", "scripts/Modify/TrimBoth", "TRIMBOTH", nullptr,
                         tr("Proxy: TRIM on two selected lines."));
    addQcadModifyCommand(modifyMenu, "&Auto Trim", "scripts/Modify/AutoTrim", "AUTOTRIM", nullptr,
                         tr("Proxy: TRIM; full interactive QCAD AutoTrim is not copied."));
    addQcadModifyCommand(modifyMenu, "&Extend", "scripts/Modify/Extend", "EXTEND", extendAction,
                         tr("DWGView native: extends two lines to their intersection."));
    addQcadModifyCommand(modifyMenu, "&Break Out Segment", "scripts/Modify/BreakOut", "BREAKOUT", nullptr,
                         tr("Proxy: BREAKOUT logged; segment deletion to be added in CadDocument."));
    addQcadModifyCommand(modifyMenu, "Break Out &Gap", "scripts/Modify/BreakOutGap", "BREAKOUTGAP", nullptr,
                         tr("Proxy: BREAKOUT logged; exact QCAD gap is not native yet."));
    addQcadModifyCommand(modifyMenu, "Break Out &Manual", "scripts/Modify/BreakOutManual", "BREAKOUTMANUAL", nullptr,
                         tr("Proxy: BREAKOUT logged; interactive break points to be added."));
    addQcadModifyCommand(modifyMenu, "&Divide", "scripts/Modify/Divide", "DIVIDE", nullptr,
                         tr("Proxy: adds markers using text/points in a future version."));
    addQcadModifyCommand(modifyMenu, "&Join", "scripts/Modify/Join", "JOIN", nullptr,
                         tr("Proxy: PEDIT smooth/close; strict topological merging to be added."));
    addQcadModifyCommand(modifyMenu, "&Reverse", "scripts/Modify/Reverse", "REVERSE", nullptr,
                         tr("Proxy: logged; polyline order reversal to be added."));
    addQcadModifyCommand(modifyMenu, "E&xplode", "scripts/Modify/Explode", "EXPLODE", nullptr,
                         tr("Proxy: logged; complex block/polyline explode to be added in CadDocument."));
    modifyMenu->addSeparator();

    addQcadModifyCommand(modifyMenu, "&Bevel / Chamfer", "scripts/Modify/Bevel", "CHAMFER", chamferAction,
                         tr("DWGView native: chamfer between two lines."));
    addQcadModifyCommand(modifyMenu, "&Round / Fillet", "scripts/Modify/Round", "FILLET", filletAction,
                         tr("DWGView native: fillet between two lines."));
    modifyMenu->addSeparator();

    QMenu* qcadDrawOrderMenu = addQcadModifyMenu(modifyMenu, "Draw &Order", "scripts/Modify/DrawOrder");
    setCadMenuIcon(qcadDrawOrderMenu, "draw_order");
    addQcadModifyCommand(qcadDrawOrderMenu, "Bring to &Front", "scripts/Modify/DrawOrder/ToFront", "DRAWORDERFRONT", nullptr,
                         tr("Proxy: draw order based on the current scene; logged for traceability."));
    addQcadModifyCommand(qcadDrawOrderMenu, "Send to &Back", "scripts/Modify/DrawOrder/ToBack", "DRAWORDERBACK", nullptr,
                         tr("Proxy: draw order based on the current scene; logged for traceability."));

    QMenu* qcadObjectMenu = addQcadModifyMenu(modifyMenu, "&Object", "scripts/Modify/EditText + EditHatch + PEDIT");
    setCadMenuIcon(qcadObjectMenu, "object");
    addQcadModifyCommand(qcadObjectMenu, "Edit &Polyline", "scripts/Modify/Pedit", "PEDIT", editPolylineAction,
                         tr("DWGView native: close/open/smooth on selected polylines."));
    addQcadModifyCommand(qcadObjectMenu, "Edit &Hatch", "scripts/Modify/EditHatch", "HATCHEDIT", editHatchAction,
                         tr("DWGView native: hatch pattern, scale, and angle."));
    addQcadModifyCommand(qcadObjectMenu, "Edit Te&xt", "scripts/Modify/EditText", "DDEDIT", editTextAction,
                         tr("DWGView native: text content, height, and rotation."));
    modifyMenu->addSeparator();

    addQcadModifyCommand(modifyMenu, "&Properties", "scripts/Modify/Properties", "PROPERTIES", propertiesAction,
                         tr("DWGView native: properties palette."));
    addQcadModifyCommand(modifyMenu, "Ma&tch Properties", "scripts/Modify/MatchProperty", "MATCHPROP", matchPropsAction,
                         tr("DWGView native: copies properties from the first object to the others."));

    windowMenu->addAction(newWindowAction);
    windowMenu->addSeparator();
    windowMenu->addAction(cascadeAction);
    windowMenu->addAction(tileHorizAction);
    windowMenu->addAction(tileVertAction);

    helpMenu->addAction(helpAction);
    helpMenu->addAction(roadmapAction);
    helpMenu->addAction(newFeaturesAction);
    helpMenu->addSeparator();
    helpMenu->addAction(aboutAction);

    // ── Barres d'outils : AutoCAD 2008 Classic ────────────
    QToolBar* standardTb = new QToolBar("Standard", this);
    standardTb->setObjectName("AutoCAD2008_StandardToolbar");
    addToolBar(Qt::TopToolBarArea, standardTb);
    standardTb->addAction(newAction);
    standardTb->addAction(openAction);
    standardTb->addAction(saveAction);
    standardTb->addSeparator();
    standardTb->addAction(exportPdfAction);
    standardTb->addAction(printAction);
    standardTb->addSeparator();
    standardTb->addAction(zoomInAction);
    standardTb->addAction(zoomOutAction);
    standardTb->addAction(zoomWindowAction);
    standardTb->addAction(fitAction);
    standardTb->addAction(panRealtimeAction);

    QToolBar* layersTb = new QToolBar("Layers", this);
    layersTb->setObjectName("AutoCAD2008_LayersToolbar");
    addToolBar(Qt::TopToolBarArea, layersTb);
    layersTb->addAction(layerNewAction);
    layersTb->addAction(layerCurrentAction);
    layersTb->addAction(layerColorAction);
    layersTb->addAction(layerMoveSelectionAction);
    layersTb->addSeparator();
    layersTb->addWidget(new QLabel(tr("Current layer:"), layersTb));
    m_currentLayerCombo = new QComboBox(layersTb);
    m_currentLayerCombo->setMinimumWidth(180);
    m_currentLayerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_currentLayerCombo->setToolTip(tr("Layer used by newly drawn objects. Selecting it here also makes the layer visible and unlocked."));
    layersTb->addWidget(m_currentLayerCombo);

    QToolBar* drawTb = new QToolBar("Draw", this);
    drawTb->setObjectName("AutoCAD2008_DrawToolbar");
    addToolBar(Qt::LeftToolBarArea, drawTb);
    drawTb->addAction(selectAction);
    drawTb->addAction(lineAction);
    drawTb->addAction(polylineAction);
    drawTb->addAction(rectAction);
    drawTb->addAction(circleAction);
    drawTb->addAction(arc3PointsAction);
    drawTb->addAction(ellipseAction);
    drawTb->addAction(polygonAction);
    drawTb->addAction(hatchAction);
    drawTb->addAction(textAction);
    drawTb->addAction(dimLinearAction);
    drawTb->addAction(leaderAction);
    drawTb->addAction(insertBlockAction);

    QToolBar* modifyTb = new QToolBar("Modify", this);
    modifyTb->setObjectName("AutoCAD2008_ModifyToolbar");
    addToolBar(Qt::LeftToolBarArea, modifyTb);
    modifyTb->addAction(deleteAction);
    modifyTb->addAction(copyAction);
    modifyTb->addAction(moveAction);
    modifyTb->addAction(rotateAction);
    modifyTb->addAction(scaleAction);
    modifyTb->addAction(mirrorHAction);
    modifyTb->addAction(mirrorVAction);
    modifyTb->addAction(offsetAction);
    modifyTb->addAction(trimAction);
    modifyTb->addAction(extendAction);
    modifyTb->addAction(filletAction);
    modifyTb->addAction(chamferAction);

    QToolBar* osnapTb = new QToolBar("Object Snap", this);
    osnapTb->setObjectName("AutoCAD2008_ObjectSnapToolbar");
    addToolBar(Qt::BottomToolBarArea, osnapTb);
    osnapTb->addAction(gridAction);
    osnapTb->addAction(snapAction);
    osnapTb->addAction(orthoAction);
    osnapTb->addAction(osnapAction);
    osnapTb->addSeparator();
    osnapTb->addAction(snapEndpointAction);
    osnapTb->addAction(snapMidpointAction);
    osnapTb->addAction(snapCenterAction);
    osnapTb->addAction(snapIntersectionAction);

    // ── Panneau calques ───────────────────────────────────
    m_layersDock = new QDockWidget(tr("Layers"), this);
    m_layersDock->setObjectName(QStringLiteral("LayersDock"));
    m_layersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_layersDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_layersDock->setMinimumWidth(150);
    m_layersDock->setMaximumWidth(220);

    auto* layersWidget = new QWidget(m_layersDock);
    layersWidget->setMinimumWidth(150);
    layersWidget->setMaximumWidth(220);
    auto* layersLayout = new QVBoxLayout(layersWidget);
    layersLayout->setContentsMargins(3, 3, 3, 3);
    layersLayout->setSpacing(3);

    m_layersTree = new QTreeWidget();
    m_layersTree->setColumnCount(7);
    m_layersTree->setHeaderLabels({"Cur", "On", "Lock", "Clr", "Layer", "Type", "Wt"});
    m_layersTree->setRootIsDecorated(false);
    m_layersTree->setAlternatingRowColors(true);
    m_layersTree->setUniformRowHeights(true);
    m_layersTree->setIndentation(8);
    m_layersTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_layersTree->setMinimumWidth(150);
    m_layersTree->setMaximumWidth(220);
    m_layersTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_layersTree->setColumnWidth(0, 38);
    m_layersTree->setColumnWidth(1, 34);
    m_layersTree->setColumnWidth(2, 40);
    m_layersTree->setColumnWidth(3, 40);
    m_layersTree->setColumnWidth(4, 82);
    m_layersTree->setColumnWidth(5, 58);
    m_layersTree->setColumnWidth(6, 34);

    auto* layerButtons = new QHBoxLayout();
    auto* addLayerButton = new QPushButton("+", layersWidget);
    auto* deleteLayerButton = new QPushButton("-", layersWidget);
    auto* currentLayerButton = new QPushButton(tr("Current"), layersWidget);
    auto* colorLayerButton = new QPushButton(tr("Color"), layersWidget);
    addLayerButton->setFixedWidth(28);
    deleteLayerButton->setFixedWidth(28);
    currentLayerButton->setMaximumWidth(72);
    colorLayerButton->setMaximumWidth(62);
    layerButtons->addWidget(addLayerButton);
    layerButtons->addWidget(deleteLayerButton);
    layerButtons->addWidget(currentLayerButton);
    layerButtons->addWidget(colorLayerButton);

    layersLayout->addWidget(m_layersTree);
    layersLayout->addLayout(layerButtons);
    m_layersDock->setWidget(layersWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_layersDock);

    buildPropertiesPanel();
    if (m_layersDock && m_propertiesDock) {
        // Stack Layers and Properties as tabs in a single right-side dock area.
        // Only one palette consumes width at a time, leaving more room for the drawing scene.
        tabifyDockWidget(m_layersDock, m_propertiesDock);
        m_layersDock->raise();
    }

    // ── Connexions ────────────────────────────────────────
    connect(newAction,     &QAction::triggered, this, &MainWindow::newDrawing);
    connect(openAction,    &QAction::triggered, this, &MainWindow::openFile);
    connect(openProjectAction, &QAction::triggered, this, &MainWindow::openProject);
    connect(saveAction,    &QAction::triggered, this, &MainWindow::saveProject);
    connect(saveAsAction,  &QAction::triggered, this, &MainWindow::saveProjectAs);
    connect(exportSvgAction, &QAction::triggered, this, &MainWindow::exportSvg);
    connect(exportDxfAction, &QAction::triggered, this, &MainWindow::exportDxf);
    connect(exportDwgOdaAction, &QAction::triggered, this, &MainWindow::exportDwgViaOda);
    connect(pageSetupAction, &QAction::triggered, this, &MainWindow::configurePageLayout);
    connect(exportPdfAction, &QAction::triggered, this, &MainWindow::exportPdf);
    connect(printAction, &QAction::triggered, this, &MainWindow::printDrawing);
    connect(setupAction,   &QAction::triggered, this, &MainWindow::configureDrawing);
    connect(exitAction,    &QAction::triggered, this, &MainWindow::close);
    connect(bgAction,      &QAction::triggered, this, &MainWindow::changeBackground);

    connect(layerNewAction, &QAction::triggered, this, &MainWindow::createLayer);
    connect(layerDeleteAction, &QAction::triggered, this, &MainWindow::deleteLayer);
    connect(layerRenameAction, &QAction::triggered, this, &MainWindow::renameLayer);
    connect(layerCurrentAction, &QAction::triggered, this, &MainWindow::setSelectedLayerCurrent);
    connect(layerColorAction, &QAction::triggered, this, &MainWindow::changeSelectedLayerColor);
    connect(layerWeightAction, &QAction::triggered, this, &MainWindow::changeSelectedLayerLineWeight);
    connect(layerTypeAction, &QAction::triggered, this, &MainWindow::changeSelectedLayerLineType);
    connect(layerMoveSelectionAction, &QAction::triggered, this, &MainWindow::moveSelectionToCurrentLayer);
    connect(createBlockAction, &QAction::triggered, this, &MainWindow::createBlockFromSelection);
    connect(addEmptyBlockAction, &QAction::triggered, this, &MainWindow::createEmptyBlock);
    connect(insertBlockAction, &QAction::triggered, this, &MainWindow::insertBlock);
    connect(renameBlockAction, &QAction::triggered, this, &MainWindow::renameBlock);
    connect(duplicateBlockAction, &QAction::triggered, this, &MainWindow::duplicateBlock);
    connect(removeBlockAction, &QAction::triggered, this, &MainWindow::removeBlock);
    connect(purgeBlocksAction, &QAction::triggered, this, &MainWindow::purgeUnusedBlocks);
    connect(explodeBlockAction, &QAction::triggered, this, &MainWindow::explodeSelectedBlocks);
    connect(showAllBlocksAction, &QAction::triggered, this, &MainWindow::showAllBlocks);
    connect(hideAllBlocksAction, &QAction::triggered, this, &MainWindow::hideAllBlocks);
    connect(selectBlockRefsAction, &QAction::triggered, this, &MainWindow::selectBlockReferences);
    connect(deselectBlockRefsAction, &QAction::triggered, this, &MainWindow::deselectBlockReferences);
    connect(blockManagerAction, &QAction::triggered, this, &MainWindow::showBlockManager);

    connect(showLogPathAction, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, tr("Diagnostic"),
                                 tr("Current log file:\n%1").arg(DebugLogger::logFilePath()));
    });
    connect(openLogFolderAction, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(DebugLogger::logDirectoryPath()));
    });
    connect(copyLogPathAction, &QAction::triggered, this, [this]() {
        QApplication::clipboard()->setText(DebugLogger::logFilePath());
        statusBar()->showMessage(tr("Log path copied to the clipboard"), 3000);
        DebugLogger::logInfo("Log path copied by the user");
    });
    connect(testLogAction, &QAction::triggered, this, [this]() {
        DebugLogger::logInfo("Test message from the Diagnostic menu");
        qInfo() << "Qt qInfo test message from Diagnostic";
        statusBar()->showMessage(tr("Test message written to the log"), 3000);
    });
    connect(addLayerButton, &QPushButton::clicked, this, &MainWindow::createLayer);
    connect(deleteLayerButton, &QPushButton::clicked, this, &MainWindow::deleteLayer);
    connect(currentLayerButton, &QPushButton::clicked, this, &MainWindow::setSelectedLayerCurrent);
    connect(colorLayerButton, &QPushButton::clicked, this, &MainWindow::changeSelectedLayerColor);
    if (m_currentLayerCombo) {
        connect(m_currentLayerCombo, &QComboBox::currentTextChanged,
                this, &MainWindow::onCurrentLayerComboChanged);
    }

    connect(m_view->scene(), &QGraphicsScene::selectionChanged, this, &MainWindow::updatePropertiesPanel);
    connect(m_objectColorButton, &QPushButton::clicked, this, &MainWindow::changeSelectedObjectColor);

    auto requireSelection = [=]() -> bool {
        if (m_view->hasCadSelection()) return true;
        QMessageBox::information(this, tr("Select"), tr("First select one or more internal CAD objects."));
        return false;
    };

    auto makeRealAction = [](QAction* action) {
        if (action) action->disconnect();
    };

    makeRealAction(undoAction);
    makeRealAction(redoAction);
    makeRealAction(clearAction);
    makeRealAction(selectAllAction);
    makeRealAction(redrawAction);
    makeRealAction(regenAction);
    makeRealAction(panRealtimeAction);
    makeRealAction(commandLineAction);
    makeRealAction(drawingPropsAction);
    makeRealAction(propertiesAction);
    makeRealAction(offsetAction);
    makeRealAction(trimAction);
    makeRealAction(extendAction);
    makeRealAction(chamferAction);
    makeRealAction(filletAction);
    makeRealAction(unitsAction);
    makeRealAction(limitsAction);
    makeRealAction(splineAction);
    makeRealAction(mtextAction);
    makeRealAction(textStyleAction);
    makeRealAction(hatchStyleAction);
    makeRealAction(dimStyleAction);
    makeRealAction(editPolylineAction);
    makeRealAction(editHatchAction);
    makeRealAction(editTextAction);
    makeRealAction(matchPropsAction);
    makeRealAction(centerMarkAction);
    makeRealAction(dimDiameterAction);
    makeRealAction(dimRadiusAction);
    makeRealAction(dimAlignedAction);
    makeRealAction(attachImageAction);
    makeRealAction(layoutAction);
    makeRealAction(cleanScreenAction);
    makeRealAction(qtCadSceneAction);
    makeRealAction(refreshCadSceneAction);
    makeRealAction(workspaceAction);
    makeRealAction(palettesAction);
    makeRealAction(viewportsAction);
    makeRealAction(namedViewsAction);
    makeRealAction(insertXrefAction);
    makeRealAction(attachDwfAction);
    makeRealAction(attachDgnAction);
    makeRealAction(loadAppAction);
    makeRealAction(newWindowAction);
    makeRealAction(cascadeAction);
    makeRealAction(tileHorizAction);
    makeRealAction(tileVertAction);
    makeRealAction(dimAngularAction);

    connect(undoAction, &QAction::triggered, this, &MainWindow::undoCommand);
    connect(redoAction, &QAction::triggered, this, &MainWindow::redoCommand);
    connect(clearAction, &QAction::triggered, deleteAction, &QAction::trigger);
    connect(selectAllAction, &QAction::triggered, this, [this]() {
        for (QGraphicsItem* item : m_view->scene()->items()) {
            bool ok = false;
            item->data(1).toInt(&ok);
            if (ok && item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) item->setSelected(true);
        }
        statusBar()->showMessage(tr("All selectable objects are selected."), 3000);
    });
    connect(redrawAction, &QAction::triggered, this, [this]() {
        m_view->viewport()->update();
        statusBar()->showMessage(tr("Redraw completed."), 2000);
    });
    connect(regenAction, &QAction::triggered, this, [this]() {
        m_view->refreshFromDocumentPreservingSelection(m_view->selectedEntityIndices());
        statusBar()->showMessage(tr("Regen completed."), 2000);
    });
    connect(panRealtimeAction, &QAction::triggered, this, [this, selectAction]() {
        selectAction->trigger();
        statusBar()->showMessage(tr("Realtime pan: middle button or Space + left button."), 5000);
    });
    connect(commandLineAction, &QAction::triggered, this, &MainWindow::showCommandLine);
    connect(drawingPropsAction, &QAction::triggered, this, &MainWindow::configureDrawing);
    connect(unitsAction, &QAction::triggered, this, &MainWindow::configureDrawing);
    connect(limitsAction, &QAction::triggered, this, &MainWindow::configureDrawing);
    connect(propertiesAction, &QAction::triggered, this, [this]() {
        if (m_propertiesDock) {
            m_propertiesDock->show();
            m_propertiesDock->raise();
        }
        updatePropertiesPanel();
        statusBar()->showMessage(tr("Properties palette opened."), 2500);
    });

    auto startNativeModifySelection = [this](const QString& command) {
        if (!m_view) return;
        const QString interactiveCommand = canonicalInteractiveModifyCommand(command);
        appendCommandConsoleMessage(interactiveModifyStartMessage(interactiveCommand));
        m_view->startInteractiveModifyCommand(interactiveCommand);
        if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
        m_view->setFocus(Qt::ShortcutFocusReason);
        statusBar()->showMessage(interactiveModifyStartMessage(interactiveCommand), 5000);
    };
    connect(offsetAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("OFFSET"));
    });
    connect(trimAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("TRIM"));
    });
    connect(extendAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("EXTEND"));
    });
    connect(chamferAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("CHAMFER"));
    });
    connect(filletAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("FILLET"));
    });



    // ── P2 : layouts, raster, workspaces, clean screen, palettes ─────────
    connect(attachImageAction, &QAction::triggered, this, &MainWindow::attachRasterImage);
    connect(layoutAction, &QAction::triggered, this, &MainWindow::configurePageLayout);
    connect(cleanScreenAction, &QAction::triggered, this, &MainWindow::toggleCleanScreen);
    connect(qtCadSceneAction, &QAction::toggled, this, &MainWindow::toggleQtCadScene);
    connect(refreshCadSceneAction, &QAction::triggered, this, &MainWindow::refreshCadScene);
    connect(workspaceAction, &QAction::triggered, this, &MainWindow::showWorkspaceManager);
    connect(palettesAction, &QAction::triggered, this, &MainWindow::showAllCadPalettes);
    connect(viewportsAction, &QAction::triggered, this, &MainWindow::showViewportManager);
    connect(namedViewsAction, &QAction::triggered, this, &MainWindow::showNamedViewsManager);
    connect(insertXrefAction, &QAction::triggered, this, &MainWindow::attachDwgReference);
    connect(attachDwfAction, &QAction::triggered, this, &MainWindow::attachDwfUnderlay);
    connect(attachDgnAction, &QAction::triggered, this, &MainWindow::attachDgnUnderlay);
    connect(loadAppAction, &QAction::triggered, this, &MainWindow::loadCadApplication);
    connect(newWindowAction, &QAction::triggered, this, &MainWindow::newWindowFromCurrentDrawing);
    connect(cascadeAction, &QAction::triggered, this, &MainWindow::cascadeWindows);
    connect(tileHorizAction, &QAction::triggered, this, &MainWindow::tileWindows);
    connect(tileVertAction, &QAction::triggered, this, &MainWindow::tileWindows);


    // ── P1 : commandes CAD avancees rendues fonctionnelles ─────────
    connect(splineAction, &QAction::triggered, this, [=]() {
        m_view->setDrawingTool(GraphicsView::DrawingTool::Spline);
        statusBar()->showMessage(tr("SPLINE mode: click control points, press Enter to finish. Local conversion to smoothed polyline."), 5000);
    });

    connect(mtextAction, &QAction::triggered, this, [=]() {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Multiline Text"), tr("MTEXT content:"), m_view->annotationText(), &ok);
        if (!ok) return;
        const double height = QInputDialog::getDouble(this, tr("Multiline Text"), tr("Text height:"), m_view->annotationTextHeight(), 0.1, 1000000.0, 3, &ok);
        if (!ok) return;
        m_view->setAnnotationText(text);
        m_view->setAnnotationTextHeight(height);
        if (m_view) { m_view->setAnnotationText(text); m_view->setAnnotationTextHeight(height); }
        m_view->setDrawingTool(GraphicsView::DrawingTool::Text);
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Text);
        statusBar()->showMessage(tr("MTEXT mode: click in the drawing to place multiline text."), 5000);
    });

    connect(textStyleAction, &QAction::triggered, this, [=]() {
        showTextStyleDialog();
    });

    connect(hatchStyleAction, &QAction::triggered, this, [=]() {
        showHatchStyleDialog(false, false);
    });

    connect(dimStyleAction, &QAction::triggered, this, [=]() {
        bool ok = false;
        const double height = QInputDialog::getDouble(this, tr("Dimension Style"), tr("Dimension text height:"), m_view->annotationTextHeight(), 0.1, 1000000.0, 3, &ok);
        if (!ok) return;
        m_view->setAnnotationTextHeight(height);
        if (m_view) m_view->setAnnotationTextHeight(height);
        statusBar()->showMessage(tr("DIMSTYLE: dimension text height = %1.").arg(height), 4000);
    });

    connect(editPolylineAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("PEDIT"));
    });

    connect(editHatchAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("HATCHEDIT"));
    });

    connect(editTextAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("DDEDIT"));
    });

    connect(matchPropsAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("MATCHPROP"));
    });

    connect(centerMarkAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("DIMCENTER"));
    });

    connect(dimDiameterAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("DIMDIAMETER"));
    });

    connect(dimRadiusAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("DIMRADIUS"));
    });

    connect(dimAlignedAction, &QAction::triggered, this, [=]() {
        m_view->setDrawingTool(GraphicsView::DrawingTool::LinearDimension);
        statusBar()->showMessage(tr("DIMALIGNED: click near endpoints/vertices; small square = reference detected without snap, then place the dimension."), 4000);
    });

    connect(dimAngularAction, &QAction::triggered, this, [=]() {
        statusBar()->showMessage(tr("DIMANGULAR P1: use two selected lines, then FILLET/CHAMFER to prepare the angle. Full graphical angular dimension is planned for P2."), 5000);
    });

    connect(deleteAction, &QAction::triggered, this, [=]() {
        if (!m_view) return;
        if (m_view->hasCadSelection()) {
            pushUndoState(tr("Erase"));
            m_view->deleteSelectedEntities();
            if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
            m_view->setFocus(Qt::ShortcutFocusReason);
            updatePropertiesPanel();
            statusBar()->showMessage(tr("Selection deleted."), 3000);
            return;
        }
        startNativeModifySelection(QStringLiteral("ERASE"));
    });
    connect(copyAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("COPY"));
    });
    connect(moveAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("MOVE"));
    });
    connect(rotateAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("ROTATE"));
    });
    connect(scaleAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("SCALE"));
    });
    connect(mirrorHAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("FLIPHORIZONTAL"));
    });
    connect(mirrorVAction, &QAction::triggered, this, [=]() {
        startNativeModifySelection(QStringLiteral("FLIPVERTICAL"));
    });


    connect(gridAction, &QAction::toggled, [=](bool checked) {
        m_view->setGridEnabled(checked);
        if (m_view) m_view->setGridEnabled(checked);
        statusBar()->showMessage(checked ? tr("Grid enabled") : tr("Grid disabled"));
    });
    connect(snapAction, &QAction::toggled, [=](bool checked) {
        m_view->setSnapEnabled(checked);
        if (m_view) m_view->setSnapEnabled(checked);
        statusBar()->showMessage(checked ? tr("Grid snap enabled") : tr("Grid snap disabled"));
    });
    connect(orthoAction, &QAction::toggled, [=](bool checked) {
        m_view->setOrthoEnabled(checked);
        if (m_view) m_view->setOrthoEnabled(checked);
        statusBar()->showMessage(checked ? tr("Ortho mode enabled") : tr("Ortho mode disabled"));
    });
    connect(osnapAction, &QAction::toggled, [=](bool checked) {
        m_view->setObjectSnapEnabled(checked);
        if (m_view) m_view->setObjectSnapEnabled(checked);
        statusBar()->showMessage(checked ? tr("Object snap enabled") : tr("Object snap disabled"));
    });

    auto connectObjectSnapOption = [=](QAction* action, GraphicsView::ObjectSnapMode mode) {
        connect(action, &QAction::toggled, [=](bool checked) {
            m_view->setObjectSnapModeEnabled(mode, checked);
            if (m_view) m_view->setObjectSnapModeEnabled(static_cast<GraphicsView::ObjectSnapMode>(static_cast<int>(mode)), checked);
            statusBar()->showMessage(tr("Snap %1 %2")
                .arg(GraphicsView::objectSnapModeName(mode), checked ? tr("enabled") : tr("disabled")));
        });
    };
    connectObjectSnapOption(snapEndpointAction, GraphicsView::ObjectSnapMode::Endpoint);
    connectObjectSnapOption(snapMidpointAction, GraphicsView::ObjectSnapMode::Midpoint);
    connectObjectSnapOption(snapCenterAction, GraphicsView::ObjectSnapMode::Center);
    connectObjectSnapOption(snapQuadrantAction, GraphicsView::ObjectSnapMode::Quadrant);
    connectObjectSnapOption(snapIntersectionAction, GraphicsView::ObjectSnapMode::Intersection);
    connectObjectSnapOption(snapTangentAction, GraphicsView::ObjectSnapMode::Tangent);
    connectObjectSnapOption(snapVertexAction, GraphicsView::ObjectSnapMode::Vertex);

    connect(gridSpacingAction, &QAction::triggered, [=]() {
        bool ok = false;
        const double spacing = QInputDialog::getDouble(
            this, tr("Grid spacing"), tr("Spacing:"),
            m_view->gridSpacing(), 0.0001, 1000000.0, 4, &ok);
        if (!ok) return;
        m_view->setGridSpacing(spacing);
        if (m_view) m_view->setGridSpacing(spacing);
        statusBar()->showMessage(tr("Grid spacing: %1").arg(spacing));
    });

    connect(selectAction, &QAction::triggered, [=]() {
        if (m_view) m_view->cancelActiveAction();
        statusBar()->showMessage(tr("ESC: action canceled. Selection / navigation mode."));
        if (m_commandLineEdit && m_view) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
    });
    connect(lineAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Line);
        statusBar()->showMessage(tr("Qt line mode: click-drag to draw"));
    });
    connect(polylineAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Polyline);
        statusBar()->showMessage(tr("Qt polyline mode: click vertices, press Enter/right-click to finish, C to close"));
    });
    connect(circleAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Circle);
        statusBar()->showMessage(tr("Qt circle mode: click the center, then drag the radius"));
    });
    connect(circleDiameterAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::CircleDiameter);
        statusBar()->showMessage(tr("Qt circle diameter mode: click both diameter endpoints"));
    });
    connect(circle3PointsAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Circle3Points);
        statusBar()->showMessage(tr("Qt 3-point circle mode: click three points"));
    });
    connect(arc3PointsAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Arc3Points);
        statusBar()->showMessage(tr("Qt 3-point arc mode: click start, arc point, end"));
    });
    connect(rectAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Rectangle);
        statusBar()->showMessage(tr("Qt rectangle mode: click-drag to draw"));
    });
    connect(ellipseAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Ellipse);
        statusBar()->showMessage(tr("Qt ellipse mode: click two corners of the box"));
    });
    connect(polygonAction, &QAction::triggered, [=]() {
        bool ok = false;
        const int sides = QInputDialog::getInt(this, tr("Polygon"), tr("Number of sides:"), m_view->polygonSides(), 3, 128, 1, &ok);
        if (!ok) return;
        m_view->setPolygonSides(sides);
        if (m_view) { m_view->setPolygonSides(sides); m_view->setDrawingTool(GraphicsView::DrawingTool::Polygon); }
        statusBar()->showMessage(tr("Qt polygon mode: click the center, then a vertex"));
    });
    connect(textAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Text);
        statusBar()->showMessage(tr("Qt text mode: click in the drawing to place text"));
    });
    connect(dimLinearAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::LinearDimension);
        statusBar()->showMessage(tr("Qt dimension mode: click point 1, point 2, then dimension position"));
    });
    connect(leaderAction, &QAction::triggered, [=]() {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Leader);
        statusBar()->showMessage(tr("Qt leader mode: click arrow, then text"));
    });
    connect(hatchAction, &QAction::triggered, [=]() {
        // AutoCAD-like workflow: Draw > Hatch first opens the floating hatch dialog.
        // The user then chooses Pick points or Select objects from that dialog.
        showHatchStyleDialog(false, true);
    });
    auto updateCadCoordinates = [=](const QPointF& p) {
        m_coordLabel->setText(QString("X: %1   Y: %2   | Grid:%3 Snap:%4 Ortho:%5 Osnap:%6")
            .arg(p.x(), 0, 'f', 2)
            .arg(p.y(), 0, 'f', 2)
            .arg(m_view->gridEnabled() ? "ON" : "OFF")
            .arg(m_view->snapEnabled() ? "ON" : "OFF")
            .arg(m_view->orthoEnabled() ? "ON" : "OFF")
            .arg(m_view->objectSnapEnabled() ? "ON" : "OFF"));
    };
    connect(m_view, &GraphicsView::mouseScenePositionChanged, updateCadCoordinates);

    connect(zoomInAction,  &QAction::triggered, m_view, &GraphicsView::zoomIn);
    connect(zoomOutAction, &QAction::triggered, m_view, &GraphicsView::zoomOut);
    connect(zoomWindowAction, &QAction::triggered, this, [this]() {
        if (!m_view) return;
        m_view->startZoomWindowMode();
        statusBar()->showMessage(tr("Zoom window: click-drag an area. Release to zoom, Esc cancels."), 5000);
    });
    connect(fitAction,     &QAction::triggered, m_view, &GraphicsView::zoomToExtents);
    connect(resetAction,   &QAction::triggered, m_view, &GraphicsView::zoomToExtents);

    connect(m_layersTree, &QTreeWidget::itemChanged,
            this, &MainWindow::onLayerToggled);
    connect(m_layersTree, &QTreeWidget::itemDoubleClicked, [=](QTreeWidgetItem*, int column) {
        if (column == 3) changeSelectedLayerColor();
        else if (column == 4) renameLayer();
        else if (column == 5) changeSelectedLayerLineType();
        else if (column == 6) changeSelectedLayerLineWeight();
    });

    buildDocumentLayersPanel();
    showCommandLine();
    optimizeDrawingWorkspaceLayout();
    QTimer::singleShot(0, this, [this]() { optimizeDrawingWorkspaceLayout(); });
    appendCommandConsoleMessage(tr("DRAW console ready. Type LINE and click the first point, then type 100, @100<45, @50,0 or click the second point."));
}


// ─────────────────────────────────────────────────────────
// New drawing CAD interne
// ─────────────────────────────────────────────────────────
void MainWindow::newDrawing()
{
    clearUndoHistory();
    DebugLogger::logContext("MainWindow::newDrawing");
    m_currentFile.clear();
    m_projectFile.clear();
    m_layers.clear();
    m_document.clear();
    m_document.setModified(false);
    m_view->refreshFromDocumentPreservingSelection();
    buildDocumentLayersPanel();
    updateWindowTitle();
    statusBar()->showMessage(tr("New CAD drawing created."));
}

// ─────────────────────────────────────────────────────────
// Open CAD project interne JSON
// ─────────────────────────────────────────────────────────
void MainWindow::openProject()
{
    clearUndoHistory();
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open CAD project"),
        m_projectFile.isEmpty() ? QDir::homePath() : QFileInfo(m_projectFile).absolutePath(),
        tr("CAD project (*.cad.json *.json);;All files (*)"));

    if (path.isEmpty()) return;

    DebugLogger::logContext("MainWindow::openProject", path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Cannot open project:\n%1\n\n%2").arg(path, file.errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Invalid JSON project:\n%1").arg(parseError.errorString()));
        return;
    }

    QString error;
    const QJsonObject root = doc.object();
    if (!m_document.fromJson(root, &error)) {
        DebugLogger::logError(QString("Project open failed %1 : %2").arg(path, error));
        QMessageBox::critical(this, tr("Error"),
                              tr("Cannot open project:\n%1\n\n%2").arg(path, error));
        return;
    }

    m_namedViews.clear();
    for (const QJsonValue& v : root.value("p3NamedViews").toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString().trimmed();
        if (!name.isEmpty()) {
            m_namedViews.insert(name, QRectF(o.value("x").toDouble(),
                                             o.value("y").toDouble(),
                                             o.value("w").toDouble(),
                                             o.value("h").toDouble()));
        }
    }
    m_externalReferences = root.value("p3ExternalReferences").toArray();

    m_currentFile.clear();
    m_layers.clear();
    m_projectFile = path;
    m_view->refreshFromDocumentPreservingSelection();
    buildDocumentLayersPanel();
    updateWindowTitle();
    DebugLogger::logInfo(QString("Project loaded: %1").arg(path));
    statusBar()->showMessage(tr("Project loaded: %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::saveProject()
{
    if (m_projectFile.isEmpty()) {
        saveProjectAs();
        return;
    }
    saveProjectToPath(m_projectFile);
}

void MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save CAD project"),
        m_projectFile.isEmpty() ? QDir::homePath() + "/drawing.cad.json" : m_projectFile,
        tr("CAD project (*.cad.json);;JSON (*.json);;All files (*)"));

    if (path.isEmpty()) return;
    if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".cad.json";
    saveProjectToPath(path);
}

bool MainWindow::saveProjectToPath(const QString& path)
{
    DebugLogger::logContext("MainWindow::saveProjectToPath", path);

    QJsonObject root = m_document.toJson();

    QJsonArray namedViews;
    for (auto it = m_namedViews.constBegin(); it != m_namedViews.constEnd(); ++it) {
        QJsonObject view;
        view["name"] = it.key();
        view["x"] = it.value().x();
        view["y"] = it.value().y();
        view["w"] = it.value().width();
        view["h"] = it.value().height();
        namedViews.append(view);
    }
    root["p3NamedViews"] = namedViews;
    root["p3ExternalReferences"] = m_externalReferences;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        DebugLogger::logError(QString("Project save failed %1 : %2").arg(path, file.errorString()));
        QMessageBox::critical(this, tr("Error"),
                              tr("Cannot save project:\n%1\n\n%2").arg(path, file.errorString()));
        return false;
    }

    const QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));

    m_projectFile = path;
    m_document.setModified(false);
    updateWindowTitle();
    DebugLogger::logInfo(QString("Project saved: %1").arg(path));
    statusBar()->showMessage(tr("Project saved: %1").arg(QFileInfo(path).fileName()));
    return true;
}

// ─────────────────────────────────────────────────────────
// Export SVG de la scène visible
// ─────────────────────────────────────────────────────────
void MainWindow::exportSvg()
{
    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export as SVG"),
        QDir::homePath() + "/drawing.svg",
        tr("SVG (*.svg);;All files (*)"));

    if (path.isEmpty()) return;
    if (!path.endsWith(".svg", Qt::CaseInsensitive)) path += ".svg";

    QRectF bounds = m_view->scene()->itemsBoundingRect();
    if (bounds.isNull() || !bounds.isValid()) bounds = m_document.limits();
    bounds = bounds.adjusted(-10.0, -10.0, 10.0, 10.0);

    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(QSize(1600, 1000));
    generator.setViewBox(bounds);
    generator.setTitle(tr("2D CAD Drawing"));
    generator.setDescription(tr("Export SVG depuis DWG Viewer Advanced"));

    QPainter painter(&generator);
    m_view->scene()->render(&painter, bounds, bounds);
    painter.end();

    statusBar()->showMessage(tr("SVG exported: %1").arg(QFileInfo(path).fileName()));
}


void MainWindow::exportDxf()
{
    DebugLogger::logContext("MainWindow::exportDxf");
    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Export editable DXF"),
        m_projectFile.isEmpty() ? QDir::homePath() : QFileInfo(m_projectFile).absolutePath(),
        tr("DXF AutoCAD (*.dxf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".dxf", Qt::CaseInsensitive)) path += ".dxf";

    QString error;
    DxfLibraryReport dxfReport;
    if (!DxfLibrary::saveEditable(path, m_document, &dxfReport, &error)) {
        DebugLogger::logError(QString("Native DXF export failed: %1 - %2").arg(path, error));
        QMessageBox::critical(this, tr("Export DXF"), tr("Cannot export DXF:\n%1").arg(error));
        return;
    }

    DebugLogger::logInfo(QString("Native DXF exported: %1 - %2").arg(path, dxfReport.summary()));
    statusBar()->showMessage(tr("Native editable DXF exported: %1").arg(QFileInfo(path).fileName()));
}


void MainWindow::exportDwgViaOda()
{
    DebugLogger::logContext("MainWindow::exportDwgViaOda");

    QString odaPath;
    if (!OdaFileConverter::isAvailable(&odaPath)) {
        QMessageBox::warning(
            this,
            tr("ODA File Converter"),
            tr("ODA File Converter was not found.\n\n"
               "Add it to PATH or define the variable:\n"
               "DWGVIEWER_ODA_CONVERTER_PATH=/chemin/vers/ODAFileConverter"));
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this,
        tr("Save DWG via ODA File Converter"),
        m_currentFile.isEmpty() ? QDir::homePath() : QFileInfo(m_currentFile).absolutePath(),
        tr("DWG AutoCAD (*.dwg)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".dwg", Qt::CaseInsensitive)) path += ".dwg";

    QTemporaryDir tempDir(QDir::tempPath() + QStringLiteral("/DWGViewer_SaveDWG_XXXXXX"));
    if (!tempDir.isValid()) {
        QMessageBox::critical(this, tr("Save DWG"), tr("Cannot create temporary folder."));
        return;
    }

    const QString tempDxf = tempDir.path() + QStringLiteral("/drawing_export.dxf");
    QString error;
    DxfLibraryReport dxfReport;
    if (!DxfLibrary::saveEditable(tempDxf, m_document, &dxfReport, &error)) {
        DebugLogger::logError(QString("Temporary native DXF export for DWG failed: %1").arg(error));
        QMessageBox::critical(this, tr("Save DWG"), tr("Cannot create temporary DXF:\n%1").arg(error));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const OdaConversionResult result = OdaFileConverter::convertDxfToDwg(tempDxf, path, QStringLiteral("ACAD2013"));
    QApplication::restoreOverrideCursor();

    if (!result.ok) {
        DebugLogger::logError(QString("ODA DXF->DWG failed: %1").arg(result.summary()));
        QMessageBox::critical(this, tr("Save DWG via ODA"), result.summary());
        return;
    }

    DebugLogger::logInfo(QString("DWG exported via ODA: %1").arg(path));
    statusBar()->showMessage(tr("DWG saved via ODA: %1").arg(QFileInfo(path).fileName()), 8000);
    QMessageBox::information(this, tr("Save DWG via ODA"),
                             tr("DWG saved successfully via ODA File Converter.\n\n%1").arg(result.summary()));
}



// ---------------------------------------------------------
// Phase 10 : mise en page, export PDF et impression
// ---------------------------------------------------------
QSizeF MainWindow::layoutPageSizeMm() const
{
    QSizeF size(210.0, 297.0); // A4
    if (m_layoutPageFormat == "A3") size = QSizeF(297.0, 420.0);
    else if (m_layoutPageFormat == "A2") size = QSizeF(420.0, 594.0);
    else if (m_layoutPageFormat == "A1") size = QSizeF(594.0, 841.0);
    else if (m_layoutPageFormat == "A0") size = QSizeF(841.0, 1189.0);

    if (m_layoutLandscape) size.transpose();
    return size;
}

double MainWindow::drawingUnitToMillimeters() const
{
    switch (m_document.unit()) {
    case CadDocument::Unit::Millimeter: return 1.0;
    case CadDocument::Unit::Centimeter: return 10.0;
    case CadDocument::Unit::Meter:      return 1000.0;
    case CadDocument::Unit::Unitless:   return 1.0;
    }
    return 1.0;
}

void MainWindow::renderLayoutPage(QPainter* painter, const QRectF& pageRectMm) const
{
    if (!painter) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillRect(pageRectMm, Qt::white);

    const double margin = m_layoutMarginMm;
    const double titleH = m_layoutTitleBlockHeightMm;
    const QRectF frame = pageRectMm.adjusted(margin, margin, -margin, -margin);
    const QRectF titleBlock(frame.left(), frame.bottom() - titleH, frame.width(), titleH);
    const QRectF drawingArea(frame.left() + 2.0, frame.top() + 2.0,
                             frame.width() - 4.0, frame.height() - titleH - 4.0);

    QPen framePen(Qt::black);
    framePen.setWidthF(0.25);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(frame);
    painter->drawRect(titleBlock);

    // Cartouche simple
    painter->drawLine(QPointF(titleBlock.left() + titleBlock.width() * 0.55, titleBlock.top()),
                      QPointF(titleBlock.left() + titleBlock.width() * 0.55, titleBlock.bottom()));
    painter->drawLine(QPointF(titleBlock.left() + titleBlock.width() * 0.75, titleBlock.top()),
                      QPointF(titleBlock.left() + titleBlock.width() * 0.75, titleBlock.bottom()));
    painter->drawLine(QPointF(titleBlock.left() + titleBlock.width() * 0.55, titleBlock.top() + titleH / 2.0),
                      QPointF(titleBlock.right(), titleBlock.top() + titleH / 2.0));

    QFont font = painter->font();
    font.setPointSizeF(3.0);
    painter->setFont(font);
    painter->drawText(titleBlock.adjusted(2.0, 2.0, -2.0, -2.0), Qt::AlignLeft | Qt::AlignTop,
                      tr("Titre : %1").arg(m_layoutTitle));
    painter->drawText(QRectF(titleBlock.left() + titleBlock.width() * 0.55 + 2.0, titleBlock.top() + 1.0,
                             titleBlock.width() * 0.20 - 4.0, titleH / 2.0 - 2.0),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      tr("Format : %1 %2").arg(m_layoutPageFormat, m_layoutLandscape ? tr("Paysage") : tr("Portrait")));
    painter->drawText(QRectF(titleBlock.left() + titleBlock.width() * 0.55 + 2.0, titleBlock.top() + titleH / 2.0,
                             titleBlock.width() * 0.20 - 4.0, titleH / 2.0 - 1.0),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      m_layoutFitToPage ? tr("Scale: Fit") : tr("Scale: 1/%1").arg(m_layoutScaleDenominator, 0, 'f', 0));
    painter->drawText(QRectF(titleBlock.left() + titleBlock.width() * 0.75 + 2.0, titleBlock.top() + 1.0,
                             titleBlock.width() * 0.25 - 4.0, titleH / 2.0 - 2.0),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      tr("Unit: %1").arg(m_document.unitName()));
    painter->drawText(QRectF(titleBlock.left() + titleBlock.width() * 0.75 + 2.0, titleBlock.top() + titleH / 2.0,
                             titleBlock.width() * 0.25 - 4.0, titleH / 2.0 - 1.0),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm"));

    QRectF source = m_view->scene()->itemsBoundingRect();
    if (!source.isValid() || source.isNull()) source = m_document.limits();
    source = source.adjusted(-5.0, -5.0, 5.0, 5.0);

    QRectF target = drawingArea;
    if (!m_layoutFitToPage && m_layoutScaleDenominator > 0.0) {
        const double factor = drawingUnitToMillimeters() / m_layoutScaleDenominator;
        const QSizeF targetSize(source.width() * factor, source.height() * factor);
        target = QRectF(QPointF(0, 0), targetSize);
        target.moveCenter(drawingArea.center());
        target = target.intersected(drawingArea);
    }

    painter->save();
    painter->setClipRect(drawingArea);
    m_view->scene()->render(painter, target, source, Qt::KeepAspectRatio);
    painter->restore();

    painter->setPen(QPen(Qt::black, 0.15, Qt::DashLine));
    painter->drawRect(drawingArea);
    painter->restore();
}

void MainWindow::configurePageLayout()
{
    bool ok = false;
    const QStringList formats = {"A4", "A3", "A2", "A1", "A0"};
    const QString format = QInputDialog::getItem(this, tr("Mise en page"), tr("Format papier :"),
                                                 formats, formats.indexOf(m_layoutPageFormat), false, &ok);
    if (!ok) return;

    const QStringList orientations = {tr("Paysage"), tr("Portrait")};
    const QString orientation = QInputDialog::getItem(this, tr("Mise en page"), tr("Orientation :"),
                                                      orientations, m_layoutLandscape ? 0 : 1, false, &ok);
    if (!ok) return;

    const QString title = QInputDialog::getText(this, tr("Mise en page"), tr("Titre du cartouche :"),
                                                QLineEdit::Normal, m_layoutTitle, &ok);
    if (!ok) return;

    const double margin = QInputDialog::getDouble(this, tr("Mise en page"), tr("Marge en mm :"),
                                                  m_layoutMarginMm, 0.0, 100.0, 2, &ok);
    if (!ok) return;

    const QStringList scaleModes = {tr("Fit to page"), tr("Manual scale")};
    const QString scaleMode = QInputDialog::getItem(this, tr("Mise en page"), tr("Scale mode:"),
                                                    scaleModes, m_layoutFitToPage ? 0 : 1, false, &ok);
    if (!ok) return;

    double denom = m_layoutScaleDenominator;
    if (scaleMode == tr("Manual scale")) {
        denom = QInputDialog::getDouble(this, tr("Mise en page"), tr("Scale denominator, example 100 for 1/100:"),
                                        m_layoutScaleDenominator, 1.0, 1000000.0, 0, &ok);
        if (!ok) return;
    }

    m_layoutPageFormat = format;
    m_layoutLandscape = (orientation == tr("Paysage"));
    m_layoutTitle = title.trimmed().isEmpty() ? tr("2D CAD Drawing") : title.trimmed();
    m_layoutMarginMm = margin;
    m_layoutFitToPage = (scaleMode == tr("Fit to page"));
    m_layoutScaleDenominator = denom;

    statusBar()->showMessage(tr("Mise en page : %1 %2, %3.")
                             .arg(m_layoutPageFormat,
                                  m_layoutLandscape ? tr("paysage") : tr("portrait"),
                                  m_layoutFitToPage ? tr("fit") : tr("scale 1/%1").arg(m_layoutScaleDenominator, 0, 'f', 0)));
}

void MainWindow::exportPdf()
{
    DebugLogger::logContext("MainWindow::exportPdf");
    QString path = QFileDialog::getSaveFileName(this, tr("Export as PDF"),
                                                QDir::homePath() + "/drawing.pdf",
                                                tr("PDF (*.pdf);;All files (*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);

    const QSizeF pageMm = layoutPageSizeMm();
    printer.setPageSize(QPageSize(pageMm, QPageSize::Millimeter, m_layoutPageFormat));
    printer.setPageOrientation(m_layoutLandscape ? QPageLayout::Landscape : QPageLayout::Portrait);

    QPainter painter(&printer);
    const QRectF pageRectMm(QPointF(0, 0), pageMm);
    painter.setWindow(0, 0, int(pageMm.width() * 10.0), int(pageMm.height() * 10.0));
    painter.scale(10.0, 10.0);
    renderLayoutPage(&painter, pageRectMm);
    painter.end();

    DebugLogger::logInfo(QString("PDF exporte: %1").arg(path));
    statusBar()->showMessage(tr("PDF exported: %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::printDrawing()
{
    QPrinter printer(QPrinter::HighResolution);
    const QSizeF pageMm = layoutPageSizeMm();
    printer.setPageSize(QPageSize(pageMm, QPageSize::Millimeter, m_layoutPageFormat));
    printer.setPageOrientation(m_layoutLandscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    printer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);

    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("Print drawing"));
    if (dialog.exec() != QDialog::Accepted) return;

    QPainter painter(&printer);
    painter.setWindow(0, 0, int(pageMm.width() * 10.0), int(pageMm.height() * 10.0));
    painter.scale(10.0, 10.0);
    renderLayoutPage(&painter, QRectF(QPointF(0, 0), pageMm));
    painter.end();

    statusBar()->showMessage(tr("Drawing sent to printer."));
}

// ─────────────────────────────────────────────────────────
// Paramètres de base : unité + limites
// ─────────────────────────────────────────────────────────
void MainWindow::configureDrawing()
{
    QStringList units;
    units << "mm" << "cm" << "m" << "unitless";

    bool ok = false;
    const QString unit = QInputDialog::getItem(
        this,
        tr("Drawing unit"),
        tr("Working unit:"),
        units,
        units.indexOf(m_document.unitName()),
        false,
        &ok);
    if (!ok) return;

    const QRectF oldLimits = m_document.limits();
    const double width = QInputDialog::getDouble(
        this,
        tr("Drawing limits"),
        tr("Workspace width:"),
        oldLimits.width(),
        0.001,
        100000000.0,
        3,
        &ok);
    if (!ok) return;

    const double height = QInputDialog::getDouble(
        this,
        tr("Drawing limits"),
        tr("Workspace height:"),
        oldLimits.height(),
        0.001,
        100000000.0,
        3,
        &ok);
    if (!ok) return;

    m_document.setUnit(CadDocument::unitFromName(unit));
    m_document.setLimits(QRectF(0.0, 0.0, width, height));
    m_view->scene()->setSceneRect(m_document.limits().adjusted(-20.0, -20.0, 20.0, 20.0));

    if (m_currentFile.isEmpty()) {
        m_view->refreshFromDocumentPreservingSelection();
    }

    updateWindowTitle();
    statusBar()->showMessage(tr("Settings updated: unit %1, limits %2 x %3")
                             .arg(unit).arg(width).arg(height));
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("DWG Viewer Advanced CAD 2D");
    if (!m_projectFile.isEmpty()) title += tr(" — %1").arg(QFileInfo(m_projectFile).fileName());
    else if (!m_currentFile.isEmpty()) title += tr(" — %1").arg(QFileInfo(m_currentFile).fileName());
    else title += tr(" — New drawing");
    setWindowTitle(title);
}


// ─────────────────────────────────────────────────────────
// Ouvrir un fichier DWG ou DXF
// ─────────────────────────────────────────────────────────
void MainWindow::openFile()
{
    clearUndoHistory();
    DebugLogger::logContext("MainWindow::openFile");
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open a DWG or DXF file — DWG via ODA, DWGView native DXF"),
        m_currentFile.isEmpty()
            ? QDir::homePath()
            : QFileInfo(m_currentFile).absolutePath(),
        tr("CAD files (*.dwg *.dxf);;DWG (*.dwg);;DXF (*.dxf);;All files (*)"));

    if (path.isEmpty()) return;

    DebugLogger::logInfo(QString("Asynchronous CAD loading: %1").arg(path));
    m_view->prepareForSceneReset();
    m_view->scene()->clear();
    m_document.clear();
    m_document.setModified(false);
    m_projectFile.clear();
    m_layers.clear();
    m_layersTree->clear();

    auto* progressDialog = new QProgressDialog(tr("Preparing to open..."), QString(), 0, 100, this);
    progressDialog->setWindowTitle(tr("Opening CAD file"));
    progressDialog->setCancelButton(nullptr);
    progressDialog->setMinimumDuration(0);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setWindowModality(Qt::ApplicationModal);
    progressDialog->setValue(0);
    progressDialog->show();

    statusBar()->showMessage(tr("Opening in background: %1").arg(QFileInfo(path).fileName()));
    QApplication::setOverrideCursor(Qt::WaitCursor);

    QPointer<MainWindow> self(this);
    QPointer<QProgressDialog> progressPtr(progressDialog);

    QThread* worker = QThread::create([self, progressPtr, path]() {
        auto result = std::make_shared<AsyncCadImportResult>();

        CadImportOptions importOptions;
        importOptions.expandBlocks = true;
        importOptions.approximateCurves = true;
        importOptions.largeFileMode = true;
        importOptions.preferOdaForDwg = true;
        importOptions.progressCallback = [progressPtr](int value, const QString& text) {
            QMetaObject::invokeMethod(qApp, [progressPtr, value, text]() {
                if (!progressPtr) return;
                progressPtr->setLabelText(text);
                progressPtr->setValue(qBound(0, value, 100));
            }, Qt::QueuedConnection);
        };

        result->ok = CadImportLibrary::loadEditable(
            path,
            result->document,
            result->layers,
            &result->importMessage,
            &result->importReport,
            importOptions);

        QMetaObject::invokeMethod(qApp, [self, progressPtr, result, path]() mutable {
            QApplication::restoreOverrideCursor();

            if (!self) {
                if (progressPtr) {
                    progressPtr->close();
                    progressPtr->deleteLater();
                }
                return;
            }

            if (!result->ok) {
                if (progressPtr) {
                    progressPtr->close();
                    progressPtr->deleteLater();
                }
                DebugLogger::logError(QString("Strict CAD import failed: %1 - %2").arg(path, result->importMessage));
                QMessageBox::critical(self, QObject::tr("Error"),
                    QObject::tr("Cannot import this CAD file:\n%1\n\n%2\n\n"
                                "Active flow: DWG -> ODA File Converter in background -> temporary DXF -> DWGView native parser.\n"
                                "Direct DXF -> DWGView native parser. No internal fallback is used.")
                        .arg(path, result->importMessage));
                self->statusBar()->showMessage(QObject::tr("Strict CAD loading failed."));
                return;
            }

            self->m_document = std::move(result->document);
            self->m_layers = result->layers;
            self->m_currentFile = path;
            self->m_projectFile.clear();

            if (progressPtr) {
                progressPtr->setLabelText(QObject::tr("Building optimized Qt 2D view..."));
                progressPtr->setValue(94);
                progressPtr->show();
                qApp->processEvents(QEventLoop::ExcludeUserInputEvents);
            }

            DebugLogger::logInfo("Debut renderToQtScene CAD 2D");
            self->refreshCadScene();
            DebugLogger::logInfo("End renderToQtScene CAD 2D");

            if (progressPtr) {
                progressPtr->setLabelText(QObject::tr("Endalizing open..."));
                progressPtr->setValue(100);
                progressPtr->close();
                progressPtr->deleteLater();
            }

            self->updateWindowTitle();
            self->buildDocumentLayersPanel();
            self->updatePropertiesPanel();

            DebugLogger::logInfo("Debut zoom etendu Qt apres import CAD");
            if (self->m_view) self->m_view->zoomToExtents();
            DebugLogger::logInfo("End Qt zoom extents after CAD import");

            DebugLogger::logInfo(QString("CAD importe strictement: %1, entites=%2, message=%3")
                                 .arg(path).arg(self->m_document.entityCount()).arg(result->importMessage));
            self->statusBar()->showMessage(result->importReport.summary(), 10000);
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}



// ─────────────────────────────────────────────────────────
// Panneau calques
// ─────────────────────────────────────────────────────────
void MainWindow::buildLayersPanel()
{
    // Bloquer les signaux pendant la construction
    m_layersTree->blockSignals(true);
    m_layersTree->clear();

    for (const LayerInfo& lay : m_layers)
    {
        auto* item = new QTreeWidgetItem(m_layersTree);

        item->setText(0, "");
        item->setCheckState(1, lay.visible ? Qt::Checked : Qt::Unchecked);

        QPixmap pm(14, 14);
        pm.fill(lay.color);
        item->setIcon(3, QIcon(pm));
        item->setText(3, lay.color.name());
        item->setText(4, lay.name);
        item->setText(5, "CAD");

        item->setData(0, Qt::UserRole, lay.name);  // stocker le nom
        item->setData(0, Qt::UserRole + 1, QStringLiteral("cad"));
    }

    m_layersTree->sortItems(4, Qt::AscendingOrder);
    m_layersTree->blockSignals(false);
}

// ─────────────────────────────────────────────────────────
// Clic sur une case "Visible" dans le panneau calques
// ─────────────────────────────────────────────────────────


void MainWindow::rebuildDocumentLayersPanelLater()
{
    if (m_pendingLayersPanelRebuild) return;
    m_pendingLayersPanelRebuild = true;
    QTimer::singleShot(0, this, [this]() {
        m_pendingLayersPanelRebuild = false;
        buildDocumentLayersPanel();
    });
}

void MainWindow::buildDocumentLayersPanel()
{
    if (!m_layersTree) return;
    if (m_rebuildingLayersPanel) {
        rebuildDocumentLayersPanelLater();
        return;
    }

    m_rebuildingLayersPanel = true;
    QSignalBlocker treeBlocker(m_layersTree);
    std::unique_ptr<QSignalBlocker> comboBlocker;
    if (m_currentLayerCombo) {
        comboBlocker = std::make_unique<QSignalBlocker>(m_currentLayerCombo);
        m_currentLayerCombo->clear();
    }

    m_layersTree->clear();
    QTreeWidgetItem* currentItem = nullptr;
    const QString currentLayer = m_document.currentLayerName();

    for (const CadLayer& lay : m_document.layers())
    {
        auto* item = new QTreeWidgetItem(m_layersTree);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);

        item->setText(0, "");
        item->setCheckState(0, lay.name == currentLayer ? Qt::Checked : Qt::Unchecked);

        item->setText(1, "");
        item->setCheckState(1, lay.visible ? Qt::Checked : Qt::Unchecked);

        item->setText(2, "");
        item->setCheckState(2, lay.locked ? Qt::Checked : Qt::Unchecked);

        QPixmap pm(14, 14);
        pm.fill(lay.color);
        item->setIcon(3, QIcon(pm));
        item->setText(3, lay.color.name());

        item->setText(4, lay.name);
        item->setText(5, lay.lineType);
        item->setText(6, QString::number(lay.lineWeight, 'f', 2));
        item->setData(0, Qt::UserRole, lay.name);
        item->setData(0, Qt::UserRole + 1, QStringLiteral("document"));

        if (m_currentLayerCombo) {
            m_currentLayerCombo->addItem(lay.name);
        }

        if (lay.name == currentLayer) {
            currentItem = item;
            QFont f = item->font(4);
            f.setBold(true);
            for (int c = 0; c < m_layersTree->columnCount(); ++c) item->setFont(c, f);
        }
    }

    m_layersTree->sortItems(4, Qt::AscendingOrder);

    if (currentItem) {
        m_layersTree->setCurrentItem(currentItem, 4, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_layersTree->scrollToItem(currentItem, QAbstractItemView::EnsureVisible);
    }

    if (m_currentLayerCombo) {
        const int comboIndex = m_currentLayerCombo->findText(currentLayer);
        if (comboIndex >= 0) m_currentLayerCombo->setCurrentIndex(comboIndex);
    }

    m_rebuildingLayersPanel = false;
}

void MainWindow::onLayerToggled(QTreeWidgetItem* item, int column)
{
    if (!item || m_rebuildingLayersPanel) return;

    const QString name = item->data(0, Qt::UserRole).toString();
    if (name.isEmpty()) return;

    const QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type != QLatin1String("document")) {
        if (m_layers.contains(name) && (column == 0 || column == 1)) {
            m_layers[name].visible = (item->checkState(column) == Qt::Checked);
            applyLayerVisibility();
        }
        return;
    }

    if (!m_document.layers().contains(name)) {
        rebuildDocumentLayersPanelLater();
        return;
    }

    if (column == 0) {
        if (item->checkState(0) == Qt::Checked) {
            setCurrentDrawingLayer(name, true);
        } else if (name == m_document.currentLayerName()) {
            // Do not rebuild/delete QTreeWidgetItems inside itemChanged. Just restore the check state safely.
            QSignalBlocker blocker(m_layersTree);
            item->setCheckState(0, Qt::Checked);
        }
        return;
    }

    if (column == 1) {
        const bool visible = item->checkState(1) == Qt::Checked;
        if (!visible && name == m_document.currentLayerName()) {
            appendCommandConsoleMessage(tr("LAYER: the current drawing layer must remain visible. Choose another current layer first."));
            QSignalBlocker blocker(m_layersTree);
            item->setCheckState(1, Qt::Checked);
            return;
        }
        m_document.setLayerVisible(name, visible);
        m_view->refreshFromDocumentPreservingSelection();
        statusBar()->showMessage(tr("Layer %1 visibility changed.").arg(name));
        return;
    }

    if (column == 2) {
        const bool locked = item->checkState(2) == Qt::Checked;
        if (locked && name == m_document.currentLayerName()) {
            appendCommandConsoleMessage(tr("LAYER: the current drawing layer cannot be locked. Choose another current layer first."));
            QSignalBlocker blocker(m_layersTree);
            item->setCheckState(2, Qt::Unchecked);
            return;
        }
        m_document.setLayerLocked(name, locked);
        m_view->refreshFromDocumentPreservingSelection();
        statusBar()->showMessage(tr("Layer %1 lock state changed.").arg(name));
        return;
    }
}

QString MainWindow::selectedDocumentLayerName() const
{
    if (!m_layersTree) return {};
    QTreeWidgetItem* item = m_layersTree->currentItem();
    if (!item) return m_document.currentLayerName();
    const QString type = item->data(0, Qt::UserRole + 1).toString();
    if (type != QLatin1String("document")) return m_document.currentLayerName();
    const QString name = item->data(0, Qt::UserRole).toString();
    return name.isEmpty() ? m_document.currentLayerName() : name;
}

void MainWindow::createLayer()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New layer"), tr("Layer name:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (!m_document.createLayer(name)) {
        QMessageBox::warning(this, tr("Layer"), tr("Cannot create this layer. It may already exist."));
        return;
    }
    m_document.setCurrentLayerName(name);
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection();
    statusBar()->showMessage(tr("Layer created and set current: %1").arg(name));
}

void MainWindow::deleteLayer()
{
    const QString name = selectedDocumentLayerName();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this, tr("Delete layer"), tr("Delete layer '%1'?\nObjects on this layer will be moved to layer 0.").arg(name)) != QMessageBox::Yes) return;

    QString error;
    if (!m_document.deleteLayer(name, &error)) {
        QMessageBox::warning(this, tr("Layer"), error);
        return;
    }
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection();
    statusBar()->showMessage(tr("Layer deleted: %1").arg(name));
}

void MainWindow::renameLayer()
{
    const QString oldName = selectedDocumentLayerName();
    if (oldName.isEmpty()) return;

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename layer"), tr("New name:"), QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || newName.isEmpty()) return;
    if (!m_document.renameLayer(oldName, newName)) {
        QMessageBox::warning(this, tr("Layer"), tr("Cannot rename this layer. Layer 0 cannot be renamed and the new name must be unique."));
        return;
    }
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection();
    statusBar()->showMessage(tr("Layer renamed: %1 -> %2").arg(oldName, newName));
}

void MainWindow::setSelectedLayerCurrent()
{
    const QString name = selectedDocumentLayerName();
    if (name.isEmpty()) return;
    setCurrentDrawingLayer(name, true);
}

void MainWindow::onCurrentLayerComboChanged(const QString& name)
{
    if (name.trimmed().isEmpty()) return;
    if (name == m_document.currentLayerName()) return;
    setCurrentDrawingLayer(name, true);
}

void MainWindow::setCurrentDrawingLayer(const QString& name, bool makeDrawable)
{
    const QString cleanName = name.trimmed().isEmpty() ? QStringLiteral("0") : name.trimmed();
    m_document.ensureLayer(cleanName);

    bool redrawNeeded = false;
    if (makeDrawable) {
        const CadLayer before = m_document.layers().value(cleanName);
        if (!before.visible) {
            m_document.setLayerVisible(cleanName, true);
            redrawNeeded = true;
        }
        if (before.locked) {
            m_document.setLayerLocked(cleanName, false);
            redrawNeeded = true;
        }
    }

    m_document.setCurrentLayerName(cleanName);
    rebuildDocumentLayersPanelLater();
    if (redrawNeeded && m_view) m_view->refreshFromDocumentPreservingSelection();

    if (m_view) {
        m_view->setFocus(Qt::ShortcutFocusReason);
    }
    appendCommandConsoleMessage(tr("Current drawing layer: %1%2")
        .arg(cleanName, makeDrawable ? tr(" (visible / unlocked)") : QString()));
    statusBar()->showMessage(tr("Current drawing layer: %1").arg(cleanName), 4000);
}

void MainWindow::changeSelectedLayerColor()
{
    const QString name = selectedDocumentLayerName();
    if (name.isEmpty()) return;
    const QColor oldColor = m_document.layers().value(name).color;
    const QColor color = QColorDialog::getColor(oldColor, this, tr("Layer color"));
    if (!color.isValid()) return;
    m_document.setLayerColor(name, color, true);
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection();
    statusBar()->showMessage(tr("Layer %1 color changed.").arg(name));
}

void MainWindow::changeSelectedLayerLineWeight()
{
    const QString name = selectedDocumentLayerName();
    if (name.isEmpty()) return;
    bool ok = false;
    const double oldWeight = m_document.layers().value(name).lineWeight;
    const double weight = QInputDialog::getDouble(this, tr("Layer weight"), tr("Line weight:"), oldWeight, 0.0, 100.0, 2, &ok);
    if (!ok) return;
    m_document.setLayerLineWeight(name, weight, true);
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection();
    statusBar()->showMessage(tr("Layer %1 weight changed.").arg(name));
}

void MainWindow::changeSelectedLayerLineType()
{
    const QString name = selectedDocumentLayerName();
    if (name.isEmpty()) return;
    const QStringList types = {"Continuous", "Dashed", "Dotted", "DashDot"};
    const QString oldType = m_document.layers().value(name).lineType;
    bool ok = false;
    const QString type = QInputDialog::getItem(this, tr("Line type"), tr("Type :"), types, qMax(0, types.indexOf(oldType)), false, &ok);
    if (!ok || type.isEmpty()) return;
    m_document.setLayerLineType(name, type, true);
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection();
    statusBar()->showMessage(tr("Layer %1 line type changed.").arg(name));
}

void MainWindow::moveSelectionToCurrentLayer()
{
    if (!m_view->hasCadSelection()) {
        QMessageBox::information(this, tr("Layers"), tr("Select one or more CAD objects first."));
        return;
    }
    m_view->moveSelectedEntitiesToLayer(m_document.currentLayerName());
    statusBar()->showMessage(tr("Selection moved to current layer: %1").arg(m_document.currentLayerName()));
}

void MainWindow::applyLayerVisibility()
{
    // Depuis l'import DWG éditable, la visibilité est portée par CadDocument.
    // Il suffit donc de redessiner le document interne.
    m_view->refreshFromDocumentPreservingSelection();
    updatePropertiesPanel();
}

// ─────────────────────────────────────────────────────────
// Changer la couleur de fond
// ─────────────────────────────────────────────────────────
void MainWindow::changeBackground()
{
    QColor color = QColorDialog::getColor(
        QColor(30, 30, 30), this, tr("Background color"));
    if (color.isValid())
        m_view->setBackgroundBrush(color);
}

// ─────────────────────────────────────────────────────────
// Panneau propriétés des objets selecteds
// ─────────────────────────────────────────────────────────
void MainWindow::buildPropertiesPanel()
{
    m_propertiesDock = new QDockWidget(tr("Properties"), this);
    m_propertiesDock->setObjectName(QStringLiteral("PropertiesDock"));
    m_propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_propertiesDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_propertiesDock->setMinimumWidth(150);
    m_propertiesDock->setMaximumWidth(220);

    auto* panel = new QWidget(m_propertiesDock);
    panel->setMinimumWidth(150);
    panel->setMaximumWidth(220);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_selectionInfoLabel = new QLabel(tr("No object selected"), panel);
    layout->addWidget(m_selectionInfoLabel);

    auto* form = new QFormLayout();

    m_objectLayerCombo = new QComboBox(panel);
    form->addRow(tr("Layer :"), m_objectLayerCombo);

    m_objectColorButton = new QPushButton(tr("Object color..."), panel);
    form->addRow(tr("Color :"), m_objectColorButton);

    m_objectLineWeightSpin = new QDoubleSpinBox(panel);
    m_objectLineWeightSpin->setRange(0.0, 100.0);
    m_objectLineWeightSpin->setDecimals(2);
    m_objectLineWeightSpin->setSingleStep(0.10);
    form->addRow(tr("Weight:"), m_objectLineWeightSpin);

    m_objectLineTypeCombo = new QComboBox(panel);
    m_objectLineTypeCombo->addItems({"Continuous", "Dashed", "Dotted", "DashDot"});
    form->addRow(tr("Line type:"), m_objectLineTypeCombo);

    auto* applyButton = new QPushButton(tr("Apply to selected objects"), panel);
    form->addRow(QString(), applyButton);
    layout->addLayout(form);

    m_geometryTable = new QTableWidget(panel);
    m_geometryTable->setColumnCount(2);
    m_geometryTable->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    m_geometryTable->horizontalHeader()->setStretchLastSection(true);
    m_geometryTable->verticalHeader()->setVisible(false);
    m_geometryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_geometryTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_geometryTable->setMinimumHeight(110);
    m_geometryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(m_geometryTable, 1);

    m_propertiesDock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applySelectedObjectProperties);

    updatePropertiesPanel();
}

void MainWindow::updatePropertiesPanel()
{
    if (!m_selectionInfoLabel || !m_objectLayerCombo || !m_geometryTable) return;

    const QVector<int> indices = m_view->selectedEntityIndices();
    const bool hasSelection = !indices.isEmpty();

    QSignalBlocker b1(m_objectLayerCombo);
    QSignalBlocker b2(m_objectLineWeightSpin);
    QSignalBlocker b3(m_objectLineTypeCombo);

    m_objectLayerCombo->clear();
    for (const CadLayer& layer : m_document.layers()) {
        m_objectLayerCombo->addItem(layer.name);
    }

    m_objectLayerCombo->setEnabled(hasSelection);
    m_objectColorButton->setEnabled(hasSelection);
    m_objectLineWeightSpin->setEnabled(hasSelection);
    m_objectLineTypeCombo->setEnabled(hasSelection);

    m_geometryTable->clearContents();
    m_geometryTable->setRowCount(0);

    if (!hasSelection) {
        m_selectionInfoLabel->setText(tr("No object selected"));
        return;
    }

    m_selectionInfoLabel->setText(indices.size() == 1
        ? tr("1 object selected")
        : tr("%1 objects selected").arg(indices.size()));

    const CadEntity* first = m_document.entityAt(indices.first());
    if (!first) return;

    QString commonLayer = first->layer();
    QString commonLineType = first->lineType();
    double commonWeight = first->lineWeight();
    bool sameLayer = true;
    bool sameLineType = true;
    bool sameWeight = true;

    for (int index : indices) {
        const CadEntity* entity = m_document.entityAt(index);
        if (!entity) continue;
        if (entity->layer() != commonLayer) sameLayer = false;
        if (entity->lineType() != commonLineType) sameLineType = false;
        if (std::abs(entity->lineWeight() - commonWeight) > 1e-9) sameWeight = false;
    }

    if (sameLayer) {
        const int layerIndex = m_objectLayerCombo->findText(commonLayer);
        if (layerIndex >= 0) m_objectLayerCombo->setCurrentIndex(layerIndex);
    } else {
        m_objectLayerCombo->setCurrentIndex(-1);
    }

    m_objectLineWeightSpin->setValue(sameWeight ? commonWeight : 0.0);

    const int typeIndex = m_objectLineTypeCombo->findText(commonLineType);
    if (sameLineType && typeIndex >= 0) m_objectLineTypeCombo->setCurrentIndex(typeIndex);
    else m_objectLineTypeCombo->setCurrentIndex(0);

    if (indices.size() == 1) {
        fillGeometryProperties(indices.first());
    } else {
        m_geometryTable->setRowCount(1);
        m_geometryTable->setItem(0, 0, new QTableWidgetItem(tr("Select")));
        m_geometryTable->setItem(0, 1, new QTableWidgetItem(tr("%1 objects").arg(indices.size())));
    }
}

void MainWindow::fillGeometryProperties(int entityIndex)
{
    if (!m_geometryTable) return;
    m_geometryTable->clearContents();
    m_geometryTable->setRowCount(0);

    const CadEntity* entity = m_document.entityAt(entityIndex);
    if (!entity) return;

    auto addRow = [&](const QString& name, const QString& value) {
        const int row = m_geometryTable->rowCount();
        m_geometryTable->insertRow(row);
        m_geometryTable->setItem(row, 0, new QTableWidgetItem(name));
        m_geometryTable->setItem(row, 1, new QTableWidgetItem(value));
    };
    auto num = [](double v) { return QString::number(v, 'f', 3); };
    auto pt = [&](const QPointF& p) { return QString("%1, %2").arg(num(p.x()), num(p.y())); };

    addRow(tr("Index"), QString::number(entityIndex));
    addRow(tr("Layer"), entity->layer());
    addRow(tr("Color"), entity->color().name());
    addRow(tr("Line type"), entity->lineType());
    addRow(tr("Weight"), num(entity->lineWeight()));

    if (const auto* line = dynamic_cast<const CadLine*>(entity)) {
        const QPointF a = line->start();
        const QPointF b = line->end();
        addRow(tr("Type"), tr("Line"));
        addRow(tr("Start"), pt(a));
        addRow(tr("End"), pt(b));
        addRow(tr("Length"), num(QLineF(a, b).length()));
        addRow(tr("Angle"), num(QLineF(a, b).angle()));
        return;
    }

    if (const auto* circle = dynamic_cast<const CadCircle*>(entity)) {
        addRow(tr("Type"), tr("Circle"));
        addRow(tr("Center"), pt(circle->center()));
        addRow(tr("Radius"), num(circle->radius()));
        addRow(tr("Diameter"), num(circle->radius() * 2.0));
        addRow(tr("Circumference"), num(2.0 * kCadPi * circle->radius()));
        addRow(tr("Area"), num(kCadPi * circle->radius() * circle->radius()));
        return;
    }

    if (const auto* rect = dynamic_cast<const CadRectangle*>(entity)) {
        const QRectF r = rect->rect().normalized();
        addRow(tr("Type"), tr("Rectangle"));
        addRow(tr("Origin"), pt(r.topLeft()));
        addRow(tr("Width"), num(r.width()));
        addRow(tr("Height"), num(r.height()));
        addRow(tr("Area"), num(r.width() * r.height()));
        addRow(tr("Perimeter"), num(2.0 * (r.width() + r.height())));
        return;
    }

    if (const auto* pl = dynamic_cast<const CadPolyline*>(entity)) {
        double length = 0.0;
        const QVector<QPointF>& points = pl->points();
        for (int i = 1; i < points.size(); ++i) length += QLineF(points[i - 1], points[i]).length();
        if (pl->closed() && points.size() > 2) length += QLineF(points.last(), points.first()).length();
        addRow(tr("Type"), tr("Polyline"));
        addRow(tr("Vertices"), QString::number(points.size()));
        addRow(tr("Closed"), pl->closed() ? tr("Yes") : tr("No"));
        addRow(tr("Length"), num(length));
        return;
    }

    if (const auto* arc = dynamic_cast<const CadArc*>(entity)) {
        addRow(tr("Type"), tr("Arc"));
        addRow(tr("Center"), pt(arc->center()));
        addRow(tr("Radius"), num(arc->radius()));
        addRow(tr("Start angle"), num(arc->startAngleDeg()));
        addRow(tr("Opening"), num(arc->spanAngleDeg()));
        addRow(tr("Arc length"), num(std::abs(arc->spanAngleDeg()) * kCadPi / 180.0 * arc->radius()));
        return;
    }

    if (const auto* ellipse = dynamic_cast<const CadEllipse*>(entity)) {
        const QRectF r = ellipse->rect().normalized();
        addRow(tr("Type"), tr("Ellipse"));
        addRow(tr("Center"), pt(r.center()));
        addRow(tr("Radius X"), num(r.width() / 2.0));
        addRow(tr("Radius Y"), num(r.height() / 2.0));
        addRow(tr("Width"), num(r.width()));
        addRow(tr("Height"), num(r.height()));
        return;
    }

    if (const auto* polygon = dynamic_cast<const CadPolygon*>(entity)) {
        addRow(tr("Type"), tr("Polygon"));
        addRow(tr("Center"), pt(polygon->center()));
        addRow(tr("Radius"), num(polygon->radius()));
        addRow(tr("Sides"), QString::number(polygon->sides()));
        addRow(tr("Rotation"), num(polygon->rotationDeg()));
        return;
    }

    if (const auto* hatch = dynamic_cast<const CadHatch*>(entity)) {
        addRow(tr("Type"), tr("Hatch / fill"));
        addRow(tr("Vertices contour"), QString::number(hatch->boundary().size()));
        addRow(tr("Motif"), hatch->pattern());
        addRow(tr("Hatch scale"), num(hatch->hatchScale()));
        addRow(tr("Angle"), num(hatch->angleDeg()));
        return;
    }

    if (const auto* block = dynamic_cast<const CadBlockReference*>(entity)) {
        addRow(tr("Type"), tr("Block / symbole"));
        addRow(tr("Nom"), block->blockName());
        addRow(tr("Point base"), pt(block->basePoint()));
        addRow(tr("Insertion"), pt(block->insertionPoint()));
        addRow(tr("Scale"), num(block->scaleFactor()));
        addRow(tr("Rotation"), num(block->rotationDeg()));
        addRow(tr("Definition objects"), QString::number(block->definitionEntities().size()));
        return;
    }

    if (const auto* text = dynamic_cast<const CadText*>(entity)) {
        addRow(tr("Type"), tr("Text"));
        addRow(tr("Position"), pt(text->position()));
        addRow(tr("Text"), text->text());
        addRow(tr("Height"), num(text->height()));
        addRow(tr("Rotation"), num(text->rotationDeg()));
        return;
    }

    if (const auto* dim = dynamic_cast<const CadLinearDimension*>(entity)) {
        addRow(tr("Type"), tr("Linear dimension"));
        addRow(tr("Point 1"), pt(dim->first()));
        addRow(tr("Point 2"), pt(dim->second()));
        addRow(tr("Position cote"), pt(dim->dimensionPoint()));
        addRow(tr("Mesure"), num(dim->measuredLength()));
        addRow(tr("Text height"), num(dim->textHeight()));
        return;
    }

    if (const auto* leader = dynamic_cast<const CadLeader*>(entity)) {
        addRow(tr("Type"), tr("Leader line"));
        addRow(tr("Pointe"), pt(leader->arrowPoint()));
        addRow(tr("Text position"), pt(leader->textPoint()));
        addRow(tr("Text"), leader->text());
        addRow(tr("Text height"), num(leader->textHeight()));
        return;
    }
}

void MainWindow::applySelectedObjectProperties()
{
    const QVector<int> indices = m_view->selectedEntityIndices();
    if (indices.isEmpty()) return;

    const QString layerName = m_objectLayerCombo->currentText().trimmed();
    const QString lineType = m_objectLineTypeCombo->currentText().trimmed().isEmpty()
        ? QStringLiteral("Continuous")
        : m_objectLineTypeCombo->currentText().trimmed();
    const double lineWeight = m_objectLineWeightSpin->value();

    if (!layerName.isEmpty()) m_document.ensureLayer(layerName);

    for (int index : indices) {
        CadEntity* entity = m_document.entityAt(index);
        if (!entity) continue;
        if (!layerName.isEmpty()) entity->setLayer(layerName);
        entity->setLineType(lineType);
        entity->setLineWeight(lineWeight);
    }

    m_document.setModified(true);
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection(indices);
    updatePropertiesPanel();
    statusBar()->showMessage(tr("Properties applied to %1 object(s).").arg(indices.size()));
}

void MainWindow::changeSelectedObjectColor()
{
    const QVector<int> indices = m_view->selectedEntityIndices();
    if (indices.isEmpty()) return;

    QColor initial = Qt::white;
    if (const CadEntity* first = m_document.entityAt(indices.first())) initial = first->color();

    const QColor color = QColorDialog::getColor(initial, this, tr("Selected object color"));
    if (!color.isValid()) return;

    for (int index : indices) {
        if (CadEntity* entity = m_document.entityAt(index)) entity->setColor(color);
    }

    m_document.setModified(true);
    m_view->refreshFromDocumentPreservingSelection(indices);
    updatePropertiesPanel();
    statusBar()->showMessage(tr("Color applied to %1 object(s).").arg(indices.size()));
}

void MainWindow::createBlockFromSelection()
{
    const QVector<int> indices = m_view->selectedEntityIndices();
    if (indices.isEmpty()) {
        QMessageBox::information(this, tr("Create block"), tr("Select the objects to convert into a block first."));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Create block"), tr("Block name:"),
                                               QLineEdit::Normal, tr("Block_%1").arg(m_document.blockNames().size() + 1), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QRectF bounds;
    bool hasBounds = false;
    for (QGraphicsItem* item : m_view->scene()->selectedItems()) {
        bool itemOk = false;
        item->data(1).toInt(&itemOk);
        if (!itemOk) continue;
        if (!hasBounds) {
            bounds = item->sceneBoundingRect();
            hasBounds = true;
        } else {
            bounds = bounds.united(item->sceneBoundingRect());
        }
    }
    const QPointF basePoint = hasBounds ? bounds.center() : m_view->mapToScene(m_view->viewport()->rect().center());

    QString error;
    pushUndoState(tr("Create block"));
    if (!m_document.createBlockFromEntities(name, indices, basePoint, &error)) {
        rollbackUndoIfNeeded();
        QMessageBox::warning(this, tr("Create block"), error);
        return;
    }

    m_view->refreshFromDocumentPreservingSelection({});
    applyBlockVisibility();
    updatePropertiesPanel();
    updateWindowTitle();
    statusBar()->showMessage(tr("Block '%1' created from %2 object(s).").arg(name).arg(indices.size()));
}

void MainWindow::createEmptyBlock()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               tr("Add Empty Block"),
                                               tr("Block name:"),
                                               QLineEdit::Normal,
                                               tr("Block_%1").arg(m_document.blockNames().size() + 1),
                                               &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    const QPointF basePoint = m_view ? m_view->mapToScene(m_view->viewport()->rect().center()) : QPointF();
    QString error;
    pushUndoState(tr("Add empty block"));
    if (!m_document.createEmptyBlock(name, basePoint, &error)) {
        rollbackUndoIfNeeded();
        QMessageBox::warning(this, tr("Add Empty Block"), error);
        return;
    }

    updateWindowTitle();
    appendCommandConsoleMessage(tr("BLOCK: empty definition '%1' created.").arg(name));
    statusBar()->showMessage(tr("Empty block '%1' created.").arg(name), 3000);
}

void MainWindow::renameBlock()
{
    const QStringList names = m_document.blockNames();
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Rename Block"), tr("No block is defined in this drawing."));
        return;
    }

    bool ok = false;
    const QString oldName = QInputDialog::getItem(this, tr("Rename Block"), tr("Block:"), names, 0, false, &ok);
    if (!ok || oldName.isEmpty()) return;

    const QString newName = QInputDialog::getText(this, tr("Rename Block"), tr("New block name:"),
                                                  QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == oldName) return;

    QString error;
    pushUndoState(tr("Rename block"));
    if (!m_document.renameBlockDefinition(oldName, newName, &error)) {
        rollbackUndoIfNeeded();
        QMessageBox::warning(this, tr("Rename Block"), error);
        return;
    }

    if (m_view) {
        m_view->refreshFromDocumentPreservingSelection();
        applyBlockVisibility();
    }
    updatePropertiesPanel();
    updateWindowTitle();
    appendCommandConsoleMessage(tr("BLOCK: '%1' renamed to '%2'.").arg(oldName, newName));
    statusBar()->showMessage(tr("Block renamed."), 3000);
}

void MainWindow::duplicateBlock()
{
    const QStringList names = m_document.blockNames();
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Duplicate Block"), tr("No block is defined in this drawing."));
        return;
    }

    bool ok = false;
    const QString sourceName = QInputDialog::getItem(this, tr("Duplicate Block"), tr("Source block:"), names, 0, false, &ok);
    if (!ok || sourceName.isEmpty()) return;

    const QString newName = QInputDialog::getText(this, tr("Duplicate Block"), tr("New block name:"),
                                                  QLineEdit::Normal, sourceName + QStringLiteral("_copy"), &ok).trimmed();
    if (!ok || newName.isEmpty()) return;

    QString error;
    pushUndoState(tr("Duplicate block"));
    if (!m_document.duplicateBlockDefinition(sourceName, newName, &error)) {
        rollbackUndoIfNeeded();
        QMessageBox::warning(this, tr("Duplicate Block"), error);
        return;
    }

    updateWindowTitle();
    appendCommandConsoleMessage(tr("BLOCK: '%1' duplicated as '%2'.").arg(sourceName, newName));
    statusBar()->showMessage(tr("Block duplicated."), 3000);
}

void MainWindow::removeBlock()
{
    const QStringList names = m_document.blockNames();
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Remove Block"), tr("No block is defined in this drawing."));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getItem(this, tr("Remove Block"), tr("Block:"), names, 0, false, &ok);
    if (!ok || name.isEmpty()) return;

    if (QMessageBox::question(this, tr("Remove Block"),
                              tr("Remove unused block definition '%1'?\nReferenced blocks cannot be removed until their references are exploded or deleted.").arg(name)) != QMessageBox::Yes)
        return;

    QString error;
    pushUndoState(tr("Remove block"));
    if (!m_document.removeBlockDefinition(name, &error)) {
        rollbackUndoIfNeeded();
        QMessageBox::warning(this, tr("Remove Block"), error);
        return;
    }

    updateWindowTitle();
    appendCommandConsoleMessage(tr("BLOCK: unused definition '%1' removed.").arg(name));
    statusBar()->showMessage(tr("Block removed."), 3000);
}

void MainWindow::purgeUnusedBlocks()
{
    QStringList purged;
    pushUndoState(tr("Purge unused blocks"));
    const int count = m_document.purgeUnusedBlockDefinitions(&purged);
    if (count <= 0) {
        rollbackUndoIfNeeded();
        QMessageBox::information(this, tr("Purge Unused Blocks"), tr("No unused block definition was found."));
        return;
    }

    updateWindowTitle();
    appendCommandConsoleMessage(tr("BLOCK PURGE: %1 definition(s) removed: %2").arg(count).arg(purged.join(QStringLiteral(", "))));
    QMessageBox::information(this, tr("Purge Unused Blocks"), tr("Removed %1 unused block definition(s).\n%2").arg(count).arg(purged.join(QStringLiteral("\n"))));
}

void MainWindow::explodeSelectedBlocks()
{
    const QVector<int> indices = m_view ? m_view->selectedEntityIndices() : QVector<int>();
    if (indices.isEmpty()) {
        QMessageBox::information(this, tr("Explode Block"), tr("Select one or more block references first."));
        return;
    }

    QString error;
    pushUndoState(tr("Explode block"));
    const int created = m_document.explodeBlockReferences(indices, &error);
    if (created <= 0) {
        rollbackUndoIfNeeded();
        QMessageBox::information(this, tr("Explode Block"), error.isEmpty() ? tr("The selection does not contain a block reference.") : error);
        return;
    }

    if (m_view) {
        m_view->refreshFromDocumentPreservingSelection();
        applyBlockVisibility();
    }
    updatePropertiesPanel();
    updateWindowTitle();
    appendCommandConsoleMessage(tr("EXPLODE BLOCK: %1 entity/entities created.").arg(created));
    statusBar()->showMessage(tr("Block exploded into %1 object(s).").arg(created), 3000);
}

void MainWindow::applyBlockVisibility()
{
    if (!m_view || !m_view->scene()) return;
    for (QGraphicsItem* item : m_view->scene()->items()) {
        if (!item || !item->data(1).isValid()) continue;
        bool ok = false;
        const int index = item->data(1).toInt(&ok);
        if (!ok || index < 0) continue;
        const CadEntity* entity = m_document.entityAt(index);
        if (entity && entity->type() == CadEntity::Type::BlockReference)
            item->setVisible(m_blocksVisible);
    }
}

void MainWindow::showAllBlocks()
{
    m_blocksVisible = true;
    applyBlockVisibility();
    appendCommandConsoleMessage(tr("BLOCK: all block references are visible."));
    statusBar()->showMessage(tr("All blocks are visible."), 2500);
}

void MainWindow::hideAllBlocks()
{
    m_blocksVisible = false;
    applyBlockVisibility();
    appendCommandConsoleMessage(tr("BLOCK: all block references are hidden in the view."));
    statusBar()->showMessage(tr("All blocks hidden in this view."), 2500);
}

void MainWindow::selectBlockReferences()
{
    if (!m_view || !m_view->scene()) return;
    int count = 0;
    for (QGraphicsItem* item : m_view->scene()->items()) {
        if (!item || !item->data(1).isValid()) continue;
        bool ok = false;
        const int index = item->data(1).toInt(&ok);
        if (!ok || index < 0) continue;
        const CadEntity* entity = m_document.entityAt(index);
        if (entity && entity->type() == CadEntity::Type::BlockReference && item->isVisible() && item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) {
            item->setSelected(true);
            ++count;
        }
    }
    updatePropertiesPanel();
    appendCommandConsoleMessage(tr("BLOCK: %1 block reference(s) selected.").arg(count));
    statusBar()->showMessage(tr("%1 block reference(s) selected.").arg(count), 2500);
}

void MainWindow::deselectBlockReferences()
{
    if (!m_view || !m_view->scene()) return;
    int count = 0;
    for (QGraphicsItem* item : m_view->scene()->items()) {
        if (!item || !item->isSelected() || !item->data(1).isValid()) continue;
        bool ok = false;
        const int index = item->data(1).toInt(&ok);
        if (!ok || index < 0) continue;
        const CadEntity* entity = m_document.entityAt(index);
        if (entity && entity->type() == CadEntity::Type::BlockReference) {
            item->setSelected(false);
            ++count;
        }
    }
    updatePropertiesPanel();
    appendCommandConsoleMessage(tr("BLOCK: %1 block reference(s) deselected.").arg(count));
    statusBar()->showMessage(tr("Block references deselected."), 2500);
}

void MainWindow::showBlockManager()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Block Manager"));
    dialog.resize(620, 420);

    auto* root = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({tr("Name"), tr("Objects"), tr("References")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(table, 1);

    auto reloadTable = [&]() {
        const QStringList names = m_document.blockNames();
        table->setRowCount(names.size());
        for (int row = 0; row < names.size(); ++row) {
            const QString name = names.at(row);
            table->setItem(row, 0, new QTableWidgetItem(name));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(m_document.blockDefinitionEntityCount(name))));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(m_document.blockReferenceCount(name))));
        }
    };
    auto selectedName = [&]() -> QString {
        const int row = table->currentRow();
        QTableWidgetItem* item = row >= 0 ? table->item(row, 0) : nullptr;
        return item ? item->text() : QString();
    };

    auto* buttons = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("Add Empty"), &dialog);
    auto* insertButton = new QPushButton(tr("Insert"), &dialog);
    auto* renameButton = new QPushButton(tr("Rename"), &dialog);
    auto* duplicateButton = new QPushButton(tr("Duplicate"), &dialog);
    auto* removeButton = new QPushButton(tr("Remove"), &dialog);
    auto* purgeButton = new QPushButton(tr("Purge"), &dialog);
    auto* closeButton = new QPushButton(tr("Close"), &dialog);
    for (QPushButton* b : {addButton, insertButton, renameButton, duplicateButton, removeButton, purgeButton}) buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, [&]() { createEmptyBlock(); reloadTable(); });
    connect(insertButton, &QPushButton::clicked, this, [&]() { Q_UNUSED(selectedName()); insertBlock(); reloadTable(); });
    connect(renameButton, &QPushButton::clicked, this, [&]() { renameBlock(); reloadTable(); });
    connect(duplicateButton, &QPushButton::clicked, this, [&]() { duplicateBlock(); reloadTable(); });
    connect(removeButton, &QPushButton::clicked, this, [&]() { removeBlock(); reloadTable(); });
    connect(purgeButton, &QPushButton::clicked, this, [&]() { purgeUnusedBlocks(); reloadTable(); });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    reloadTable();
    dialog.exec();
}

void MainWindow::insertBlock()
{
    const QStringList names = m_document.blockNames();
    if (names.isEmpty()) {
        QMessageBox::information(this, tr("Insert block"), tr("No block is defined in this drawing."));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getItem(this, tr("Insert block"), tr("Block :"), names, 0, false, &ok);
    if (!ok || name.isEmpty()) return;

    const double x = QInputDialog::getDouble(this, tr("Insert block"), tr("Insertion point X:"),
                                             m_view->mapToScene(m_view->viewport()->rect().center()).x(),
                                             -100000000.0, 100000000.0, 4, &ok);
    if (!ok) return;
    const double y = QInputDialog::getDouble(this, tr("Insert block"), tr("Insertion point Y:"),
                                             m_view->mapToScene(m_view->viewport()->rect().center()).y(),
                                             -100000000.0, 100000000.0, 4, &ok);
    if (!ok) return;
    const double scale = QInputDialog::getDouble(this, tr("Insert block"), tr("Scale :"),
                                                 1.0, -1000000.0, 1000000.0, 6, &ok);
    if (!ok) return;
    const double rotation = QInputDialog::getDouble(this, tr("Insert block"), tr("Rotation in degrees:"),
                                                    0.0, -360000.0, 360000.0, 3, &ok);
    if (!ok) return;

    QString error;
    pushUndoState(tr("Insert block"));
    if (!m_document.insertBlockReference(name, QPointF(x, y), scale, rotation, &error)) {
        rollbackUndoIfNeeded();
        QMessageBox::warning(this, tr("Insert block"), error);
        return;
    }

    m_view->refreshFromDocumentPreservingSelection({m_document.entityCount() - 1});
    applyBlockVisibility();
    updatePropertiesPanel();
    updateWindowTitle();
    statusBar()->showMessage(tr("Block '%1' inserted.").arg(name));
}

// ─────────────────────────────────────────────────────────
// P0 AutoCAD-like command infrastructure: Undo/Redo + command line
// ─────────────────────────────────────────────────────────
void MainWindow::pushUndoState(const QString& label)
{
    m_undoStack.append(m_document.toJson());
    m_undoLabels.append(label.isEmpty() ? tr("Modification") : label);
    if (m_undoStack.size() > 100) {
        m_undoStack.removeFirst();
        m_undoLabels.removeFirst();
    }
    m_redoStack.clear();
    m_redoLabels.clear();
}

void MainWindow::rollbackUndoIfNeeded()
{
    if (!m_undoStack.isEmpty())
        m_undoStack.removeLast();

    if (!m_undoLabels.isEmpty())
        m_undoLabels.removeLast();
}

void MainWindow::restoreDocumentSnapshot(const QJsonObject& snapshot)
{
    QString error;
    if (!m_document.fromJson(snapshot, &error)) {
        QMessageBox::warning(this, tr("Restauration"), tr("Cannot restore drawing state:\n%1").arg(error));
        return;
    }
    buildDocumentLayersPanel();
    m_view->refreshFromDocumentPreservingSelection();
    updatePropertiesPanel();
    m_document.setModified(true);
}

void MainWindow::clearUndoHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoLabels.clear();
    m_redoLabels.clear();
}

void MainWindow::undoCommand()
{
    if (m_undoStack.isEmpty()) {
        statusBar()->showMessage(tr("UNDO: no action to undo."), 2500);
        return;
    }
    m_redoStack.append(m_document.toJson());
    m_redoLabels.append(m_undoLabels.isEmpty() ? tr("Redo") : m_undoLabels.last());
    const QJsonObject snapshot = m_undoStack.takeLast();
    const QString label = m_undoLabels.isEmpty() ? QString() : m_undoLabels.takeLast();
    restoreDocumentSnapshot(snapshot);
    statusBar()->showMessage(tr("UNDO : %1").arg(label.isEmpty() ? tr("change") : label), 3000);
}

void MainWindow::redoCommand()
{
    if (m_redoStack.isEmpty()) {
        statusBar()->showMessage(tr("REDO: no action to redo."), 2500);
        return;
    }
    m_undoStack.append(m_document.toJson());
    m_undoLabels.append(m_redoLabels.isEmpty() ? tr("Undo") : m_redoLabels.last());
    const QJsonObject snapshot = m_redoStack.takeLast();
    const QString label = m_redoLabels.isEmpty() ? QString() : m_redoLabels.takeLast();
    restoreDocumentSnapshot(snapshot);
    statusBar()->showMessage(tr("REDO : %1").arg(label.isEmpty() ? tr("change") : label), 3000);
}


void MainWindow::attachRasterImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Raster Image Reference"),
        m_currentFile.isEmpty() ? QDir::homePath() : QFileInfo(m_currentFile).absolutePath(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff);;All files (*)"));
    if (path.isEmpty()) return;

    QPixmap pix(path);
    if (pix.isNull()) {
        QMessageBox::warning(this, tr("IMAGEATTACH"), tr("Cannot load image:\n%1").arg(path));
        return;
    }

    bool ok = false;
    const double x = QInputDialog::getDouble(this, tr("IMAGEATTACH"), tr("Insertion point X:"), 0.0, -1e12, 1e12, 3, &ok);
    if (!ok) return;
    const double y = QInputDialog::getDouble(this, tr("IMAGEATTACH"), tr("Insertion point Y:"), 0.0, -1e12, 1e12, 3, &ok);
    if (!ok) return;
    const double scale = QInputDialog::getDouble(this, tr("IMAGEATTACH"), tr("Image scale:"), 1.0, 0.000001, 1e9, 6, &ok);
    if (!ok) return;

    auto* item = new QGraphicsPixmapItem(pix);
    item->setPos(QPointF(x, y));
    item->setScale(scale);
    item->setTransformationMode(Qt::SmoothTransformation);
    item->setFlag(QGraphicsItem::ItemIsSelectable, true);
    item->setFlag(QGraphicsItem::ItemIsMovable, true);
    item->setZValue(-10000.0);
    item->setData(0, QStringLiteral("RasterReference"));
    item->setData(1, path);
    m_view->scene()->addItem(item);

    statusBar()->showMessage(tr("IMAGEATTACH: image attached as a movable raster reference. Note: P2 does not export it to JSON/DXF yet."), 7000);
}

void MainWindow::toggleCleanScreen()
{
    m_cleanScreenActive = !m_cleanScreenActive;
    const QList<QToolBar*> bars = findChildren<QToolBar*>();
    for (QToolBar* bar : bars) {
        if (m_cleanScreenActive) {
            bar->setProperty("p2_prev_visible", bar->isVisible());
            bar->hide();
        } else {
            bar->setVisible(bar->property("p2_prev_visible").toBool());
        }
    }

    const QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        if (dock == m_commandDock) continue;
        if (m_cleanScreenActive) {
            dock->setProperty("p2_prev_visible", dock->isVisible());
            dock->hide();
        } else {
            dock->setVisible(dock->property("p2_prev_visible").toBool());
        }
    }

    statusBar()->showMessage(m_cleanScreenActive
        ? tr("CLEANSCREEN enabled: toolbars and palettes hidden. Run Clean Screen again or type CLEANSCREEN to restore.")
        : tr("CLEANSCREEN disabled: interface restored."), 5000);
}

void MainWindow::showWorkspaceManager()
{
    const QStringList presets = {
        tr("AutoCAD 2008 Classic"),
        tr("Drafting & Annotation"),
        tr("Large File Review"),
        tr("Clean Screen")
    };
    bool ok = false;
    const QString preset = QInputDialog::getItem(this, tr("Workspaces"), tr("Espace de travail :"), presets, 0, false, &ok);
    if (!ok) return;
    applyWorkspacePreset(preset);
}

void MainWindow::applyWorkspacePreset(const QString& preset)
{
    const QString p = preset.toLower();
    const QList<QToolBar*> bars = findChildren<QToolBar*>();
    const QList<QDockWidget*> docks = findChildren<QDockWidget*>();

    for (QToolBar* bar : bars) bar->show();
    for (QDockWidget* dock : docks) dock->show();

    if (p.contains("clean")) {
        if (!m_cleanScreenActive) toggleCleanScreen();
        return;
    }

    if (m_cleanScreenActive) {
        m_cleanScreenActive = false;
        for (QToolBar* bar : bars) bar->show();
        for (QDockWidget* dock : docks) dock->show();
    }

    if (p.contains("large")) {
        for (QToolBar* bar : bars) {
            const QString name = bar->objectName();
            const bool keep = name.contains("Standard") || name.contains("Layers") || name.contains("ObjectSnap");
            bar->setVisible(keep);
        }
        if (m_propertiesDock) m_propertiesDock->hide();
        m_view->setPerformanceModeEnabled(true);
        statusBar()->showMessage(tr("Workspace Large File Review: lighter interface + performance mode active."), 5000);
        return;
    }

    if (p.contains("drafting")) {
        for (QToolBar* bar : bars) {
            const QString name = bar->objectName();
            bar->setVisible(!name.contains("Modify") || true);
        }
        if (m_propertiesDock) m_propertiesDock->show();
        optimizeDrawingWorkspaceLayout();
        statusBar()->showMessage(tr("Workspace Drafting & Annotation: drawing, modify, layers/properties stacked as tabs with maximum drawing area."), 5000);
        return;
    }

    optimizeDrawingWorkspaceLayout();
    statusBar()->showMessage(tr("Workspace AutoCAD 2008 Classic applied with compact palettes."), 5000);
}

void MainWindow::showAllCadPalettes()
{
    const QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        dock->show();
        dock->raise();
    }
    if (m_propertiesDock) {
        m_propertiesDock->show();
        m_propertiesDock->raise();
    }
    optimizeDrawingWorkspaceLayout();
    statusBar()->showMessage(tr("PALETTES: layers, properties, and command line shown as stacked tabs to maximize the drawing area."), 4000);
}



// ─────────────────────────────────────────────────────────
// P3 AutoCAD-like advanced shell: XREF, underlays, named views,
// viewports, plugins and multi-window helpers.
// ─────────────────────────────────────────────────────────
void MainWindow::registerExternalReference(const QString& kind, const QString& path, const QString& note)
{
    QJsonObject ref;
    ref["kind"] = kind;
    ref["path"] = path;
    ref["fileName"] = QFileInfo(path).fileName();
    ref["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    ref["note"] = note;
    m_externalReferences.append(ref);
    m_document.setModified(true);
}

bool MainWindow::attachExternalCadFile(const QString& path, const QString& kind)
{
    if (path.isEmpty()) return false;

    CadDocument imported;
    QMap<QString, LayerInfo> importedLayers;
    QString importMessage;
    CadImportReport importReport;
    CadImportOptions importOptions;
    importOptions.expandBlocks = true;
    importOptions.approximateCurves = true;
    importOptions.largeFileMode = true;
    const bool ok = CadImportLibrary::loadEditable(path, imported, importedLayers, &importMessage, &importReport, importOptions);

    if (!ok) {
        QMessageBox::warning(this, tr("XREF"),
            tr("Cannot load external reference:\n%1\n\n%2").arg(path, importMessage));
        return false;
    }

    bool inputOk = false;
    const QPointF defaultPos = m_view->mapToScene(m_view->viewport()->rect().center());
    const double x = QInputDialog::getDouble(this, tr("XREF"), tr("Insertion point X:"), defaultPos.x(), -1e12, 1e12, 4, &inputOk);
    if (!inputOk) return false;
    const double y = QInputDialog::getDouble(this, tr("XREF"), tr("Insertion point Y:"), defaultPos.y(), -1e12, 1e12, 4, &inputOk);
    if (!inputOk) return false;
    const double scale = QInputDialog::getDouble(this, tr("XREF"), tr("Scale :"), 1.0, 0.000001, 1e9, 6, &inputOk);
    if (!inputOk) return false;
    const double rotation = QInputDialog::getDouble(this, tr("XREF"), tr("Rotation in degrees:"), 0.0, -360000.0, 360000.0, 3, &inputOk);
    if (!inputOk) return false;

    pushUndoState(tr("Attach XREF"));

    const QString prefix = QFileInfo(path).completeBaseName().left(48).replace('|', '_');
    const QPointF origin(0.0, 0.0);
    int added = 0;
    m_document.reserveEntities(static_cast<size_t>(m_document.entityCount() + imported.entityCount()));

    for (const CadLayer& layer : imported.layers()) {
        const QString refLayer = QStringLiteral("%1|%2").arg(prefix, layer.name);
        m_document.ensureLayer(refLayer);
        CadLayer& targetLayer = m_document.layers()[refLayer];
        targetLayer.color = layer.color;
        targetLayer.visible = layer.visible;
        targetLayer.locked = layer.locked;
        targetLayer.lineWeight = layer.lineWeight;
        targetLayer.lineType = layer.lineType;
    }

    for (const auto& src : imported.entities()) {
        if (!src) continue;
        std::unique_ptr<CadEntity> copy = src->clone();
        copy->setLayer(QStringLiteral("%1|%2").arg(prefix, copy->layer()));
        copy->scale(origin, scale);
        copy->rotate(origin, rotation);
        copy->translate(QPointF(x, y));
        m_document.addEntity(std::move(copy));
        ++added;
        if ((added % 2000) == 0) QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    registerExternalReference(kind, path, tr("Imported as editable XREF with layer prefix %1|; entities=%2").arg(prefix).arg(added));
    m_view->configureForDocumentSize(m_document.entityCount());
    m_view->refreshFromDocumentPreservingSelection();
    applyBlockVisibility();
    m_view->updateSceneRectToContent();
    buildDocumentLayersPanel();
    updatePropertiesPanel();
    updateWindowTitle();
    statusBar()->showMessage(tr("XREF attached: %1 entity/entities, layers prefixed %2|").arg(added).arg(prefix), 6000);
    return true;
}

void MainWindow::attachDwgReference()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("DWG/DXF Reference"),
        m_currentFile.isEmpty() ? QDir::homePath() : QFileInfo(m_currentFile).absolutePath(),
        tr("CAD references (*.dwg *.dxf);;DWG (*.dwg);;DXF (*.dxf);;All files (*)"));
    attachExternalCadFile(path, QStringLiteral("CAD-XREF"));
}

void MainWindow::attachDwfUnderlay()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("DWF Underlay"), QDir::homePath(), tr("DWF (*.dwf *.dwfx);;All files (*)"));
    if (path.isEmpty()) return;
    registerExternalReference(QStringLiteral("DWF-UNDERLAY"), path, tr("Underlay reference saved. Native DWF rendering requires a dedicated DWF backend."));
    QMessageBox::information(this, tr("DWFATTACH"), tr("DWF underlay saved in the project:\n%1\n\nThe viewer keeps the reference and can display it when a DWF backend is added.").arg(path));
}

void MainWindow::attachDgnUnderlay()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("DGN Underlay"), QDir::homePath(), tr("DGN (*.dgn);;All files (*)"));
    if (path.isEmpty()) return;
    registerExternalReference(QStringLiteral("DGN-UNDERLAY"), path, tr("Underlay reference saved. Native DGN rendering requires a dedicated DGN backend."));
    QMessageBox::information(this, tr("DGNATTACH"), tr("DGN underlay saved in the project:\n%1\n\nThe viewer keeps the reference and can display it when a DGN backend is added.").arg(path));
}

void MainWindow::showNamedViewsManager()
{
    const QStringList choices = { tr("Save current view"), tr("Restore a view"), tr("Delete a view"), tr("List views") };
    bool ok = false;
    const QString op = QInputDialog::getItem(this, tr("Named Views"), tr("Operation:"), choices, 0, false, &ok);
    if (!ok) return;

    if (op == choices.at(0)) {
        const QRectF r = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
        const QString name = QInputDialog::getText(this, tr("Named Views"), tr("View name:"), QLineEdit::Normal,
                                                   tr("View_%1").arg(m_namedViews.size() + 1), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        m_namedViews.insert(name, r);
        m_document.setModified(true);
        statusBar()->showMessage(tr("Named view saved: %1").arg(name), 4000);
        return;
    }

    if (m_namedViews.isEmpty()) {
        QMessageBox::information(this, tr("Named Views"), tr("No named view saved."));
        return;
    }

    const QStringList names = m_namedViews.keys();
    if (op == choices.at(1)) {
        const QString name = QInputDialog::getItem(this, tr("Named Views"), tr("Vue :"), names, 0, false, &ok);
        if (!ok || !m_namedViews.contains(name)) return;
        m_view->fitInView(m_namedViews.value(name), Qt::KeepAspectRatio);
        statusBar()->showMessage(tr("View restored: %1").arg(name), 3000);
        return;
    }

    if (op == choices.at(2)) {
        const QString name = QInputDialog::getItem(this, tr("Named Views"), tr("View to delete:"), names, 0, false, &ok);
        if (!ok || !m_namedViews.contains(name)) return;
        m_namedViews.remove(name);
        m_document.setModified(true);
        statusBar()->showMessage(tr("View deleted: %1").arg(name), 3000);
        return;
    }

    QString text;
    for (auto it = m_namedViews.constBegin(); it != m_namedViews.constEnd(); ++it) {
        const QRectF r = it.value();
        text += QStringLiteral("%1  X=%2 Y=%3 W=%4 H=%5\n")
            .arg(it.key()).arg(r.x(), 0, 'f', 2).arg(r.y(), 0, 'f', 2).arg(r.width(), 0, 'f', 2).arg(r.height(), 0, 'f', 2);
    }
    QMessageBox::information(this, tr("Named Views"), text);
}

void MainWindow::showViewportManager()
{
    const QStringList choices = { tr("Create paper viewport frame"), tr("Zoom model extents"), tr("Show viewport info") };
    bool ok = false;
    const QString op = QInputDialog::getItem(this, tr("Viewports"), tr("Operation:"), choices, 0, false, &ok);
    if (!ok) return;

    if (op == choices.at(0)) {
        const QPointF c = m_view->mapToScene(m_view->viewport()->rect().center());
        const double w = QInputDialog::getDouble(this, tr("Viewport"), tr("Frame width:"), 100.0, 1.0, 1e9, 3, &ok);
        if (!ok) return;
        const double h = QInputDialog::getDouble(this, tr("Viewport"), tr("Frame height:"), 70.0, 1.0, 1e9, 3, &ok);
        if (!ok) return;
        auto* rect = m_view->scene()->addRect(QRectF(c.x() - w / 2.0, c.y() - h / 2.0, w, h), QPen(QColor(80, 160, 255), 0.0, Qt::DashLine));
        rect->setFlag(QGraphicsItem::ItemIsSelectable, true);
        rect->setFlag(QGraphicsItem::ItemIsMovable, true);
        rect->setData(0, QStringLiteral("PaperViewportFrame"));
        rect->setZValue(20000.0);
        statusBar()->showMessage(tr("Paper viewport created as a movable frame. Full layout export is planned for a later step."), 6000);
        return;
    }

    if (op == choices.at(1)) {
        if (m_view) m_view->zoomToExtents();
        return;
    }

    const QRectF r = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
    QMessageBox::information(this, tr("Viewports"),
        tr("Current model viewport:\nX=%1\nY=%2\nWidth=%3\nHeight=%4\n\nNamed views: %5\nExternal references: %6")
            .arg(r.x(), 0, 'f', 2).arg(r.y(), 0, 'f', 2).arg(r.width(), 0, 'f', 2).arg(r.height(), 0, 'f', 2)
            .arg(m_namedViews.size()).arg(m_externalReferences.size()));
}

void MainWindow::loadCadApplication()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Application"), QDir::homePath(), tr("Plugins (*.so *.dll *.dylib);;All files (*)"));
    if (path.isEmpty()) return;

    QLibrary* lib = new QLibrary(path, this);
    if (!lib->load()) {
        QMessageBox::warning(this, tr("APPLOAD"), tr("Cannot load plugin:\n%1\n\n%2").arg(path, lib->errorString()));
        lib->deleteLater();
        return;
    }

    using InitFn = bool (*)(QMainWindow*);
    InitFn init = reinterpret_cast<InitFn>(lib->resolve("initializeCadPlugin"));
    if (init) {
        const bool initialized = init(this);
        statusBar()->showMessage(initialized ? tr("Plugin loaded and initialized: %1").arg(QFileInfo(path).fileName())
                                             : tr("Plugin loaded, but initialization returned false."), 6000);
    } else {
        statusBar()->showMessage(tr("Plugin loaded: %1. Optional symbol initializeCadPlugin not found.").arg(QFileInfo(path).fileName()), 6000);
    }
}

void MainWindow::newWindowFromCurrentDrawing()
{
    auto* window = new MainWindow();
    window->restoreDocumentSnapshot(m_document.toJson());
    window->m_namedViews = m_namedViews;
    window->m_externalReferences = m_externalReferences;
    window->m_currentFile = m_currentFile;
    window->m_projectFile = m_projectFile;
    window->updateWindowTitle();
    window->show();
    window->raise();
    statusBar()->showMessage(tr("New window created with a copy of the current drawing."), 4000);
}

void MainWindow::cascadeWindows()
{
    int offset = 0;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w)) {
            mw->resize(1200, 800);
            mw->move(40 + offset, 40 + offset);
            offset += 32;
        }
    }
    statusBar()->showMessage(tr("Windows arranged in cascade."), 3000);
}

void MainWindow::tileWindows()
{
    QList<MainWindow*> windows;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w)) windows.append(mw);
    }
    if (windows.isEmpty()) return;

    const QRect screen = QApplication::primaryScreen() ? QApplication::primaryScreen()->availableGeometry() : QRect(0, 0, 1400, 900);
    const int cols = qCeil(qSqrt(static_cast<double>(windows.size())));
    const int rows = qCeil(static_cast<double>(windows.size()) / static_cast<double>(cols));
    const int cellW = qMax(320, screen.width() / qMax(1, cols));
    const int cellH = qMax(240, screen.height() / qMax(1, rows));

    for (int i = 0; i < windows.size(); ++i) {
        const int row = i / cols;
        const int col = i % cols;
        windows.at(i)->setGeometry(screen.x() + col * cellW, screen.y() + row * cellH, cellW, cellH);
    }
    statusBar()->showMessage(tr("Windows tiled."), 3000);
}

void MainWindow::showCommandLine()
{
    if (!m_commandDock) {
        m_commandDock = new QDockWidget(tr("Command Line"), this);
        m_commandDock->setObjectName(QStringLiteral("CommandLineDock"));
        m_commandDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
        m_commandDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
        m_commandDock->setMinimumHeight(58);
        m_commandDock->setMaximumHeight(110);
        auto* panel = new QWidget(m_commandDock);
        panel->setMinimumHeight(54);
        panel->setMaximumHeight(96);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(2);

        m_commandHistoryText = new QPlainTextEdit(panel);
        m_commandHistoryText->setReadOnly(true);
        m_commandHistoryText->setMaximumBlockCount(250);
        m_commandHistoryText->setPlaceholderText(tr("Command events appear here."));
        m_commandHistoryText->setMinimumHeight(26);
        m_commandHistoryText->setMaximumHeight(52);

        auto* inputRow = new QWidget(panel);
        auto* inputLayout = new QHBoxLayout(inputRow);
        inputLayout->setContentsMargins(0, 0, 0, 0);
        auto* prompt = new QLabel(tr("Command:"), inputRow);
        m_commandLineEdit = new QLineEdit(inputRow);
        m_commandLineEdit->setMinimumHeight(22);
        m_commandLineEdit->setMaximumHeight(26);
        m_commandLineEdit->setPlaceholderText(m_view ? m_view->currentDrawPrompt() : tr("LINE, PLINE, CIRCLE, RECTANGLE, @100<45, @50,0, 100..."));
        inputLayout->addWidget(prompt);
        inputLayout->addWidget(m_commandLineEdit, 1);

        layout->addWidget(m_commandHistoryText, 1);
        layout->addWidget(inputRow);
        panel->setLayout(layout);
        m_commandDock->setWidget(panel);
        addDockWidget(Qt::BottomDockWidgetArea, m_commandDock);
        connect(m_commandLineEdit, &QLineEdit::returnPressed, this, &MainWindow::executeCommandLine);
        m_commandLineEdit->installEventFilter(this);
    }
    m_commandDock->show();
    m_commandDock->raise();
    optimizeDrawingWorkspaceLayout();
    if (m_commandLineEdit) m_commandLineEdit->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::optimizeDrawingWorkspaceLayout()
{
    if (m_view) {
        m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_view->setMinimumSize(480, 320);
    }

    if (m_layersDock) {
        m_layersDock->setMinimumWidth(150);
        m_layersDock->setMaximumWidth(220);
    }
    if (m_propertiesDock) {
        m_propertiesDock->setMinimumWidth(150);
        m_propertiesDock->setMaximumWidth(220);
    }
    if (m_commandDock) {
        m_commandDock->setMinimumHeight(58);
        m_commandDock->setMaximumHeight(110);
    }

    if (m_layersDock && m_propertiesDock) {
        const Qt::DockWidgetArea layersArea = dockWidgetArea(m_layersDock);
        const Qt::DockWidgetArea propertiesArea = dockWidgetArea(m_propertiesDock);
        if (layersArea != Qt::RightDockWidgetArea) {
            removeDockWidget(m_layersDock);
            addDockWidget(Qt::RightDockWidgetArea, m_layersDock);
        }
        if (propertiesArea != Qt::RightDockWidgetArea) {
            removeDockWidget(m_propertiesDock);
            addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);
        }
        tabifyDockWidget(m_layersDock, m_propertiesDock);
    }

    QList<QDockWidget*> horizontalDocks;
    QList<int> horizontalSizes;
    if (m_layersDock && m_layersDock->isVisible()) {
        horizontalDocks << m_layersDock;
        horizontalSizes << 185;
    } else if (m_propertiesDock && m_propertiesDock->isVisible()) {
        horizontalDocks << m_propertiesDock;
        horizontalSizes << 185;
    }
    if (!horizontalDocks.isEmpty()) {
        resizeDocks(horizontalDocks, horizontalSizes, Qt::Horizontal);
    }
    if (m_commandDock && m_commandDock->isVisible()) {
        resizeDocks(QList<QDockWidget*>() << m_commandDock, QList<int>() << 72, Qt::Vertical);
    }
}

void MainWindow::appendCommandConsoleMessage(const QString& message)
{
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
                                  message);
    if (m_commandHistoryText) {
        m_commandHistoryText->appendPlainText(line);
        QScrollBar* bar = m_commandHistoryText->verticalScrollBar();
        if (bar) bar->setValue(bar->maximum());
    }
    DebugLogger::logDebug(QStringLiteral("DRAW-CONSOLE: %1").arg(message));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_commandLineEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            if (m_commandLineEdit) m_commandLineEdit->clear();
            if (m_view) {
                m_view->cancelActiveAction();
                if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
            }
            appendCommandConsoleMessage(tr("ESC: command line cleared and current action canceled."));
            statusBar()->showMessage(tr("ESC: action canceled."), 2500);
            event->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::executeCommandLine()
{
    if (!m_commandLineEdit) return;
    const QString command = m_commandLineEdit->text().trimmed();
    if (command.isEmpty()) {
        if (m_view) {
            if (m_view->interactiveModifyCommandActive()) {
                m_view->commitInteractiveModifyStage();
            } else if (m_view->drawingTool() == GraphicsView::DrawingTool::Noe) {
                m_view->repeatLastDrawCommand();
            } else {
                m_view->processDrawConsoleCommand(QStringLiteral("END"));
            }
            if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
        }
        return;
    }
    m_commandLineEdit->clear();
    if (m_view && m_view->processDrawConsoleCommand(command)) {
        if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
        return;
    }
    executeCadCommand(command);
    if (m_commandLineEdit && m_view) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
}


void MainWindow::toggleQtCadScene(bool checked)
{
    Q_UNUSED(checked);
    if (m_view) {
        m_view->show();
        setCentralWidget(m_view);
        refreshCadScene();
    }
    statusBar()->showMessage(tr("Qt 2D scene active. OpenCascade engine removed from this version."), 3000);
}

void MainWindow::refreshCadScene()
{
    if (!m_view)
        return;
    m_view->setDocument(&m_document);
    m_view->refreshFromDocumentPreservingSelection();
    m_view->updateSceneRectToContent();
    m_view->viewport()->update();
}


void MainWindow::showTextStyleDialog()
{
    if (!m_view) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Text Style"));
    dialog.resize(620, 430);

    auto* root = new QHBoxLayout(&dialog);

    auto* left = new QVBoxLayout();
    auto* currentLabel = new QLabel(tr("Current text style:  %1").arg(m_currentTextStyleName), &dialog);
    auto* stylesLabel = new QLabel(tr("Styles:"), &dialog);
    auto* styleList = new QListWidget(&dialog);
    for (const QString& name : m_textStyleNames) {
        if (!name.trimmed().isEmpty()) styleList->addItem(name.trimmed());
    }
    QList<QListWidgetItem*> currentItems = styleList->findItems(m_currentTextStyleName, Qt::MatchExactly);
    if (!currentItems.isEmpty()) styleList->setCurrentItem(currentItems.first());
    else if (styleList->count() > 0) styleList->setCurrentRow(0);

    auto* styleFilter = new QComboBox(&dialog);
    styleFilter->addItems({tr("All styles"), tr("Used styles")});

    auto* sample = new QLabel(QStringLiteral("AaBb123"), &dialog);
    sample->setMinimumHeight(90);
    sample->setFrameShape(QFrame::Box);
    sample->setAlignment(Qt::AlignCenter);
    sample->setStyleSheet(QStringLiteral("background:white;color:black;"));

    left->addWidget(currentLabel);
    left->addWidget(stylesLabel);
    left->addWidget(styleList, 1);
    left->addWidget(styleFilter);
    left->addWidget(sample);
    root->addLayout(left, 1);

    auto* right = new QVBoxLayout();

    auto* fontGroup = new QGroupBox(tr("Font"), &dialog);
    auto* fontLayout = new QGridLayout(fontGroup);
    auto* fontCombo = new QFontComboBox(fontGroup);
    fontCombo->setCurrentFont(QFont(m_currentTextFontName));
    auto* fontStyleCombo = new QComboBox(fontGroup);
    fontStyleCombo->addItems({QStringLiteral("Regular"), QStringLiteral("Bold"), QStringLiteral("Italic"), QStringLiteral("Bold Italic")});
    int styleIndex = fontStyleCombo->findText(m_currentTextFontStyle);
    if (styleIndex >= 0) fontStyleCombo->setCurrentIndex(styleIndex);
    auto* bigFontCheck = new QCheckBox(tr("Use Big Font"), fontGroup);
    bigFontCheck->setEnabled(false);
    fontLayout->addWidget(new QLabel(tr("Font Name:"), fontGroup), 0, 0);
    fontLayout->addWidget(fontCombo, 1, 0);
    fontLayout->addWidget(new QLabel(tr("Font Style:"), fontGroup), 0, 1);
    fontLayout->addWidget(fontStyleCombo, 1, 1);
    fontLayout->addWidget(bigFontCheck, 2, 0, 1, 2);

    auto* sizeGroup = new QGroupBox(tr("Size"), &dialog);
    auto* sizeLayout = new QGridLayout(sizeGroup);
    auto* annotativeCheck = new QCheckBox(tr("Annotative"), sizeGroup);
    annotativeCheck->setChecked(m_currentTextAnnotative);
    auto* matchOrientationCheck = new QCheckBox(tr("Match text orientation to layout"), sizeGroup);
    matchOrientationCheck->setEnabled(false);
    auto* heightSpin = new QDoubleSpinBox(sizeGroup);
    heightSpin->setRange(0.01, 1000000.0);
    heightSpin->setDecimals(3);
    heightSpin->setValue(m_view->annotationTextHeight());
    sizeLayout->addWidget(annotativeCheck, 0, 0);
    sizeLayout->addWidget(matchOrientationCheck, 1, 0);
    sizeLayout->addWidget(new QLabel(tr("Paper Text Height"), sizeGroup), 0, 1);
    sizeLayout->addWidget(heightSpin, 1, 1);

    auto* effectsGroup = new QGroupBox(tr("Effects"), &dialog);
    auto* effectsLayout = new QGridLayout(effectsGroup);
    auto* upsideDownCheck = new QCheckBox(tr("Upside down"), effectsGroup);
    auto* backwardsCheck = new QCheckBox(tr("Backwards"), effectsGroup);
    auto* verticalCheck = new QCheckBox(tr("Vertical"), effectsGroup);
    verticalCheck->setEnabled(false);
    auto* widthSpin = new QDoubleSpinBox(effectsGroup);
    widthSpin->setRange(0.01, 100.0);
    widthSpin->setDecimals(4);
    widthSpin->setValue(m_currentTextWidthFactor);
    auto* obliqueSpin = new QDoubleSpinBox(effectsGroup);
    obliqueSpin->setRange(-85.0, 85.0);
    obliqueSpin->setDecimals(3);
    obliqueSpin->setValue(m_currentTextObliqueAngle);
    effectsLayout->addWidget(upsideDownCheck, 0, 0);
    effectsLayout->addWidget(new QLabel(tr("Width Factor:"), effectsGroup), 0, 1);
    effectsLayout->addWidget(widthSpin, 1, 1);
    effectsLayout->addWidget(backwardsCheck, 1, 0);
    effectsLayout->addWidget(new QLabel(tr("Oblique Angle:"), effectsGroup), 2, 1);
    effectsLayout->addWidget(obliqueSpin, 3, 1);
    effectsLayout->addWidget(verticalCheck, 2, 0);

    auto* buttonColumn = new QVBoxLayout();
    auto* setCurrentButton = new QPushButton(tr("Set Current"), &dialog);
    auto* newButton = new QPushButton(tr("New"), &dialog);
    auto* deleteButton = new QPushButton(tr("Delete"), &dialog);
    buttonColumn->addWidget(setCurrentButton);
    buttonColumn->addWidget(newButton);
    buttonColumn->addWidget(deleteButton);
    buttonColumn->addStretch(1);

    auto* rightTop = new QHBoxLayout();
    auto* groups = new QVBoxLayout();
    groups->addWidget(fontGroup);
    groups->addWidget(sizeGroup);
    groups->addWidget(effectsGroup);
    rightTop->addLayout(groups, 1);
    rightTop->addLayout(buttonColumn);
    right->addLayout(rightTop, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch(1);
    auto* applyButton = new QPushButton(tr("Apply"), &dialog);
    auto* closeButton = new QPushButton(tr("Close"), &dialog);
    auto* helpButton = new QPushButton(tr("Help"), &dialog);
    bottom->addWidget(applyButton);
    bottom->addWidget(closeButton);
    bottom->addWidget(helpButton);
    right->addLayout(bottom);
    root->addLayout(right, 2);

    auto updateSample = [&]() {
        QFont f(fontCombo->currentFont().family());
        f.setPointSize(24);
        const QString fs = fontStyleCombo->currentText();
        f.setBold(fs.contains(QStringLiteral("Bold")));
        f.setItalic(fs.contains(QStringLiteral("Italic")));
        sample->setFont(f);
    };
    updateSample();
    connect(fontCombo, &QFontComboBox::currentFontChanged, &dialog, [=, &updateSample](const QFont&) { updateSample(); });
    connect(fontStyleCombo, &QComboBox::currentTextChanged, &dialog, [=, &updateSample](const QString&) { updateSample(); });

    auto applyStyle = [&]() {
        if (QListWidgetItem* item = styleList->currentItem()) {
            m_currentTextStyleName = item->text().trimmed();
        }
        if (m_currentTextStyleName.isEmpty()) m_currentTextStyleName = QStringLiteral("STANDARD");
        m_currentTextFontName = fontCombo->currentFont().family();
        m_currentTextFontStyle = fontStyleCombo->currentText();
        m_currentTextAnnotative = annotativeCheck->isChecked();
        m_currentTextWidthFactor = widthSpin->value();
        m_currentTextObliqueAngle = obliqueSpin->value();
        m_view->setAnnotationTextHeight(heightSpin->value());
        m_view->setAnnotationTextWidthFactor(m_currentTextWidthFactor);
        m_view->setAnnotationTextObliqueAngle(m_currentTextObliqueAngle);
        m_view->setAnnotationFontName(m_currentTextFontName);
        currentLabel->setText(tr("Current text style:  %1").arg(m_currentTextStyleName));
        statusBar()->showMessage(tr("Current STYLE: %1, height=%2, width=%3, oblique=%4.")
                                     .arg(m_currentTextStyleName)
                                     .arg(heightSpin->value())
                                     .arg(m_currentTextWidthFactor)
                                     .arg(m_currentTextObliqueAngle), 5000);
    };

    connect(setCurrentButton, &QPushButton::clicked, &dialog, applyStyle);
    connect(applyButton, &QPushButton::clicked, &dialog, applyStyle);
    connect(closeButton, &QPushButton::clicked, &dialog, [&]() { applyStyle(); dialog.accept(); });
    connect(helpButton, &QPushButton::clicked, &dialog, [&]() {
        QMessageBox::information(&dialog, tr("Text Style"),
            tr("Format > Text Style controls the style used by TEXT/MTEXT: font, height, width factor, and oblique angle. New text uses these settings."));
    });
    connect(newButton, &QPushButton::clicked, &dialog, [&]() {
        bool ok = false;
        const QString name = QInputDialog::getText(&dialog, tr("New Text Style"), tr("Nom du style:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (styleList->findItems(name, Qt::MatchExactly).isEmpty()) {
            m_textStyleNames.append(name);
            styleList->addItem(name);
        }
        const QList<QListWidgetItem*> items = styleList->findItems(name, Qt::MatchExactly);
        if (!items.isEmpty()) styleList->setCurrentItem(items.first());
    });
    connect(deleteButton, &QPushButton::clicked, &dialog, [&]() {
        QListWidgetItem* item = styleList->currentItem();
        if (!item) return;
        const QString name = item->text();
        if (name == QStringLiteral("STANDARD")) {
            QMessageBox::information(&dialog, tr("Text Style"), tr("The STANDARD style cannot be deleted."));
            return;
        }
        m_textStyleNames.removeAll(name);
        delete styleList->takeItem(styleList->row(item));
        if (styleList->count() > 0) styleList->setCurrentRow(0);
    });

    dialog.exec();
}

void MainWindow::showHatchStyleDialog(bool editSelectedHatches, bool startHatchAfterOk)
{
    if (!m_view) return;

    QDialog dialog(this);
    dialog.setWindowTitle(editSelectedHatches ? tr("Hatch Edit") : tr("Hatch and Gradient"));
    dialog.resize(560, 520);

    auto* root = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);
    auto* hatchTab = new QWidget(tabs);
    auto* hatchLayout = new QHBoxLayout(hatchTab);

    auto* left = new QVBoxLayout();
    auto* typeGroup = new QGroupBox(tr("Type and pattern"), hatchTab);
    auto* typeLayout = new QGridLayout(typeGroup);
    auto* typeCombo = new QComboBox(typeGroup);
    typeCombo->addItems({tr("Predefined"), tr("User defined")});
    auto* patternCombo = new QComboBox(typeGroup);
    patternCombo->setEditable(true);
    patternCombo->addItems({
        QStringLiteral("ANSI31"), QStringLiteral("ANSI32"), QStringLiteral("ANSI33"), QStringLiteral("ANSI34"),
        QStringLiteral("ANSI35"), QStringLiteral("ANSI36"), QStringLiteral("ANSI37"), QStringLiteral("ANSI38"),
        QStringLiteral("HORIZONTAL"), QStringLiteral("VERTICAL"), QStringLiteral("CROSS"), QStringLiteral("CROSSHATCH"),
        QStringLiteral("DIAGONAL_CROSS"), QStringLiteral("GRID"), QStringLiteral("NET"), QStringLiteral("DENSE"),
        QStringLiteral("SOLID"), QStringLiteral("SOLID_10"), QStringLiteral("SOLID_20"), QStringLiteral("SOLID_25"),
        QStringLiteral("SOLID_30"), QStringLiteral("SOLID_40"), QStringLiteral("SOLID_50"), QStringLiteral("SOLID_60"),
        QStringLiteral("SOLID_75"), QStringLiteral("SOLID_90"), QStringLiteral("SOLID_LIGHT"), QStringLiteral("SOLID_MEDIUM"),
        QStringLiteral("SOLID_DARK"), QStringLiteral("SOLID_TRANSPARENT"),
        QStringLiteral("CONCRETE"), QStringLiteral("BETON"), QStringLiteral("AR_CONC"), QStringLiteral("WOOD"),
        QStringLiteral("BOIS"), QStringLiteral("AR_WOOD"), QStringLiteral("SAND"), QStringLiteral("SABLE"),
        QStringLiteral("GRAVEL"), QStringLiteral("GRAVIER"), QStringLiteral("BRICK"), QStringLiteral("BRIQUE"),
        QStringLiteral("MASONRY"), QStringLiteral("TILE"), QStringLiteral("CARRELAGE"), QStringLiteral("EARTH"),
        QStringLiteral("TERRE"), QStringLiteral("GROUND"), QStringLiteral("INSULATION"), QStringLiteral("ISOLATION"),
        QStringLiteral("GLASS"), QStringLiteral("VERRE"), QStringLiteral("WATER"), QStringLiteral("EAU"),
        QStringLiteral("GRASS"), QStringLiteral("HERBE"), QStringLiteral("STEEL"), QStringLiteral("METAL")
    });
    patternCombo->setMaxVisibleItems(24);
    patternCombo->setToolTip(tr("Native hatch pattern. Examples: SOLID_50, CONCRETE/BETON, WOOD/BOIS, SAND/SABLE, BRICK/BRIQUE, INSULATION/ISOLATION."));
    int pidx = patternCombo->findText(m_view->hatchPattern(), Qt::MatchFixedString);
    if (pidx >= 0) patternCombo->setCurrentIndex(pidx);
    else patternCombo->setEditText(m_view->hatchPattern());
    auto* useCurrentColorCheck = new QCheckBox(tr("Use Current"), typeGroup);
    useCurrentColorCheck->setChecked(true);
    auto* swatch = new QLabel(typeGroup);
    swatch->setMinimumHeight(44);
    swatch->setFrameShape(QFrame::Box);
    swatch->setAlignment(Qt::AlignCenter);
    swatch->setText(patternCombo->currentText());
    swatch->setStyleSheet(QStringLiteral("background:white;color:black;"));
    typeLayout->addWidget(new QLabel(tr("Type:"), typeGroup), 0, 0);
    typeLayout->addWidget(typeCombo, 0, 1);
    typeLayout->addWidget(new QLabel(tr("Pattern:"), typeGroup), 1, 0);
    typeLayout->addWidget(patternCombo, 1, 1);
    typeLayout->addWidget(new QLabel(tr("Color:"), typeGroup), 2, 0);
    typeLayout->addWidget(useCurrentColorCheck, 2, 1);
    typeLayout->addWidget(new QLabel(tr("Swatch:"), typeGroup), 3, 0);
    typeLayout->addWidget(swatch, 3, 1);

    auto* angleGroup = new QGroupBox(tr("Angle and scale"), hatchTab);
    auto* angleLayout = new QGridLayout(angleGroup);
    auto* angleSpin = new QDoubleSpinBox(angleGroup);
    angleSpin->setRange(-360000.0, 360000.0);
    angleSpin->setDecimals(3);
    angleSpin->setValue(m_view->hatchAngleDeg());
    auto* scaleSpin = new QDoubleSpinBox(angleGroup);
    scaleSpin->setRange(0.01, 1000000.0);
    scaleSpin->setDecimals(4);
    scaleSpin->setValue(m_view->hatchScale());
    auto* doubleCheck = new QCheckBox(tr("Double"), angleGroup);
    doubleCheck->setEnabled(false);
    auto* relativeCheck = new QCheckBox(tr("Relative to paper space"), angleGroup);
    relativeCheck->setEnabled(false);
    angleLayout->addWidget(new QLabel(tr("Angle:"), angleGroup), 0, 0);
    angleLayout->addWidget(angleSpin, 0, 1);
    angleLayout->addWidget(new QLabel(tr("Scale:"), angleGroup), 1, 0);
    angleLayout->addWidget(scaleSpin, 1, 1);
    angleLayout->addWidget(doubleCheck, 2, 0, 1, 2);
    angleLayout->addWidget(relativeCheck, 3, 0, 1, 2);

    auto* originGroup = new QGroupBox(tr("Hatch origin"), hatchTab);
    auto* originLayout = new QVBoxLayout(originGroup);
    auto* useCurrentOrigin = new QRadioButton(tr("Use current origin"), originGroup);
    useCurrentOrigin->setChecked(true);
    auto* specifiedOrigin = new QRadioButton(tr("Specified origin"), originGroup);
    specifiedOrigin->setEnabled(false);
    originLayout->addWidget(useCurrentOrigin);
    originLayout->addWidget(specifiedOrigin);

    left->addWidget(typeGroup);
    left->addWidget(angleGroup);
    left->addWidget(originGroup);
    left->addStretch(1);

    auto* right = new QVBoxLayout();
    auto* boundariesGroup = new QGroupBox(tr("Boundaries"), hatchTab);
    auto* boundariesLayout = new QVBoxLayout(boundariesGroup);
    auto* pickPointsButton = new QPushButton(tr("Add: Pick points"), boundariesGroup);
    auto* selectObjectsButton = new QPushButton(tr("Add: Select objects"), boundariesGroup);
    auto* removeButton = new QPushButton(tr("Remove boundaries"), boundariesGroup);
    removeButton->setEnabled(false);
    auto* recreateButton = new QPushButton(tr("Recreate boundary"), boundariesGroup);
    recreateButton->setEnabled(false);
    auto* viewButton = new QPushButton(tr("View Selections"), boundariesGroup);
    viewButton->setEnabled(false);
    boundariesLayout->addWidget(pickPointsButton);
    boundariesLayout->addWidget(selectObjectsButton);
    boundariesLayout->addWidget(removeButton);
    boundariesLayout->addWidget(recreateButton);
    boundariesLayout->addWidget(viewButton);

    auto* optionsGroup = new QGroupBox(tr("Options"), hatchTab);
    auto* optionsLayout = new QGridLayout(optionsGroup);
    auto* annotative = new QCheckBox(tr("Annotative"), optionsGroup);
    auto* associative = new QCheckBox(tr("Associative"), optionsGroup);
    auto* separate = new QCheckBox(tr("Create separate hatches"), optionsGroup);
    separate->setChecked(true);
    auto* drawOrder = new QComboBox(optionsGroup);
    drawOrder->addItems({tr("Send Behind Boundary"), tr("Bring in Front")});
    auto* layerCombo = new QComboBox(optionsGroup);
    layerCombo->addItem(tr("Use Current"));
    auto* transparency = new QComboBox(optionsGroup);
    transparency->addItem(tr("Use Current"));
    optionsLayout->addWidget(annotative, 0, 0, 1, 2);
    optionsLayout->addWidget(associative, 1, 0, 1, 2);
    optionsLayout->addWidget(separate, 2, 0, 1, 2);
    optionsLayout->addWidget(new QLabel(tr("Draw order:"), optionsGroup), 3, 0);
    optionsLayout->addWidget(drawOrder, 3, 1);
    optionsLayout->addWidget(new QLabel(tr("Layer:"), optionsGroup), 4, 0);
    optionsLayout->addWidget(layerCombo, 4, 1);
    optionsLayout->addWidget(new QLabel(tr("Transparency:"), optionsGroup), 5, 0);
    optionsLayout->addWidget(transparency, 5, 1);

    right->addWidget(boundariesGroup);
    right->addWidget(optionsGroup);
    right->addStretch(1);

    hatchLayout->addLayout(left, 2);
    hatchLayout->addLayout(right, 1);
    tabs->addTab(hatchTab, tr("Hatch"));

    auto* gradientTab = new QWidget(tabs);
    auto* gradientLayout = new QVBoxLayout(gradientTab);
    gradientLayout->addWidget(new QLabel(tr("Gradient is reserved for a future step. Hatch settings remain native DXF."), gradientTab));
    gradientLayout->addStretch(1);
    tabs->addTab(gradientTab, tr("Gradient"));
    root->addWidget(tabs, 1);

    auto* bottom = new QHBoxLayout();
    auto* previewButton = new QPushButton(tr("Preview"), &dialog);
    auto* okButton = new QPushButton(tr("OK"), &dialog);
    auto* cancelButton = new QPushButton(tr("Cancel"), &dialog);
    auto* helpButton = new QPushButton(tr("Help"), &dialog);
    bottom->addWidget(previewButton);
    bottom->addStretch(1);
    bottom->addWidget(okButton);
    bottom->addWidget(cancelButton);
    bottom->addWidget(helpButton);
    root->addLayout(bottom);

    auto updateSwatch = [&]() {
        QString name = patternCombo->currentText().trimmed().isEmpty() ? QStringLiteral("ANSI31") : patternCombo->currentText().trimmed().toUpper();
        name.replace('-', '_');
        name.replace(' ', '_');
        swatch->setText(name + QStringLiteral("\n") + tr("native preview in drawing area"));
    };
    connect(patternCombo, &QComboBox::currentTextChanged, &dialog, [=, &updateSwatch](const QString&) { updateSwatch(); });
    updateSwatch();

    auto applyHatchStyle = [&]() {
        QString pattern = patternCombo->currentText().trimmed().isEmpty() ? QStringLiteral("ANSI31") : patternCombo->currentText().trimmed().toUpper();
        pattern.replace('-', '_');
        pattern.replace(' ', '_');
        m_view->setHatchPattern(pattern);
        m_view->setHatchScale(scaleSpin->value());
        m_view->setHatchAngleDeg(angleSpin->value());
        statusBar()->showMessage(tr("HATCHSTYLE: pattern=%1, scale=%2, angle=%3.")
                                     .arg(pattern).arg(scaleSpin->value()).arg(angleSpin->value()), 5000);
    };

    auto editSelected = [&]() {
        applyHatchStyle();
        pushUndoState(tr("HATCHEDIT"));
        if (!m_view->editSelectedHatches(m_view->hatchPattern(), m_view->hatchScale(), m_view->hatchAngleDeg())) {
            rollbackUndoIfNeeded();
            QMessageBox::information(&dialog, tr("Hatch Edit"), tr("Select at least one hatch."));
            return false;
        }
        return true;
    };

    connect(previewButton, &QPushButton::clicked, &dialog, applyHatchStyle);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, &dialog, [&]() {
        if (editSelectedHatches) {
            if (!editSelected()) return;
        } else {
            applyHatchStyle();
            if (startHatchAfterOk) {
                m_view->setDrawingTool(GraphicsView::DrawingTool::Hatch);
                statusBar()->showMessage(tr("HATCH: click boundary points, C to close, Enter to create."), 6000);
            }
        }
        dialog.accept();
    });
    connect(pickPointsButton, &QPushButton::clicked, &dialog, [&]() {
        applyHatchStyle();
        m_view->setDrawingTool(GraphicsView::DrawingTool::Hatch);
        statusBar()->showMessage(tr("HATCH: click boundary points, C to close, Enter to create."), 6000);
        dialog.accept();
    });
    connect(selectObjectsButton, &QPushButton::clicked, &dialog, [&]() {
        applyHatchStyle();
        // AutoCAD-like Select objects workflow:
        // close the floating dialog, return to the drawing, select closed boundaries, then Enter creates the hatch.
        if (m_view) {
            m_view->startInteractiveModifyCommand(QStringLiteral("HATCHSELECT"));
            m_view->setFocus(Qt::ShortcutFocusReason);
        }
        statusBar()->showMessage(tr("HATCH Select objects: select circle/rectangle/closed polyline or lines forming a closed boundary, then press Enter."), 7000);
        dialog.accept();
    });
    connect(helpButton, &QPushButton::clicked, &dialog, [&]() {
        QMessageBox::information(&dialog, tr("Create hatch"),
            tr("Method 1: HATCH > Add: Pick points, click boundary points, then press Enter.\n"
               "Method 2: HATCH > Add: Select objects, the dialog closes; select closed boundaries in the drawing, then press Enter.\n"
               "Accepted boundaries: circle, rectangle, ellipse, polygon, closed polyline, or connected lines forming a closed boundary.\n"
               "HATCHSTYLE only sets the current style; HATCHEDIT modifies selected hatches."));
    });

    dialog.exec();
}

void MainWindow::executeCadCommand(const QString& command)
{
    const QString cmd = command.trimmed().toUpper();

    // Appel utilisateur: les commandes de Modify passent d'abord par le mode
    // sélection séquentielle. L'opération réelle n'est lancée qu'après Entrée.
    // Quand cette fonction est rappelée par interactiveModifyCommandReady,
    // m_executingValidatedModifyCommand est vrai et on exécute réellement.
    if (!m_executingValidatedModifyCommand && m_view && isInteractiveModifySelectionCommand(cmd)) {
        const QString interactiveCommand = canonicalInteractiveModifyCommand(cmd);
        appendCommandConsoleMessage(interactiveModifyStartMessage(interactiveCommand));
        m_view->startInteractiveModifyCommand(interactiveCommand);
        if (m_commandLineEdit) m_commandLineEdit->setPlaceholderText(m_view->currentDrawPrompt());
        m_view->setFocus(Qt::ShortcutFocusReason);
        statusBar()->showMessage(interactiveModifyStartMessage(interactiveCommand), 5000);
        return;
    }

    auto fmtPt = [](const QPointF& p) -> QString {
        return QStringLiteral("%1,%2").arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4);
    };

    auto entityTypeName = [](const CadEntity* e) -> QString {
        if (!e) return QStringLiteral("NULL");
        switch (e->type()) {
        case CadEntity::Type::Line: return QStringLiteral("LINE");
        case CadEntity::Type::Circle: return QStringLiteral("CIRCLE");
        case CadEntity::Type::Rectangle: return QStringLiteral("RECTANGLE");
        case CadEntity::Type::Polyline: return QStringLiteral("POLYLINE");
        case CadEntity::Type::Arc: return QStringLiteral("ARC");
        case CadEntity::Type::Ellipse: return QStringLiteral("ELLIPSE");
        case CadEntity::Type::Polygon: return QStringLiteral("POLYGON");
        case CadEntity::Type::Text: return QStringLiteral("TEXT");
        case CadEntity::Type::LinearDimension: return QStringLiteral("DIMENSION");
        case CadEntity::Type::Leader: return QStringLiteral("LEADER");
        case CadEntity::Type::Hatch: return QStringLiteral("HATCH");
        case CadEntity::Type::BlockReference: return QStringLiteral("INSERT/BLOCK");
        }
        return QStringLiteral("ENTITY");
    };

    auto entityBounds = [](const CadEntity* e) -> QRectF {
        if (!e) return QRectF();
        if (auto* l = dynamic_cast<const CadLine*>(e)) return QRectF(l->start(), l->end()).normalized();
        if (auto* c = dynamic_cast<const CadCircle*>(e)) { const double r = c->radius(); return QRectF(c->center().x()-r, c->center().y()-r, 2*r, 2*r); }
        if (auto* r = dynamic_cast<const CadRectangle*>(e)) return r->rect().normalized();
        if (auto* pl = dynamic_cast<const CadPolyline*>(e)) {
            if (pl->points().isEmpty()) return QRectF();
            QRectF b(pl->points().first(), QSizeF(0,0));
            for (const QPointF& p : pl->points()) b = b.united(QRectF(p, QSizeF(0,0)));
            return b.normalized();
        }
        if (auto* a = dynamic_cast<const CadArc*>(e)) { const double r = a->radius(); return QRectF(a->center().x()-r, a->center().y()-r, 2*r, 2*r); }
        if (auto* el = dynamic_cast<const CadEllipse*>(e)) return el->rect().normalized();
        if (auto* pg = dynamic_cast<const CadPolygon*>(e)) { const double r = pg->radius(); return QRectF(pg->center().x()-r, pg->center().y()-r, 2*r, 2*r); }
        if (auto* tx = dynamic_cast<const CadText*>(e)) return QRectF(tx->position(), QSizeF(tx->height() * tx->text().size() * 0.65, tx->height())).normalized();
        if (auto* d = dynamic_cast<const CadLinearDimension*>(e)) return QRectF(d->first(), d->second()).normalized().united(QRectF(d->dimensionPoint(), QSizeF(0,0)));
        if (auto* leader = dynamic_cast<const CadLeader*>(e)) return QRectF(leader->arrowPoint(), leader->textPoint()).normalized();
        if (auto* block = dynamic_cast<const CadBlockReference*>(e)) return QRectF(block->insertionPoint(), QSizeF(0,0));
        return QRectF();
    };

    auto polyArea = [](const QVector<QPointF>& pts, bool closed) -> double {
        if (pts.size() < 3 || !closed) return 0.0;
        double a = 0.0;
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF p1 = pts.at(i);
            const QPointF p2 = pts.at((i + 1) % pts.size());
            a += p1.x() * p2.y() - p2.x() * p1.y();
        }
        return std::abs(a) * 0.5;
    };

    auto routeDrawCommand = [&]() -> bool {
        if (!m_view) return false;
        static const QStringList drawLike = {
            QStringLiteral("RAY"), QStringLiteral("XLINE"), QStringLiteral("DONUT"), QStringLiteral("GRADIENT"),
            QStringLiteral("BOUNDARY"), QStringLiteral("REGION"), QStringLiteral("TABLE"), QStringLiteral("POINT"),
            QStringLiteral("MTEXT"), QStringLiteral("LINE"), QStringLiteral("L"), QStringLiteral("PLINE"), QStringLiteral("PL"),
            QStringLiteral("SPLINE"), QStringLiteral("CIRCLE"), QStringLiteral("ARC"), QStringLiteral("RECTANGLE"),
            QStringLiteral("RECTANG"), QStringLiteral("ELLIPSE"), QStringLiteral("POLYGON"), QStringLiteral("TEXT"),
            QStringLiteral("HATCH"), QStringLiteral("BHATCH"), QStringLiteral("LEADER"), QStringLiteral("MLEADER")
        };
        const bool modifyDimensionCommand =
            cmd == QStringLiteral("DIMCENTER") ||
            cmd == QStringLiteral("DIMDIAMETER") ||
            cmd == QStringLiteral("DIMRADIUS");
        if (drawLike.contains(cmd) || (cmd.startsWith(QStringLiteral("DIM")) && !modifyDimensionCommand)) {
            if (cmd == QStringLiteral("DONUT")) {
                appendCommandConsoleMessage(tr("DONUT: DWGView proxy -> use CIRCLE then OFFSET/weight; command converted to CIRCLE."));
                return m_view->processDrawConsoleCommand(QStringLiteral("CIRCLE"));
            }
            if (cmd == QStringLiteral("GRADIENT") || cmd == QStringLiteral("BOUNDARY") || cmd == QStringLiteral("REGION")) {
                appendCommandConsoleMessage(tr("%1: DWGView proxy -> closed HATCH boundary.").arg(cmd));
                return m_view->processDrawConsoleCommand(QStringLiteral("HATCH"));
            }
            if (cmd == QStringLiteral("TABLE")) {
                m_view->setAnnotationText(QStringLiteral("TABLE"));
                appendCommandConsoleMessage(tr("TABLE: DWGView proxy -> inserts a TABLE label. The real table object still needs to be added to the model."));
                return m_view->processDrawConsoleCommand(QStringLiteral("TEXT"));
            }
            return m_view->processDrawConsoleCommand(cmd);
        }
        return false;
    };

    if (routeDrawCommand()) return;
    if (cmd == "U" || cmd == "UNDO") { undoCommand(); return; }
    if (cmd == "REDO") { redoCommand(); return; }
    if (cmd == "LINE" || cmd == "L") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Line); statusBar()->showMessage(tr("Commande LINE Qt."), 3000); return; }
    if (cmd == "SPLINE" || cmd == "SPL") { m_view->setDrawingTool(GraphicsView::DrawingTool::Spline); statusBar()->showMessage(tr("SPLINE command: click points, Enter to finish."), 3000); return; }
    if (cmd == "PLINE" || cmd == "PL") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Polyline); statusBar()->showMessage(tr("Commande PLINE Qt."), 3000); return; }
    if (cmd == "CIRCLE" || cmd == "C") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Circle); statusBar()->showMessage(tr("Commande CIRCLE Qt."), 3000); return; }
    if (cmd == "ARC" || cmd == "A") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Arc3Points); statusBar()->showMessage(tr("Commande ARC Qt."), 3000); return; }
    if (cmd == "RECTANG" || cmd == "RECTANGLE" || cmd == "REC") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Rectangle); statusBar()->showMessage(tr("Commande RECTANGLE Qt."), 3000); return; }
    if (cmd == "TEXT" || cmd == "DTEXT" || cmd == "MTEXT") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Text); statusBar()->showMessage(tr("Commande TEXT Qt."), 3000); return; }
    if (cmd == "DIMLINEAR" || cmd == "DIM" || cmd == "DIMALIGNED" || cmd == "DIMROTATED" ||
        cmd == "DIMBASELINE" || cmd == "DIMCONTINUE" || cmd == "DIMARC" ||
        cmd == "DIMANGULAR" || cmd == "DIMANGULAR3P" || cmd == "DIMANGULARARC") {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::LinearDimension);
        appendCommandConsoleMessage(tr("QCAD Dimension compatibility: %1 -> CadLinearDimension DWGView.").arg(cmd));
        statusBar()->showMessage(tr("Qt Dimension command: %1 - references detected without snap, small confirmation square.").arg(cmd), 4000);
        return;
    }
    if (cmd == "DIMORDINATE" || cmd == "DIMORDINATEX" || cmd == "DIMORDINATEY" || cmd == "TOLERANCE") {
        if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Leader);
        appendCommandConsoleMessage(tr("QCAD Dimension compatibility: %1 -> LEADER/TEXT visible DWGView.").arg(cmd));
        statusBar()->showMessage(tr("Qt annotation/tolerance command: %1").arg(cmd), 3000);
        return;
    }
    if (cmd == "ELLIPSE" || cmd == "EL") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Ellipse); statusBar()->showMessage(tr("Commande ELLIPSE Qt."), 3000); return; }
    if (cmd == "POLYGON" || cmd == "POL") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Polygon); statusBar()->showMessage(tr("Commande POLYGON Qt."), 3000); return; }
    if (cmd == "LEADER" || cmd == "MLEADER") { if (m_view) m_view->setDrawingTool(GraphicsView::DrawingTool::Leader); statusBar()->showMessage(tr("Commande LEADER Qt."), 3000); return; }
    if (cmd == "HATCH" || cmd == "BHATCH") { showHatchStyleDialog(false, true); return; }
    if (cmd == "HATCHSTYLE" || cmd == "HPSTYLE") { showHatchStyleDialog(false, false); return; }
    if (cmd == "HATCHSELECT" || cmd == "HATCHFROMSELECTION") {
        if (!m_view) return;
        pushUndoState(tr("HATCH"));
        if (!m_view->createHatchFromSelectedClosedBoundaries()) {
            rollbackUndoIfNeeded();
            QMessageBox::information(this, tr("Hatch"), tr("Select a closed boundary: rectangle, circle, ellipse, polygon, or closed polyline."));
        }
        return;
    }
    auto requireCadSelectionForCommand = [this, &cmd]() -> bool {
        if (m_view && m_view->hasCadSelection()) return true;
        appendCommandConsoleMessage(tr("MODIFY_ACTION: %1 ignored, no active CAD selection.").arg(cmd));
        QMessageBox::information(this, tr("Modify"), tr("Select one or more CAD objects first."));
        return false;
    };
    // Logique Modify type AutoCAD/QCAD: si une commande Modify est lancée
    // depuis le menu sans sélection active, on n'affiche plus seulement un message.
    // On démarre un mode séquentiel: sélectionner objet(s), Entrée, puis selon
    // la commande sélectionner la cible et Entrée pour finaliser.
    static const QSet<QString> interactiveModifyCommands = {
        QStringLiteral("MATCHPROP"), QStringLiteral("TRIM"), QStringLiteral("TR"), QStringLiteral("EXTEND"), QStringLiteral("EX"),
        QStringLiteral("CHAMFER"), QStringLiteral("CHA"), QStringLiteral("FILLET"), QStringLiteral("F"),
        QStringLiteral("MOVE"), QStringLiteral("M"), QStringLiteral("TRANSLATE"), QStringLiteral("STRETCH"),
        QStringLiteral("COPY"), QStringLiteral("CO"), QStringLiteral("CP"), QStringLiteral("COPYBASE"),
        QStringLiteral("ROTATE"), QStringLiteral("RO"), QStringLiteral("ROTATE2"),
        QStringLiteral("SCALE"), QStringLiteral("SC"), QStringLiteral("LENGTHEN"),
        QStringLiteral("MIRROR"), QStringLiteral("MI"), QStringLiteral("FLIPHORIZONTAL"), QStringLiteral("FLIPVERTICAL"),
        QStringLiteral("OFFSET"), QStringLiteral("O"), QStringLiteral("ARRAY"),
        QStringLiteral("ERASE"), QStringLiteral("E"), QStringLiteral("DELETE"),
        QStringLiteral("PEDIT"), QStringLiteral("HATCHEDIT"), QStringLiteral("DIMCENTER"),
        QStringLiteral("DIMDIAMETER"), QStringLiteral("DIMRADIUS")
    };
    if (m_view && interactiveModifyCommands.contains(cmd) && !m_view->hasCadSelection()) {
        QString startCmd = cmd;
        if (cmd == QStringLiteral("TR")) startCmd = QStringLiteral("TRIM");
        else if (cmd == QStringLiteral("EX")) startCmd = QStringLiteral("EXTEND");
        else if (cmd == QStringLiteral("CHA")) startCmd = QStringLiteral("CHAMFER");
        else if (cmd == QStringLiteral("F")) startCmd = QStringLiteral("FILLET");
        else if (cmd == QStringLiteral("O")) startCmd = QStringLiteral("OFFSET");
        appendCommandConsoleMessage(tr("MODIFY_ACTION: %1 expects a scene selection. Click objects, then press Enter.").arg(startCmd));
        if (m_view->startInteractiveModifyCommand(startCmd)) return;
    }

    if (cmd == "CLOSE") { close(); return; }
    if (cmd == "PUBLISH") { exportPdf(); return; }
    if (cmd == "DWGPROPS") {
        const QRectF lim = m_document.limits();
        QMessageBox::information(this, tr("Drawing Properties"),
            tr("File: %1\nEntities: %2\nCurrent layer: %3\nUnit: %4\nLimits: %5,%6  %7 x %8")
                .arg(m_currentFile.isEmpty() ? tr("Untitled") : m_currentFile)
                .arg(m_document.entityCount())
                .arg(m_document.currentLayerName())
                .arg(m_document.unitName())
                .arg(lim.x()).arg(lim.y()).arg(lim.width()).arg(lim.height()));
        return;
    }
    if (cmd == "SELECTALL") {
        if (m_view && m_view->scene()) {
            int count = 0;
            for (QGraphicsItem* item : m_view->scene()->items()) {
                if (!item) continue;
                bool ok = false;
                const int idx = item->data(1).toInt(&ok);
                if (ok && idx >= 0 && !item->data(2).toBool()) {
                    item->setSelected(true);
                    ++count;
                }
            }
            appendCommandConsoleMessage(tr("SELECTALL: %1 objects selected.").arg(count));
            updatePropertiesPanel();
        }
        return;
    }
    if (cmd == "REDRAW") { if (m_view && m_view->viewport()) m_view->viewport()->update(); return; }
    if (cmd == "QTSCENE" || cmd == "QTREGEN") { refreshCadScene(); return; }
    if (cmd == "COLOR") { if (m_view && m_view->hasCadSelection()) changeSelectedObjectColor(); else changeSelectedLayerColor(); return; }
    if (cmd == "LINETYPE") { changeSelectedLayerLineType(); return; }
    if (cmd == "LINEWEIGHT") { changeSelectedLayerLineWeight(); return; }
    if (cmd == "LAYER") { buildDocumentLayersPanel(); if (m_layersTree) m_layersTree->setFocus(); statusBar()->showMessage(tr("Gestionnaire de calques actif."), 3000); return; }
    if (cmd == "BLOCK" || cmd == "BM" || cmd == "BLOCKMANAGER") { showBlockManager(); return; }
    if (cmd == "BA" || cmd == "BADD" || cmd == "ADDBLOCK") { createEmptyBlock(); return; }
    if (cmd == "BC" || cmd == "BMAKE" || cmd == "CREATEBLOCK") { createBlockFromSelection(); return; }
    if (cmd == "BI" || cmd == "INSERT" || cmd == "INSERTBLOCK") { insertBlock(); return; }
    if (cmd == "BN" || cmd == "BRENAME" || cmd == "RENAMEBLOCK") { renameBlock(); return; }
    if (cmd == "BY" || cmd == "BDUP" || cmd == "DUPLICATEBLOCK") { duplicateBlock(); return; }
    if (cmd == "BR" || cmd == "BREMOVE" || cmd == "REMOVEBLOCK") { removeBlock(); return; }
    if (cmd == "BP" || cmd == "BPURGE" || cmd == "PURGEBLOCKS") { purgeUnusedBlocks(); return; }
    if (cmd == "BS" || cmd == "SHOWBLOCKS") { showAllBlocks(); return; }
    if (cmd == "BH" || cmd == "HIDEBLOCKS") { hideAllBlocks(); return; }
    if (cmd == "BK" || cmd == "SELECTBLOCKREFS") { selectBlockReferences(); return; }
    if (cmd == "BX" || cmd == "DESELECTBLOCKREFS") { deselectBlockReferences(); return; }
    if (cmd == "XP" || cmd == "EXPLODEBLOCK") { explodeSelectedBlocks(); return; }
    if (cmd == "UNITS" || cmd == "LIMITS" || cmd == "OPTIONS" || cmd == "DSETTINGS") { configureDrawing(); return; }
    if (cmd == "STYLE") { showTextStyleDialog(); return; }
    if (cmd == "DIMSTYLE") { bool ok=false; const double h=QInputDialog::getDouble(this,tr("Dimension Style"),tr("Default dimension height:"),m_view?m_view->annotationTextHeight():6.0,0.01,1000000.0,3,&ok); if(ok && m_view) m_view->setAnnotationTextHeight(h); return; }
    if (cmd == "DDPTYPE") { if (m_view) { m_view->setAnnotationText(QStringLiteral("•")); m_view->setDrawingTool(GraphicsView::DrawingTool::Text); } return; }
    if (cmd == "FIELD" || cmd == "INSERTOBJ" || cmd == "TABLESTYLE" || cmd == "CUI" || cmd == "HELP" || cmd == "NEWFEATURES" || cmd == "ABOUT") {
        QMessageBox::information(this, tr("%1").arg(cmd), tr("Command %1 connected: non-destructive information/dialog. The drawing remains unchanged.").arg(cmd));
        return;
    }
    if (cmd == "FIND") {
        bool ok=false;
        const QString needle = QInputDialog::getText(this, tr("Find"), tr("Text or layer to search:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || needle.trimmed().isEmpty()) return;
        int hits = 0;
        for (int i=0; i<m_document.entityCount(); ++i) {
            const CadEntity* e = m_document.entityAt(i);
            if (!e) continue;
            bool match = e->layer().contains(needle, Qt::CaseInsensitive);
            if (auto* t = dynamic_cast<const CadText*>(e)) match = match || t->text().contains(needle, Qt::CaseInsensitive);
            if (match) ++hits;
        }
        appendCommandConsoleMessage(tr("FIND: %1 occurrence(s) pour '%2'.").arg(hits).arg(needle));
        QMessageBox::information(this, tr("Find"), tr("%1 occurrence(s) found.").arg(hits));
        return;
    }
    if (cmd == "DIST" || cmd == "DI") {
        const QVector<int> indices = m_view ? m_view->selectedEntityIndices() : QVector<int>();
        if (indices.size() == 1) {
            const CadEntity* e = m_document.entityAt(indices.first());
            double length = 0.0;
            if (auto* l = dynamic_cast<const CadLine*>(e)) length = QLineF(l->start(), l->end()).length();
            else if (auto* pl = dynamic_cast<const CadPolyline*>(e)) { const auto& pts=pl->points(); for(int i=1;i<pts.size();++i) length += QLineF(pts.at(i-1), pts.at(i)).length(); if(pl->closed() && pts.size()>2) length += QLineF(pts.last(), pts.first()).length(); }
            else if (auto* c = dynamic_cast<const CadCircle*>(e)) length = 2.0 * kCadPi * c->radius();
            else if (auto* a = dynamic_cast<const CadArc*>(e)) length = std::abs(qDegreesToRadians(a->spanAngleDeg()) * a->radius());
            appendCommandConsoleMessage(tr("DIST/LIST length: %1").arg(length, 0, 'f', 6));
            QMessageBox::information(this, tr("Distance"), tr("Length = %1").arg(length, 0, 'f', 6));
        } else {
            QMessageBox::information(this, tr("Distance"), tr("Select a line, polyline, circle, or arc to measure."));
        }
        return;
    }
    if (cmd == "AREA") {
        const QVector<int> indices = m_view ? m_view->selectedEntityIndices() : QVector<int>();
        double area = 0.0;
        for (int idx : indices) {
            const CadEntity* e = m_document.entityAt(idx);
            if (auto* r = dynamic_cast<const CadRectangle*>(e)) area += std::abs(r->rect().width()*r->rect().height());
            else if (auto* c = dynamic_cast<const CadCircle*>(e)) area += kCadPi*c->radius()*c->radius();
            else if (auto* el = dynamic_cast<const CadEllipse*>(e)) area += kCadPi*std::abs(el->rect().width()*0.5)*std::abs(el->rect().height()*0.5);
            else if (auto* pl = dynamic_cast<const CadPolyline*>(e)) area += polyArea(pl->points(), pl->closed());
            else if (auto* h = dynamic_cast<const CadHatch*>(e)) area += polyArea(h->boundary(), true);
        }
        appendCommandConsoleMessage(tr("AREA: %1").arg(area, 0, 'f', 6));
        QMessageBox::information(this, tr("Area"), tr("Selection area = %1").arg(area, 0, 'f', 6));
        return;
    }
    if (cmd == "LIST") {
        const QVector<int> indices = m_view ? m_view->selectedEntityIndices() : QVector<int>();
        QStringList rows;
        for (int idx : indices) {
            const CadEntity* e = m_document.entityAt(idx);
            const QRectF b = entityBounds(e);
            rows << tr("#%1 %2 layer=%3 bounds=(%4,%5 %6x%7)")
                        .arg(idx).arg(entityTypeName(e)).arg(e?e->layer():QString())
                        .arg(b.x(),0,'f',3).arg(b.y(),0,'f',3).arg(b.width(),0,'f',3).arg(b.height(),0,'f',3);
        }
        const QString out = rows.isEmpty() ? tr("No selection.") : rows.join(QLatin1Char('\n'));
        appendCommandConsoleMessage(QStringLiteral("LIST:\n%1").arg(out));
        QMessageBox::information(this, tr("List"), out.left(4000));
        return;
    }

    if (cmd == "COPY" || cmd == "CO" || cmd == "CP" || cmd == "COPYBASE" || cmd == "COPYCLIP" || cmd == "PASTECLIP" || cmd == "PASTEBLOCK" || cmd == "PASTEORIG") {
        if (!requireCadSelectionForCommand()) return;
        bool ok = false;
        const double dx = QInputDialog::getDouble(this, tr("Copy"), tr("Offset X:"), m_view->gridSpacing(), -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        const double dy = QInputDialog::getDouble(this, tr("Copy"), tr("Offset Y:"), m_view->gridSpacing(), -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        pushUndoState(tr("Copy"));
        m_view->copySelectedEntities(QPointF(dx, dy));
        appendCommandConsoleMessage(tr("COPY: selection copied with offset %1,%2.").arg(dx).arg(dy));
        return;
    }
    if (cmd == "MOVE" || cmd == "M" || cmd == "TRANSLATE" || cmd == "STRETCH") {
        if (!requireCadSelectionForCommand()) return;
        bool ok = false;
        const double dx = QInputDialog::getDouble(this, tr("Move"), tr("Move X:"), 0.0, -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        const double dy = QInputDialog::getDouble(this, tr("Move"), tr("Move Y:"), 0.0, -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        pushUndoState(cmd == "STRETCH" ? tr("Stretch proxy") : tr("Move"));
        m_view->moveSelectedEntities(QPointF(dx, dy));
        appendCommandConsoleMessage(tr("%1: move applied %2,%3.").arg(cmd).arg(dx).arg(dy));
        return;
    }
    if (cmd == "ROTATE" || cmd == "RO" || cmd == "ROTATE2") {
        if (!requireCadSelectionForCommand()) return;
        bool ok = false;
        const double angle = QInputDialog::getDouble(this, tr("Rotate"), tr("Angle in degrees:"), 90.0, -360000.0, 360000.0, 3, &ok);
        if (!ok) return;
        pushUndoState(cmd == "ROTATE2" ? tr("Rotate2 proxy") : tr("Rotate"));
        m_view->rotateSelectedEntities(angle);
        appendCommandConsoleMessage(tr("%1: rotation %2° applied around the selection center.").arg(cmd).arg(angle));
        return;
    }
    if (cmd == "SCALE" || cmd == "SC" || cmd == "LENGTHEN") {
        if (!requireCadSelectionForCommand()) return;
        bool ok = false;
        const double factor = QInputDialog::getDouble(this, tr("Scale"), tr("Facteur :"), cmd == "LENGTHEN" ? 1.25 : 2.0, 0.000001, 1000000.0, 6, &ok);
        if (!ok) return;
        pushUndoState(cmd == "LENGTHEN" ? tr("Lengthen proxy") : tr("Scale"));
        m_view->scaleSelectedEntities(factor);
        appendCommandConsoleMessage(tr("%1: factor %2 applied around the selection center.").arg(cmd).arg(factor));
        return;
    }
    if (cmd == "MIRROR" || cmd == "MI") {
        if (!requireCadSelectionForCommand()) return;
        const QStringList axes{tr("Horizontal"), tr("Vertical")};
        bool ok = false;
        const QString axis = QInputDialog::getItem(this, tr("Mirror"), tr("Axe :"), axes, 0, false, &ok);
        if (!ok) return;
        pushUndoState(tr("Mirror"));
        if (axis == axes.at(0)) m_view->mirrorSelectedEntitiesHorizontal();
        else m_view->mirrorSelectedEntitiesVertical();
        appendCommandConsoleMessage(tr("MIRROR: %1 axis applied.").arg(axis));
        return;
    }
    if (cmd == "FLIPHORIZONTAL" || cmd == "MIRRORH") {
        if (!requireCadSelectionForCommand()) return;
        pushUndoState(tr("Flip Horizontal"));
        m_view->mirrorSelectedEntitiesHorizontal();
        return;
    }
    if (cmd == "FLIPVERTICAL" || cmd == "MIRRORV") {
        if (!requireCadSelectionForCommand()) return;
        pushUndoState(tr("Flip Vertical"));
        m_view->mirrorSelectedEntitiesVertical();
        return;
    }
    if (cmd == "TRANSLATEROTATE") {
        if (!requireCadSelectionForCommand()) return;
        bool ok = false;
        const double dx = QInputDialog::getDouble(this, tr("Translate/Rotate"), tr("Move X:"), 0.0, -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        const double dy = QInputDialog::getDouble(this, tr("Translate/Rotate"), tr("Move Y:"), 0.0, -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        const double angle = QInputDialog::getDouble(this, tr("Translate/Rotate"), tr("Angle rotation :"), 0.0, -360000.0, 360000.0, 3, &ok);
        if (!ok) return;
        pushUndoState(tr("Translate/Rotate"));
        m_view->moveSelectedEntities(QPointF(dx, dy));
        if (std::abs(angle) > 1e-12) m_view->rotateSelectedEntities(angle);
        appendCommandConsoleMessage(tr("TRANSLATEROTATE: dx=%1 dy=%2 angle=%3.").arg(dx).arg(dy).arg(angle));
        return;
    }
    if (cmd == "ARRAY") {
        if (!requireCadSelectionForCommand()) return;
        bool ok = false;
        const int rows = QInputDialog::getInt(this, tr("Array"), tr("Lines :"), 2, 1, 1000, 1, &ok);
        if (!ok) return;
        const int cols = QInputDialog::getInt(this, tr("Array"), tr("Colonnes :"), 2, 1, 1000, 1, &ok);
        if (!ok) return;
        const double dx = QInputDialog::getDouble(this, tr("Array"), tr("Espacement X :"), m_view->gridSpacing(), -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        const double dy = QInputDialog::getDouble(this, tr("Array"), tr("Espacement Y :"), m_view->gridSpacing(), -100000000.0, 100000000.0, 4, &ok);
        if (!ok) return;
        pushUndoState(tr("Array"));
        const QVector<int> base = m_view->selectedEntityIndices();
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (r == 0 && c == 0) continue;
                m_document.duplicateEntities(base, QPointF(c * dx, r * dy));
            }
        }
        m_view->refreshFromDocumentPreservingSelection(base);
        appendCommandConsoleMessage(tr("ARRAY: %1 x %2 copies created.").arg(rows).arg(cols));
        return;
    }

    if (cmd == "ERASE" || cmd == "E" || cmd == "DELETE" || cmd == "CUTCLIP") { if (m_view->hasCadSelection()) { pushUndoState(cmd == "CUTCLIP" ? tr("Cut") : tr("Erase")); m_view->deleteSelectedEntities(); } return; }
    if (cmd == "OFFSET" || cmd == "O") { bool ok=false; double d=QInputDialog::getDouble(this,tr("Offset"),tr("Distance:"),m_view->gridSpacing(),-100000000.0,100000000.0,4,&ok); if(ok){ pushUndoState(tr("Offset")); m_view->offsetSelectedEntities(d);} return; }
    if (cmd == "TRIM" || cmd == "TR") { pushUndoState(tr("Trim")); if(!m_view->trimSelectedLinesToIntersection() && !m_undoStack.isEmpty()) { m_undoStack.removeLast(); if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast(); } return; }
    if (cmd == "EXTEND" || cmd == "EX") {
        if (m_view) {
            appendCommandConsoleMessage(tr("EXTEND: select an open entity with two endpoints, press Enter, then select the target boundary entity and press Enter."));
            m_view->startInteractiveModifyCommand(QStringLiteral("EXTEND"));
            statusBar()->showMessage(tr("EXTEND: select the open source, then press Enter."), 4000);
        }
        return;
    }
    if (cmd == "CHAMFER" || cmd == "CHA") { bool ok=false; double d=QInputDialog::getDouble(this,tr("Chamfer"),tr("Distance:"),m_view->gridSpacing(),0.000001,100000000.0,4,&ok); if(ok){ pushUndoState(tr("Chamfer")); if(!m_view->chamferSelectedLines(d) && !m_undoStack.isEmpty()) { m_undoStack.removeLast(); if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast(); } } return; }
    if (cmd == "FILLET" || cmd == "F") { bool ok=false; double r=QInputDialog::getDouble(this,tr("Fillet"),tr("Radius:"),m_view->gridSpacing(),0.000001,100000000.0,4,&ok); if(ok){ pushUndoState(tr("Fillet")); if(!m_view->filletSelectedLines(r) && !m_undoStack.isEmpty()) { m_undoStack.removeLast(); if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast(); } } return; }
    if (cmd == "PEDIT") {
        const QStringList ops = {tr("Fermer polyligne"), tr("Open polyline"), tr("Lisser / Spline fit")};
        bool ok = false;
        const QString op = QInputDialog::getItem(this, tr("Edit Polyline"), tr("Operation:"), ops, 0, false, &ok);
        if (!ok) return;
        pushUndoState(tr("PEDIT"));
        bool changed = false;
        if (op == ops.at(0)) changed = m_view->closeOrOpenSelectedPolylines(true);
        else if (op == ops.at(1)) changed = m_view->closeOrOpenSelectedPolylines(false);
        else changed = m_view->smoothSelectedPolylines();
        if (!changed) { rollbackUndoIfNeeded(); QMessageBox::information(this, tr("PEDIT"), tr("Select at least one polyline.")); }
        return;
    }
    if (cmd == "HATCHEDIT") {
        showHatchStyleDialog(true, false);
        return;
    }
    if (cmd == "MATCHPROP") { pushUndoState(tr("MATCHPROP")); if(!m_view->matchPropertiesFromFirstSelection() && !m_undoStack.isEmpty()) { m_undoStack.removeLast(); if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast(); } return; }
    if (cmd == "DIMCENTER") { pushUndoState(tr("DIMCENTER")); if(!m_view->addCenterMarksToSelectedCircles(m_view->gridSpacing()) && !m_undoStack.isEmpty()) { m_undoStack.removeLast(); if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast(); } return; }
    if (cmd == "DIMDIAMETER") { pushUndoState(tr("DIMDIAMETER")); if(!m_view->convertSelectedCirclesToDiameterDimensions(m_view->gridSpacing()*2.0) && !m_undoStack.isEmpty()) { m_undoStack.removeLast(); if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast(); } return; }
    if (cmd == "DIMRADIUS") { pushUndoState(tr("DIMRADIUS")); if(!m_view->convertSelectedCirclesToRadiusDimensions(m_view->gridSpacing()*1.5) && !m_undoStack.isEmpty()) { m_undoStack.removeLast(); if (!m_undoLabels.isEmpty()) m_undoLabels.removeLast(); } return; }
    if (cmd == "OFFSETTHROUGH") {
        appendCommandConsoleMessage(tr("OFFSETTHROUGH: proxy vers OFFSET distance dans DWGView."));
        executeCadCommand(QStringLiteral("OFFSET"));
        return;
    }
    if (cmd == "TRIMBOTH" || cmd == "AUTOTRIM") {
        appendCommandConsoleMessage(tr("%1: proxy vers TRIM deux lignes selectedes.").arg(cmd));
        executeCadCommand(QStringLiteral("TRIM"));
        return;
    }
    if (cmd == "JOIN") {
        pushUndoState(tr("JOIN proxy"));
        if(!m_view->smoothSelectedPolylines() && !m_undoStack.isEmpty()) { rollbackUndoIfNeeded(); }
        appendCommandConsoleMessage(tr("JOIN: DWGView proxy via smooth/close polyline. Strict merging to develop in CadDocument."));
        return;
    }
    if (cmd == "DRAWORDERFRONT" || cmd == "DRAWORDERBACK") {
        if (m_view && m_view->scene()) {
            const qreal z = (cmd == "DRAWORDERFRONT") ? 1000000.0 : -1000000.0;
            int count = 0;
            for (QGraphicsItem* item : m_view->scene()->selectedItems()) {
                if (!item) continue;
                item->setZValue(z);
                ++count;
            }
            appendCommandConsoleMessage(tr("%1: draw order applied to %2 item(s).").arg(cmd).arg(count));
        }
        return;
    }
    if (cmd == "REVERSE") {
        const QVector<int> indices = m_view ? m_view->selectedEntityIndices() : QVector<int>();
        bool changed = false;
        pushUndoState(tr("Reverse"));
        for (int idx : indices) {
            if (auto* pl = dynamic_cast<CadPolyline*>(m_document.entityAt(idx))) {
                QVector<QPointF> pts = pl->points();
                std::reverse(pts.begin(), pts.end());
                pl->setPoints(pts);
                changed = true;
            }
        }
        if (changed) m_view->refreshFromDocumentPreservingSelection(indices); else rollbackUndoIfNeeded();
        appendCommandConsoleMessage(changed ? tr("REVERSE: polyline(s) reversed.") : tr("REVERSE: select a polyline."));
        return;
    }
    if (cmd == "EXPLODE") {
        const QVector<int> indices = m_view ? m_view->selectedEntityIndices() : QVector<int>();
        bool changed = false;
        pushUndoState(tr("Explode"));
        QString blockError;
        const int explodedBlockEntities = m_document.explodeBlockReferences(indices, &blockError);
        if (explodedBlockEntities > 0) {
            if (m_view) {
                m_view->refreshFromDocumentPreservingSelection();
                applyBlockVisibility();
            }
            appendCommandConsoleMessage(tr("EXPLODE: block reference(s) converted to %1 entity/entities.").arg(explodedBlockEntities));
            return;
        }
        QVector<int> toRemove;
        for (int idx : indices) {
            CadEntity* e = m_document.entityAt(idx);
            if (auto* pl = dynamic_cast<CadPolyline*>(e)) {
                const QVector<QPointF> pts = pl->points();
                if (pts.size() >= 2) {
                    for (int i=1; i<pts.size(); ++i) m_document.addEntity(std::make_unique<CadLine>(pts.at(i-1), pts.at(i)));
                    if (pl->closed() && pts.size() > 2) m_document.addEntity(std::make_unique<CadLine>(pts.last(), pts.first()));
                    toRemove.append(idx); changed = true;
                }
            } else if (auto* r = dynamic_cast<CadRectangle*>(e)) {
                const QRectF b = r->rect().normalized();
                const QPointF p1=b.topLeft(), p2=b.topRight(), p3=b.bottomRight(), p4=b.bottomLeft();
                m_document.addEntity(std::make_unique<CadLine>(p1,p2)); m_document.addEntity(std::make_unique<CadLine>(p2,p3));
                m_document.addEntity(std::make_unique<CadLine>(p3,p4)); m_document.addEntity(std::make_unique<CadLine>(p4,p1));
                toRemove.append(idx); changed = true;
            }
        }
        if (changed) { m_document.removeEntities(toRemove); m_view->refreshFromDocumentPreservingSelection(); }
        else rollbackUndoIfNeeded();
        appendCommandConsoleMessage(changed ? tr("EXPLODE: objects converted to segments.") : tr("EXPLODE: select a polyline or rectangle."));
        return;
    }
    if (cmd == "DIVIDE") {
        const QVector<int> indices = m_view ? m_view->selectedEntityIndices() : QVector<int>();
        bool ok = false;
        const int parts = QInputDialog::getInt(this, tr("Divide"), tr("Nombre de segments:"), 2, 2, 10000, 1, &ok);
        if (!ok) return;
        pushUndoState(tr("Divide"));
        int markers = 0;
        for (int idx : indices) {
            const CadEntity* e = m_document.entityAt(idx);
            if (auto* l = dynamic_cast<const CadLine*>(e)) {
                for (int i=1; i<parts; ++i) {
                    const double t = static_cast<double>(i)/parts;
                    const QPointF p(l->start().x() + (l->end().x()-l->start().x())*t, l->start().y() + (l->end().y()-l->start().y())*t);
                    m_document.addEntity(std::make_unique<CadText>(p, QStringLiteral("•"), std::max(1.0, m_view ? m_view->annotationTextHeight()*0.5 : 2.0)));
                    ++markers;
                }
            }
        }
        if (markers > 0) m_view->refreshFromDocumentPreservingSelection(indices); else rollbackUndoIfNeeded();
        appendCommandConsoleMessage(tr("DIVIDE: %1 marker(s) added.").arg(markers));
        return;
    }
    if (cmd == "BREAK" || cmd == "BREAKOUT" || cmd == "BREAKOUTGAP" || cmd == "BREAKOUTMANUAL") {
        appendCommandConsoleMessage(tr("%1: command connected; exact interactive break still to add. Use TRIM/EXPLODE for the current workflow.").arg(cmd));
        statusBar()->showMessage(tr("%1: utilisez TRIM ou EXPLODE dans cette version." ).arg(cmd), 5000);
        return;
    }
    if (cmd == "DDEDIT" || cmd == "TEXTEDIT") {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Text Edit"), tr("New text:"), m_view->annotationText(), &ok);
        if (!ok) return;
        const double height = QInputDialog::getDouble(this, tr("Text Edit"), tr("Height:"), m_view->annotationTextHeight(), 0.01, 1000000.0, 3, &ok);
        if (!ok) return;
        const double rot = QInputDialog::getDouble(this, tr("Text Edit"), tr("Rotation:"), 0.0, -360000.0, 360000.0, 3, &ok);
        if (!ok) return;
        pushUndoState(tr("DDEDIT"));
        if(!m_view->editSelectedText(text, height, rot)) { rollbackUndoIfNeeded(); QMessageBox::information(this, tr("DDEDIT"), tr("Select at least one text entity.")); }
        else { m_view->setAnnotationText(text); m_view->setAnnotationTextHeight(height); }
        return;
    }

    if (cmd == "STYLE" || cmd == "DIMSTYLE") { statusBar()->showMessage(tr("STYLE/DIMSTYLE: use the Format menu to set default height."), 4000); return; }
    if (cmd == "PROPERTIES" || cmd == "PR") { if (m_propertiesDock) { m_propertiesDock->show(); m_propertiesDock->raise(); } updatePropertiesPanel(); return; }
    if (cmd == "REGEN" || cmd == "RE") { refreshCadScene(); return; }
    if (cmd == "ZOOM" || cmd == "Z") { if (m_view) m_view->zoomToExtents(); return; }
    if (cmd == "PAN" || cmd == "P") { m_view->setDrawingTool(GraphicsView::DrawingTool::Noe); statusBar()->showMessage(tr("PAN : utilisez le bouton milieu ou espace + bouton gauche."), 4000); return; }
    if (cmd == "COMMANDLINE") { showCommandLine(); return; }
    if (cmd == "IMAGEATTACH" || cmd == "IMAGE") { attachRasterImage(); return; }
    if (cmd == "LAYOUT" || cmd == "PAGESETUP") { configurePageLayout(); return; }
    if (cmd == "CLEANSCREEN" || cmd == "CLEANSCREENON" || cmd == "CLEANSCREENOFF") { toggleCleanScreen(); return; }
    if (cmd == "WORKSPACE" || cmd == "WSCURRENT") { showWorkspaceManager(); return; }
    if (cmd == "PALETTES") { showAllCadPalettes(); return; }
    if (cmd == "VIEW" || cmd == "NAMEDVIEWS") { showNamedViewsManager(); return; }
    if (cmd == "VPORTS" || cmd == "VIEWPORTS") { showViewportManager(); return; }
    if (cmd == "XREF" || cmd == "DWGATTACH") { attachDwgReference(); return; }
    if (cmd == "DWFATTACH") { attachDwfUnderlay(); return; }
    if (cmd == "DGNATTACH") { attachDgnUnderlay(); return; }
    if (cmd == "APPLOAD") { loadCadApplication(); return; }
    if (cmd == "NEWWINDOW") { newWindowFromCurrentDrawing(); return; }
    if (cmd == "CASCADE") { cascadeWindows(); return; }
    if (cmd == "TILEHORIZ" || cmd == "TILEVERT") { tileWindows(); return; }
    statusBar()->showMessage(tr("Unknown command or not connected yet: %1").arg(command), 4000);
}
