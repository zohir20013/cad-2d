# DWGViewerAdvanced

**DWGViewerAdvanced** is a 2D CAD viewer and lightweight editor written in **C++17** with **Qt6**. It opens, displays, inspects, edits, and exports CAD drawings through a native DXF pipeline, with optional DWG support through **ODA File Converter**.

The project is designed as a clean base for a CAD viewer/editor: Qt Widgets interface, 2D graphics scene, layer management, native DXF import/export, PDF/SVG export, and drawing/modify tools inspired by a classic AutoCAD-style workflow.

## Main features

- Qt6 desktop interface based on `QGraphicsView`.
- Native DXF loading and rendering.
- Optional DWG workflow through external DWG ↔ DXF conversion with ODA File Converter.
- 2D CAD tools: line, circle, arc, rectangle, polyline, spline placeholder, text, dimension, hatch, selection, and modify tools.
- Expanded 2D geometry kernel in `src/geometry/`: centralized tolerances, vector math, projections, signed distances, closest-point queries, bounding boxes, analytical intersections, polygon helpers, polyline simplification, offset helpers, tangent construction, trim/extend helpers, true line-line fillet construction, spline/NURBS approximation, DXF bulge support, hatch generation, snap candidates, affine transforms, arrays, dimension geometry, convex region clipping, chamfer/break helpers, and polyline healing utilities.
- Parametric 2D constraint foundation in `src/constraints/`: fixed, coincident, horizontal, vertical, collinear, parallel, perpendicular, equal length, distance, angle, point-on-line, point-on-circle, concentric, equal radius, tangent line-circle, midpoint, and symmetry constraints.
- CAD annotation engine in `src/annotation/`: associative dimension definitions, linear/aligned/rotated/angular/radius/diameter/ordinate/arc-length dimensions, baseline and continued dimensions, formatted tolerances, unit suffixes, arrowhead geometry, leaders, center marks, simple tables, text wrapping, and revision-cloud geometry.
- Production workflow modules: entity metrics, advanced selection, layer states, snapshot-based undo/redo, drawing audit/repair, block attributes, plot layout/viewport helpers, command-line command registry, measurement engine, named view manager, user preferences, and standards checker.
- Pro CAD workflow additions: advanced modify engine, smart object snap engine, style manager, file recovery/autosave manager, customizable tool palette registry, and sheet-set/batch-plot preparation.
- Optimized drafting workspace layout: the drawing scene gets priority, the command line is compact, and Layers/Properties are stacked as tabs in one narrow dock column.
- Layer management with colors, line types, line weights, visibility, locking, and current-layer selection.
- Professional scene navigation: slower stable mouse-cursor anchored zoom/dezoom, precision Ctrl zoom, corrected cursor lock after every wheel tick, wheel panning, middle-button pan, Space+left pan, zoom window, zoom extents, render optimization, and LOD behavior.
- Export to DXF, PDF, and SVG.
- Embedded SVG icons through Qt resources.


## Native DXF library improvements

The native DXF pipeline has been strengthened so DXF handling is no longer just a file-open helper. The `src/dxf_library/DxfLibrary.*` facade now provides:

- quick DXF scanning before import: ASCII/binary detection, `ACADVER`, `INSUNITS`, sections, layer count, block count, entity count, INSERT count, and object count;
- structured `DxfLibraryReport` output for imports/exports, including imported entity counts, fallback counts, unsupported/ignored geometry, block statistics, layouts, XDATA and XREF warnings;
- native preservation of DXF `BLOCK` definitions and normal `INSERT` references so the Block menu can manage imported DXF blocks;
- optional block explosion behavior through import options or the `DWGVIEWER_DXF_FORCE_EXPLODE_BLOCKS` environment variable;
- improved native DXF export with a real `BLOCKS` section, user block definitions, standard model/paper block records, and `INSERT` entities instead of losing block structure;
- a native `saveEditable()` facade so future UI/export code can use the DXF library instead of calling document internals directly.

This makes the DXF layer closer to a CAD database bridge: the viewer can import, report, preserve, edit, manage blocks, and export DXF using the project’s own C++ code. DWG files still require external DWG-to-DXF conversion because DWG is a proprietary binary format.


## Professional scene navigation

The drawing view has been updated to behave more like a CAD viewport:

- mouse wheel zooms in/out around the exact point under the cursor, so the area below the mouse stays fixed while zooming;
- normal wheel zoom is intentionally slower and smoother: about 7% per wheel notch instead of large jumps;
- Ctrl + mouse wheel enables precision zoom, about 2% per notch, for small drafting details;
- high-resolution touchpad wheel events are normalized and clamped to avoid sudden zoom bursts;
- after every zoom tick, `GraphicsView` measures the scene point under the cursor and applies a correction transform so the target point remains locked under the mouse;
- Shift + mouse wheel pans horizontally;
- Alt + mouse wheel pans vertically;
- middle mouse button drag pans the model space;
- Space + left mouse button drag also pans the model space;
- middle mouse double-click runs Zoom Extents;
- `ZW` / `ZOOMWINDOW` starts a zoom-window selection;
- `ZE` / `ZOOMEXTENTS` fits the full drawing;
- `ZI` and `ZO` zoom in/out around the current mouse position with a smaller 1.10 step.

The implementation uses `mapToScene()` for anchor acquisition, clamps extreme zoom scales, and applies a post-zoom anchor correction. This avoids drift caused by hidden Qt viewport/scrollbar state and is more stable for large CAD coordinates and large DXF/DWG drawings.

## Drafting workspace layout

The main window is configured to maximize the central drawing scene. The `GraphicsView` is the expanding central widget, while auxiliary palettes are deliberately compact:

- `LayersDock` and `PropertiesDock` are tabified on the right side, so only one narrow palette column is used.
- The user can switch between the Layers and Properties tabs without reducing the drawing canvas width twice.
- `CommandLineDock` stays at the bottom with a small height and a short command-history area.
- The `PALETTES` and workspace commands reapply the tabbed compact layout automatically after showing dock widgets.

This makes the default workspace closer to a professional drafting workflow: large model-space viewport first, supporting palettes second.

## Geometry kernel scope

The geometry layer is intentionally separated in `src/geometry/GeometryKernel.*` and `src/geometry/GeometryKernelAdvanced.*` so editing tools can reuse the same numerical behavior instead of duplicating math inside the UI layer. It currently covers:

- finite/infinite line math, projections, distances, signed distances, orientation, collinearity, ray parameters, and nearest ray candidate selection;
- analytical intersections for line-line, segment-segment, line-circle, segment-circle, circle-circle, line-ellipse, segment-ellipse, polyline-segment, polyline-polyline, circle-polyline, ellipse-polyline, arc-segment, arc-circle, arc-arc, arc-polyline, and approximated arc-ellipse;
- closest-point queries for segments, polylines, circles, arcs, and ellipses;
- bounding boxes for point sets, circles, arcs, and ellipses;
- polygon utilities, area, centroid, winding, point-in-polygon, regular polygon generation, and polyline simplification;
- CAD edit helpers for offsets, line-line fillets, segment trim/extend, line-line chamfer, segment break helpers, and tangent points from an external point to a circle;
- AutoCAD-style construction helpers for circles from 2/3 points, arcs from 3 points, center-start-end arcs, parallel/perpendicular lines, angle bisectors, rectangular arrays, polar arrays, and affine transforms;
- DXF/LWPOLYLINE bulge conversion and bulged polyline approximation;
- spline foundations: quadratic/cubic Bezier evaluation, cubic Bezier flattening, clamped B-spline evaluation, NURBS evaluation/approximation, and Catmull-Rom fit-point approximation;
- snap candidate generation for endpoints, midpoints, centers, quadrants, perpendicular, tangent, nearest-on-curve, and best-snap selection;
- region foundations: convex hull, convex polygon intersection, convex clipping, self-intersection detection, polyline healing, resampling/stationing, and hatch line generation;
- dimension geometry builders for aligned, linear, angular, and radius dimensions.

This makes the project a stronger foundation for professional-style 2D editing commands comparable to the geometric support expected by a classic 2D CAD workflow. It is still an open-source educational/editor kernel, not a clone of AutoCAD’s proprietary geometry engine.


## Parametric geometric constraints

This version adds a first UI-independent constraint solver in `src/constraints/GeometricConstraintSolver.*`. It is designed as the next development step after the geometry kernel: drawing entities can be mapped to solver points, lightweight line references, and circle references, then solved before writing the updated coordinates back to the CAD document.

Supported constraint families include:

- fixed point and coincident point constraints;
- horizontal, vertical, collinear, parallel, and perpendicular line constraints;
- dimensional constraints for distance, angle, equal length, and equal radius;
- point-on-line and point-on-circle constraints;
- concentric circles and tangent line-circle constraints;
- midpoint and symmetry constraints;
- residual reporting, validation issues, simple graph statistics, estimated scalar constraints, and estimated degrees of freedom.

Minimal API example:

```cpp
#include "constraints/GeometricConstraintSolver.h"

using namespace CadGeometry;

GeometricConstraintSystem system;
int a = system.addPoint(QPointF(0.0, 0.0), true);
int b = system.addPoint(QPointF(10.0, 3.0));
int c = system.addPoint(QPointF(1.0, 8.0));

int base = system.addLine(a, b);
int height = system.addLine(a, c);

system.addHorizontal(base);
system.addPerpendicular(base, height);
system.addDistance(a, b, 20.0);

ConstraintSolveReport report = system.solve();
QPointF solvedB = system.pointPosition(b);
QPointF solvedC = system.pointPosition(c);
```

The current solver uses iterative projection/relaxation. This is appropriate for interactive 2D editing prototypes and many simple CAD constraints, but it is not yet a full industrial parametric solver. The next professional step is a Jacobian-based solver with constraint dependency analysis, conflict diagnostics, persistent constraint objects attached to `CadEntity`, and UI commands for applying/removing constraints.

## AutoCAD 2010-style kernel additions

This version adds a broader 2D geometry foundation intended to support commands commonly expected in a classic 2D CAD workflow, including CIRCLE 2P/3P helpers, ARC 3P helpers, DXF bulge conversion, spline/NURBS approximation, OSNAP candidate generation, hatch line generation, affine transforms, rectangular/polar arrays, chamfer/break helpers, region clipping, polyline stationing, and dimension construction geometry.

It does **not** claim binary or feature parity with AutoCAD 2010. AutoCAD uses a mature proprietary geometry/database stack. This project now contains a stronger open 2D kernel foundation that can support more AutoCAD-like commands inside this Qt editor.


## CAD annotation and associative dimensions

This version adds a UI-independent annotation engine in `src/annotation/CadAnnotationEngine.*`. The module is designed to generate render-ready geometry for professional CAD annotation commands without tying the calculations to a specific widget. It can be used by `GraphicsView`, DXF export, PDF export, and future property panels from the same data model.

Supported annotation foundations include:

- aligned, horizontal/vertical linear, rotated, angular, radius, diameter, ordinate, and arc-length dimensions;
- baseline and continued dimension batches;
- associative dimension definitions that can be rebuilt when referenced points, circles, or arcs change;
- dimension styles with text size, arrow size, annotation scale, precision, prefixes/suffixes, unit suffixes, and decimal comma support;
- tolerance formatting: symmetric, deviation, limits, and basic dimensions;
- multiple arrowhead families: closed filled, closed blank, open, architectural tick, dot, and none;
- leader/multileader geometry, center marks, simple table geometry, text wrapping, and revision-cloud outlines.

Minimal API example:

```cpp
#include "annotation/CadAnnotationEngine.h"

using namespace CadAnnotation;

DimensionStyle style;
style.linearPrecision = 2;
style.showUnitSuffix = true;
style.lengthUnit = LengthUnit::Millimeter;

ToleranceSpec tol;
tol.mode = ToleranceMode::Symmetric;
tol.upper = 0.05;

DimensionVisualGeometry dim = makeAlignedDimension(
    QPointF(0.0, 0.0),
    QPointF(120.0, 0.0),
    12.0,
    style,
    tol
);

// dim.dimensionLines, dim.extensionLines, dim.arrows and dim.textBlocks
// are ready for rendering or export.
```

This is the next development step after the geometric constraint solver. The remaining work is to attach persistent `DimensionDefinition` objects to `CadEntity`, update them after entity edits, and export the generated annotation objects to DXF DIMENSION/MTEXT/LEADER entities.

## Production workflow modules

This version adds a broader set of UI-independent modules that make the project closer to a real CAD application architecture, not only a drawing canvas. These modules are designed to be connected progressively to menus, palettes, command-line actions, and export pipelines.

### Entity utilities

`src/core/CadEntityUtils.*` provides reusable geometry analysis for every supported `CadEntity` type:

- type names and JSON entity summaries;
- representative point extraction for lines, circles, arcs, ellipses, polylines, text, dimensions, leaders, hatches, and block references;
- entity and document bounding boxes;
- curve length and closed-area estimates;
- nearest-point distance queries;
- degenerate-entity detection;
- drawing statistics for reports and future properties palettes.

### Advanced selection

`src/selection/CadSelectionEngine.*` adds selection logic that can be reused by mouse selection, command-line selection, and future selection filters:

- nearest pick with aperture;
- crossing and contained window selection;
- fence selection;
- selection by layer and by entity type;
- selection set union/subtract/intersection/toggle;
- filter by type, layer and color.

### Layer states and layer tools

`src/layers/CadLayerStateManager.*` adds CAD-style layer workflow helpers:

- save/restore named layer states;
- isolate layers;
- show/unlock all layers;
- freeze empty layers;
- merge layers;
- count entities per layer;
- find entities on layer groups.

### Command history

`src/history/CadCommandHistory.*` adds a document-snapshot undo/redo foundation:

- transaction begin/commit/cancel;
- undo/redo labels;
- configurable history depth;
- JSON snapshot restore through `CadDocument::toJson()` and `CadDocument::fromJson()`.

This is not as memory-efficient as a delta command stack, but it is reliable and simple for the current document model. The next step is replacing large snapshots with per-command deltas for very large drawings.

### Drawing audit and repair

`src/quality/CadAuditEngine.*` provides CAD cleanup tools similar to a lightweight `AUDIT` / `PURGE` foundation:

- missing layer detection;
- empty layer reporting and purge;
- degenerate geometry reporting and purge;
- duplicate geometry hints;
- out-of-limits warnings;
- invalid style repair;
- automatic creation of missing layers referenced by entities.


### Block management menu

The application now includes a top-level **Block** menu inspired by QCAD/AutoCAD-style workflows. The implemented actions are native C++ actions, not placeholders:

- **Explode Selected Block(s)** converts selected block references back into world-space editable entities.
- **Show All Blocks** and **Hide All Blocks** control block-reference visibility in the current drawing view.
- **Add Empty Block** creates a reusable empty block definition.
- **Create Block from Selection** converts selected geometry into a block definition and inserts a reference.
- **Insert Block** places a reference with insertion point, scale and rotation.
- **Rename Block** updates the definition and all existing references.
- **Duplicate Block** copies an existing definition under a new name.
- **Remove Block** removes only unused definitions, protecting referenced blocks.
- **Purge Unused Blocks** removes all definitions without references.
- **Select/Deselect Block References** filters selection to block references.
- **Block Manager** lists definitions with object count and reference count and provides quick actions.

Command aliases are also connected: `BLOCK`, `BA`, `BC`, `BI`, `BN`, `BY`, `BR`, `BP`, `BS`, `BH`, `BK`, `BX`, `XP`, and `EXPLODE`.

### Block attributes

`src/blocks/CadBlockAttributeEngine.*` adds foundations for attribute-based blocks:

- attribute definitions with tag, prompt, default value, flags, insertion point and style;
- attribute instantiation at block insertion;
- transform of attribute positions by base point, insertion point, scale and rotation;
- required-attribute validation;
- basic field evaluation syntax `%<FIELD>%`;
- JSON serialization helpers.

### Plot layout and paper-space helpers

`src/plot/CadPlotLayoutEngine.*` adds a first paper-space / plotting foundation:

- standard A-series, Letter, Legal, Tabloid and ARCH paper sizes;
- portrait/landscape orientation;
- printable-area calculation;
- viewport definitions;
- model-to-paper and paper-to-model transforms;
- automatic viewport from model extents;
- scale parsing and formatting such as `1:100`.

These modules still need UI integration. They are intentionally separated first so the core logic can be tested and reused before adding toolbars, palettes and commands.


## Additional CAD workflow modules

This version adds another UI-independent development layer for functions normally needed by a more complete 2D CAD application.

### Command-line engine

`src/commands/CadCommandLineEngine.*` adds a command registry and parser for AutoCAD-style command workflows:

- built-in command definitions and aliases such as `LINE/L`, `CIRCLE/C`, `MOVE/M`, `COPY/CO`, `ROTATE/RO`, `OFFSET/O`, `TRIM/TR`, `EXTEND/EX`, `FILLET/F`, `ZOOM/Z`, `DIST/DI`, and `AREA/AA`;
- token parsing with quoted strings;
- script parsing with line and semicolon separators;
- command autocomplete;
- command categories, help text, repeatable-command history, and system variables such as `ORTHOMODE`, `OSMODE`, `PICKBOX`, `SNAPMODE`, `GRIDMODE`, `LUNITS`, `LUPREC`, and `AUPREC`;
- JSON serialization for command profiles and history.

This prepares the project for a real command prompt instead of only toolbar-driven editing.

### Measurement engine

`src/measure/CadMeasurementEngine.*` adds reusable measurement tools:

- distance, angle, coordinate, and bounding-box measurements;
- polyline length, polygon area, polygon centroid;
- entity length, area, bounds, degenerate state, and layer/type summary;
- selection length and selection area;
- drawing measurement summaries;
- nearest-entity query and radius search;
- formatted distance and area labels.

These functions are intended for future `DIST`, `AREA`, `LIST`, and properties palette commands.

### Named view and navigation manager

`src/view/CadViewStateManager.*` adds view state logic independent of `QGraphicsView`:

- zoom extents, zoom window, zoom scale, pan, and view rotation;
- named views save/restore;
- previous/next view navigation stacks;
- model-to-view and view-to-model transforms;
- JSON persistence for view states.

### User preferences

`src/settings/CadUserPreferences.*` adds persistent drafting preferences:

- grid display and adaptive grid settings;
- snap spacing, object snap mode mask, snap aperture, and snap tracking;
- ortho mode, polar tracking angles, dynamic input, command-line autocomplete;
- unit precision and decimal formatting;
- display colors, anti-aliasing, lineweight visibility, and large-drawing mode;
- classic AutoCAD-style and dark-modern profiles;
- JSON save/load from a standard configuration path.

### CAD standards checker

`src/standards/CadStandardsManager.*` adds a first CAD standards validation layer:

- standard layer definitions with color, linetype, lineweight, required-state, and override policy;
- metric architectural and metric mechanical profiles;
- validation for missing required layers, non-standard layers, mismatched layer styles, invalid entity layers, unknown linetypes, and non-standard lineweights;
- automatic fix helpers for creating required layers, restoring standard layer properties, moving invalid entities to layer `0`, and resetting forbidden entity overrides.

These modules do not replace the UI work still needed, but they add the internal logic required for command-line interaction, measurement, standards checking, preferences, and named views.


## Pro CAD workflow additions

This version adds another professional-development layer intended to close more of the gap between a lightweight CAD editor and a classic production CAD workflow.

### Advanced modify engine

`src/modify/CadAdvancedModifyEngine.*` adds reusable editing operations that are normally required before a CAD editor feels complete:

- grip point extraction for common entity types;
- crossing-window stretch logic for lines, polylines and dimensions;
- `LENGTHEN`-style line extension by delta or target length;
- `BREAK`, `DIVIDE`, and `MEASURE` helpers for line entities;
- connected-line `JOIN` into polyline;
- `EXPLODE` foundations for rectangles, polylines, polygons, circles, arcs, ellipses, hatches and leaders;
- two-point `ALIGN` with optional scale;
- rectangular arrays, polar arrays and copy-along-path helpers.

### Smart object snap engine

`src/snap/CadSmartSnapEngine.*` adds a higher-level OSNAP system on top of the geometry kernel:

- endpoint, midpoint, center, quadrant, intersection, perpendicular, tangent, nearest and extension modes;
- snap aperture ranking with AutoCAD-style priority ordering;
- grid snapping, ortho tracking and polar tracking helpers;
- approximate intersection snapping across mixed entity types through sampled curve segments;
- command/UI-ready `SmartSnapContext` and `SmartSnapResult` structures.

### CAD style manager

`src/styles/CadStyleManager.*` adds document-level style definitions:

- text styles with font, fixed height, width factor, oblique angle and annotative flag;
- dimension styles with text height, arrow size, extension settings, precision, suffixes and decimal formatting;
- linetype definitions with dash/gap strokes;
- multileader and table style foundations;
- ISO and architectural default profiles;
- JSON serialization for future project/session persistence.

### File safety and recovery

`src/recovery/CadFileSafetyManager.*` adds safer save/recovery foundations:

- atomic JSON writes through `QSaveFile`;
- backup creation before destructive saves;
- autosave file naming based on the original document path;
- recovery-file listing, SHA-256 hashing and old-recovery cleanup.

### Tool palette registry

`src/palettes/CadToolPalette.*` adds a command/tool registry for future palettes and ribbon-like UI:

- default draw, modify, inquiry, annotate and manage tools;
- AutoCAD-like aliases such as `L`, `C`, `CO`, `RO`, `TR`, `EX`, `DLI`, `DAL`, `LA`, and `PU`;
- favorites, visibility toggles, category lists and search;
- JSON persistence for customizable tool palettes.

### Sheet set and batch plot preparation

`src/print/CadSheetSetManager.*` adds a stronger plotting workflow foundation:

- named sheet/layout definitions;
- standard metric sheet presets A0 through A4;
- printable area, model window, plot scale, monochrome and lineweight flags;
- plot stamp metadata;
- PDF plot-job data model for current-sheet or multi-sheet export;
- scale parsing/formatting such as `1:100`.

These modules are UI-independent by design. The next implementation step is wiring them into menus, command handlers, dock panels and rendering/export code.


## Project status

This archive is cleaned for GitHub publication. Old correction notes, build folders, CMake caches, logs, generated artifacts, AppImage scripts, Debian package scripts, and packaging folders were removed.

The repository contains only the source code, resources, DXF tests, a clean build script, and this single presentation file.

Direct DWG decoding is not embedded as an internal library. For DWG files, the application uses ODA File Converter as an external tool, then processes the generated DXF through the native pipeline.

## Repository structure

```text
.
├── CMakeLists.txt
├── README.md
├── build_clean.sh
├── main.cpp
├── resources/
│   ├── icons.qrc
│   └── icons/cad/
├── src/
│   ├── MainWindow.*
│   ├── GraphicsView.*
│   ├── DebugLogger.*
│   ├── DxfLoader.*
│   ├── cad/
│   ├── annotation/
│   ├── blocks/
│   ├── cad_import/
│   ├── commands/
│   ├── constraints/
│   ├── core/
│   ├── dxf_library/
│   ├── geometry/
│   ├── history/
│   ├── layers/
│   ├── measure/
│   ├── modify/
│   ├── palettes/
│   ├── plot/
│   ├── print/
│   ├── quality/
│   ├── recovery/
│   ├── settings/
│   ├── snap/
│   ├── standards/
│   ├── selection/
│   ├── styles/
│   └── view/
└── tests/
    ├── dxf_regression/
    └── dxf_samples/
```

## Dependencies

### Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build python3 \
  qt6-base-dev qt6-base-dev-tools qt6-svg-dev qt6-tools-dev \
  libgl1-mesa-dev libxkbcommon-x11-0 libxcb-cursor0 \
  libxcb-xinerama0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
  libxcb-render-util0 libxcb-shape0 libxcb-xkb1
```

### Windows

Install:

- Qt 6 with Qt Creator;
- CMake;
- Ninja or Visual Studio Build Tools;
- a C++ kit compatible with the installed Qt version.

## Build on Linux

Simple method:

```bash
chmod +x build_clean.sh
./build_clean.sh
```

Manual method:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/DWGViewerAdvanced
```

Without Ninja:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/DWGViewerAdvanced
```

## Build on Windows

With Qt Creator:

1. Open `CMakeLists.txt`.
2. Select a Qt6 kit.
3. Run `Configure Project`.
4. Build and run.

Command-line build, adapting the Qt path as needed:

```bat
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.6.3\msvc2019_64
cmake --build build --parallel
build\DWGViewerAdvanced.exe
```

## DWG support

To open or export DWG files, install ODA File Converter and define the executable path when necessary.

Linux:

```bash
export DWGVIEWER_ODA_CONVERTER_PATH=/path/to/ODAFileConverter
./build/DWGViewerAdvanced
```

Windows:

```bat
set DWGVIEWER_ODA_CONVERTER_PATH=C:\path\to\ODAFileConverter.exe
build\DWGViewerAdvanced.exe
```

When this variable is not set, the application tries to locate the tool in the system `PATH` and in common installation paths.

## DXF tests

The included tests quickly validate the structure of the DXF samples in `tests/dxf_samples`.

```bash
python3 tests/dxf_regression/run_dxf_regression.py
```

These tests do not replace full graphical validation, but they are useful for checking that the sample files remain readable and structurally valid.

## Recommended cleanup before commit

```bash
rm -rf build build-* .appimage-tools AppDir dist dist-deb pkg-deb pkg-deb-from-appdir
find . -name "*.user" -delete
find . -name "*.log" -delete
```

Generated files are already covered by `.gitignore`.

## Suggested GitHub publication

```bash
git init
git add .
git commit -m "Initial cleaned Qt6 CAD viewer source"
git branch -M main
git remote add origin https://github.com/USER/DWGViewerAdvanced.git
git push -u origin main
```

Replace `USER` with your GitHub account name.

## Known limitations

- DWG depends on an external converter; it is not decoded natively inside this project.
- Import quality depends on the source DXF file or on the DXF generated by the converter.
- The geometry, constraint and workflow layers are now stronger 2D editing foundations, including analytical intersections, construction helpers, spline/NURBS approximation, snaps, hatching, convex region clipping, dimension geometry, transforms, arrays, chamfer/break/trim/extend helpers, polyline healing, advanced modify tools, smart OSNAP logic, style definitions, autosave/recovery, tool palettes, sheet-set preparation and a first parametric constraint solver. They are still not a full proprietary industrial CAD engine: a Jacobian-based constraint solver, robust general polygon booleans, deeper UI integration for the new modules, DWG-native decoding, and industrial validation remain future work.
- Very large drawings may require additional optimization depending on machine performance and entity complexity.
- No license file is included in the supplied source. Before public release, choose a license and add it to the repository.

## License

No license is defined in the provided source code. Add a suitable license file before publishing the project publicly.

### DXF viewport and stale extents handling

The native DXF loader now validates `$EXTMIN` / `$EXTMAX` against the actual imported entity bounds.
Some exporters write stale or oversized extents, which can make a drawing open far away from the visible area or appear incorrectly zoomed.
When the header extents are clearly wrong, the application automatically uses measured geometry bounds for the scene and Zoom Extents.


### DXF hatch safety fix

This version adds defensive handling for malformed DXF `HATCH` boundaries. Some real DXF files contain open or stale hatch loops, seed/origin points, or broken associative boundaries. Rendering those loops as closed paths can create very long false lines outside the hatch area. The native DXF importer now validates hatch loop continuity, rejects unsafe hatch loops, and the renderer also refuses to close hatch paths with abnormal gaps.

- Robust DXF HATCH import guard: broken or unordered hatch edge paths are rejected instead of drawing false long bridge lines outside the hatch boundary.

### Native material hatch pattern library

This version expands the native CAD hatch renderer with multiple predefined material and solid-fill patterns. The hatch dialog now includes CAD-style patterns such as ANSI31-ANSI38, horizontal, vertical, crosshatch, grid, net, concrete/béton, wood/bois, sand/sable, gravel/gravier, brick/brique, masonry, tile/carrelage, earth/terre, insulation/isolation, glass/verre, water/eau, grass/herbe, steel and metal.

Solid hatch support was also extended with opacity-style variants: `SOLID`, `SOLID_10`, `SOLID_20`, `SOLID_25`, `SOLID_30`, `SOLID_40`, `SOLID_50`, `SOLID_60`, `SOLID_75`, `SOLID_90`, `SOLID_LIGHT`, `SOLID_MEDIUM`, `SOLID_DARK`, and `SOLID_TRANSPARENT`.

The renderer uses native Qt tiled brushes for material patterns and exports solid variants as standard DXF `SOLID` hatches for better compatibility with other CAD applications.

### Hatch rendering fix

Material hatch patterns are now rendered through the same native pattern logic in both normal drawing mode and large-document LOD mode. This fixes the issue where selecting CONCRETE, WOOD, SAND, BRICK, INSULATION or SOLID variants could still display the same default diagonal hatch in large DXF scenes.

### DXF dimension style scaling

The native DXF importer now reads dimension style data such as `DIMSCALE`, `DIMTXT`, `DIMASZ`, `DIMEXO`, `DIMEXE`, `DIMLFAC`, and `DIMGAP` from the DXF header and DIMSTYLE table. Linear dimension fallback rendering uses these values for text and arrow sizes, with a safety clamp that prevents oversized annotation from covering the drawing when a DXF contains stale or incompatible dimension block graphics.


## DXF annotation rendering improvements

This build improves CAD annotation fidelity for imported DXF/DWG-converted files:

- MTEXT paragraph breaks are rendered line by line instead of being collapsed.
- Common AutoCAD MTEXT formatting codes are cleaned while preserving visible text.
- MTEXT width and line-spacing metadata are kept for wrapped notes.
- MLEADER text content is parsed from DXF text fields when available.
- Dense closed spline/polyline revision-cloud boundaries are detected more reliably.
- SHX-like text styles such as TXT, STANDARD, ROMANS and SIMPLEX are mapped to a readable vector font fallback.

These changes are intended to make notes, leaders and revision clouds display closer to BricsCAD/AutoCAD for 2D construction details.

### DXF leader text and revision cloud rendering update

This build improves AutoCAD/BricsCAD-style note rendering around leader annotations:

- MLEADER/LEADER text is lifted above the landing line so the line no longer cuts through the letters.
- MTEXT imported from DXF is lifted slightly when it is anchored on an underline or leader landing.
- Dense bulged polylines that look like revision clouds are closed more reliably when the DXF closed flag is missing.
- This keeps note clouds and text closer to the way BricsCAD/AutoCAD display construction notes.
