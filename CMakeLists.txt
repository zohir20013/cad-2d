cmake_minimum_required(VERSION 3.16)

project(DWGViewerAdvanced
    VERSION 1.0.0
    DESCRIPTION "Qt6 DWG/DXF CAD viewer and lightweight 2D editor"
    LANGUAGES CXX
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Svg
    PrintSupport
)

add_library(DxfLibrary STATIC
    src/dxf_library/DxfLibrary.cpp
    src/dxf_library/DxfLibrary.h
)

target_include_directories(DxfLibrary PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dxf_library
)

target_link_libraries(DxfLibrary PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

add_library(CadGeometry STATIC
    src/geometry/GeometryKernel.cpp
    src/geometry/GeometryKernel.h
    src/geometry/GeometryKernelAdvanced.cpp
    src/geometry/GeometryKernelAdvanced.h
    src/constraints/GeometricConstraintSolver.cpp
    src/constraints/GeometricConstraintSolver.h
    src/annotation/CadAnnotationEngine.cpp
    src/annotation/CadAnnotationEngine.h
)

target_include_directories(CadGeometry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/geometry
    ${CMAKE_CURRENT_SOURCE_DIR}/src/constraints
    ${CMAKE_CURRENT_SOURCE_DIR}/src/annotation
)

target_link_libraries(CadGeometry PUBLIC
    Qt6::Core
    Qt6::Gui
)

add_library(CadImportLibrary STATIC
    src/cad_import/CadImportLibrary.cpp
    src/cad_import/CadImportLibrary.h
    src/cad_import/OdaFileConverter.cpp
    src/cad_import/OdaFileConverter.h
)

target_include_directories(CadImportLibrary PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cad_import
)

target_link_libraries(CadImportLibrary PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    DxfLibrary
)

add_executable(DWGViewerAdvanced
    resources/icons.qrc
    main.cpp
    src/DebugLogger.cpp
    src/DebugLogger.h
    src/DxfLoader.cpp
    src/DxfLoader.h
    src/GraphicsView.cpp
    src/GraphicsView.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/cad/CadDocument.cpp
    src/cad/CadDocument.h
    src/cad/CadEntity.cpp
    src/cad/CadEntity.h
    src/cad/CadLayer.cpp
    src/cad/CadLayer.h
    src/cad/CadRenderStyle.h
    src/cad/LayerInfo.h
    src/core/CadEntityUtils.cpp
    src/core/CadEntityUtils.h
    src/selection/CadSelectionEngine.cpp
    src/selection/CadSelectionEngine.h
    src/layers/CadLayerStateManager.cpp
    src/layers/CadLayerStateManager.h
    src/history/CadCommandHistory.cpp
    src/history/CadCommandHistory.h
    src/quality/CadAuditEngine.cpp
    src/quality/CadAuditEngine.h
    src/blocks/CadBlockAttributeEngine.cpp
    src/blocks/CadBlockAttributeEngine.h
    src/plot/CadPlotLayoutEngine.cpp
    src/plot/CadPlotLayoutEngine.h
    src/commands/CadCommandLineEngine.cpp
    src/commands/CadCommandLineEngine.h
    src/measure/CadMeasurementEngine.cpp
    src/measure/CadMeasurementEngine.h
    src/view/CadViewStateManager.cpp
    src/view/CadViewStateManager.h
    src/settings/CadUserPreferences.cpp
    src/settings/CadUserPreferences.h
    src/standards/CadStandardsManager.cpp
    src/standards/CadStandardsManager.h
    src/modify/CadAdvancedModifyEngine.cpp
    src/modify/CadAdvancedModifyEngine.h
    src/snap/CadSmartSnapEngine.cpp
    src/snap/CadSmartSnapEngine.h
    src/styles/CadStyleManager.cpp
    src/styles/CadStyleManager.h
    src/recovery/CadFileSafetyManager.cpp
    src/recovery/CadFileSafetyManager.h
    src/palettes/CadToolPalette.cpp
    src/palettes/CadToolPalette.h
    src/print/CadSheetSetManager.cpp
    src/print/CadSheetSetManager.h
)

target_include_directories(DWGViewerAdvanced PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DWGViewerAdvanced PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::PrintSupport
    CadGeometry
    CadImportLibrary
    DxfLibrary
)

if(MSVC)
    target_compile_options(DWGViewerAdvanced PRIVATE /W4)
else()
    target_compile_options(DWGViewerAdvanced PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS DWGViewerAdvanced
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

