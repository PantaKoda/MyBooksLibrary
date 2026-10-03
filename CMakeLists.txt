cmake_minimum_required(VERSION 3.16)

project(MyBooksLibrary VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Sql Pdf)

# --- pdfbookmark SDK ---
# Set PDFBOOKMARK_SDK as a cache variable (-DPDFBOOKMARK_SDK=..., a CMakeUserPresets.json,
# or the Qt Creator kit) or as an environment variable. A nonempty cache value wins; an empty
# one (e.g. left by an earlier failed configure) is filled from the environment.
# See docs/BUILDING.md.
set(PDFBOOKMARK_SDK "" CACHE PATH "Installed pdfbookmark SDK folder")
if(NOT PDFBOOKMARK_SDK AND NOT "$ENV{PDFBOOKMARK_SDK}" STREQUAL "")
    set(PDFBOOKMARK_SDK "$ENV{PDFBOOKMARK_SDK}" CACHE PATH "Installed pdfbookmark SDK folder" FORCE)
endif()
if(NOT PDFBOOKMARK_SDK)
    message(FATAL_ERROR
        "PDFBOOKMARK_SDK is not set. Pass -DPDFBOOKMARK_SDK=<installed SDK folder> "
        "or set the PDFBOOKMARK_SDK environment variable (see docs/BUILDING.md).")
endif()
list(APPEND CMAKE_PREFIX_PATH "${PDFBOOKMARK_SDK}")
find_package(pdfbookmark 0.4 CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

# Application code without the GUI or the SDK, shared by the app and the tests.
qt_add_library(mbl_core STATIC
    src/domain/book.h
    src/domain/codes.cpp
    src/domain/collection.h
    src/domain/ids.h
    src/domain/importing.h
    src/domain/jobs.h
    src/domain/metadata.h
    src/domain/metadata.cpp
    src/domain/export.h
    src/domain/export.cpp
    src/domain/result.h
    src/domain/search.h
    src/domain/toc.h
    src/infrastructure/databaseexecutor.h
    src/infrastructure/databaseexecutor.cpp
    src/infrastructure/memorystats.h
    src/infrastructure/memorystats.cpp
    src/infrastructure/sqlitecapabilities.h
    src/infrastructure/sqlitecapabilities.cpp
    src/catalog/catalog.h
    src/catalog/catalog.cpp
    src/catalog/catalog_internal.h
    src/catalog/imports.h
    src/catalog/imports.cpp
    src/catalog/jobs.h
    src/catalog/jobs.cpp
    src/catalog/exports.h
    src/catalog/exports.cpp
    src/catalog/backup.h
    src/catalog/backup.cpp
    src/catalog/reading.h
    src/catalog/reading.cpp
    src/catalog/tocedits.h
    src/catalog/tocedits.cpp
    src/catalog/collections.h
    src/catalog/collections.cpp
    src/catalog/library.h
    src/catalog/library.cpp
    src/catalog/migrations.h
    src/catalog/migrations.cpp
    src/processing/bookexporter.h
    src/processing/contentsanalyzer.h
    src/processing/metadataextractor.h
    src/processing/processingcoordinator.h
    src/processing/processingcoordinator.cpp
    src/reader/checkverdict.h
    src/reader/checkverdict.cpp
    src/search/ftsquery.h
    src/search/ftsquery.cpp
    src/search/searchindex.h
    src/search/searchindex.cpp
    src/storage/exportdestination.h
    src/storage/exportdestination.cpp
    src/storage/backup.h
    src/storage/backup.cpp
    src/storage/filecopy.h
    src/storage/filecopy.cpp
    src/storage/importservice.h
    src/storage/importservice.cpp
    src/storage/librarylayout.h
    src/storage/librarylayout.cpp
    src/storage/reportstore.h
    src/storage/reportstore.cpp
)
target_include_directories(mbl_core PUBLIC src)
target_link_libraries(mbl_core PUBLIC Qt6::Sql)
if(MSVC)
    target_compile_options(mbl_core PUBLIC /utf-8)   # UTF-8 string literals
endif()
if(WIN32)
    target_link_libraries(mbl_core PRIVATE psapi)
endif()

# Presentation (GUI-owned models and controllers) and the composition helpers.
qt_add_library(mbl_presentation STATIC
    src/app/librarymemory.h
    src/app/librarymemory.cpp
    src/app/libraryroot.h
    src/app/libraryroot.cpp
    src/presentation/bookinspector.h
    src/presentation/bookinspector.cpp
    src/presentation/booklistmodel.h
    src/presentation/booklistmodel.cpp
    src/presentation/collectionlistmodel.h
    src/presentation/collectionlistmodel.cpp
    src/presentation/exportcontroller.h
    src/presentation/exportcontroller.cpp
    src/presentation/backupcontroller.h
    src/presentation/backupcontroller.cpp
    src/presentation/joblistmodel.h
    src/presentation/joblistmodel.cpp
    src/presentation/toctreemodel.h
    src/presentation/toctreemodel.cpp
    src/presentation/librarycontroller.h
    src/presentation/librarycontroller.cpp
    src/presentation/libraryswitcher.h
    src/presentation/libraryswitcher.cpp
    src/presentation/searchcontroller.h
    src/presentation/searchcontroller.cpp
    src/presentation/searchresultsmodel.h
    src/presentation/searchresultsmodel.cpp
    src/reader/readercontroller.h
    src/reader/readercontroller.cpp
)
# Theme: the views' shared sizes, spacing and colours (docs/UI.md, "Theme"). In
# this module, which every view and every QML test already imports.
set_source_files_properties(qml/theme/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
    QT_RESOURCE_ALIAS Theme.qml
)
qt_add_qml_module(mbl_presentation
    URI MyBooksLibrary.Presentation
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/MyBooksLibrary/Presentation
    QML_FILES
        qml/theme/Theme.qml
)
target_link_libraries(mbl_presentation PUBLIC mbl_core Qt6::Core Qt6::Qml Qt6::Quick)
# qmltyperegistrar includes registered headers by bare file name.
target_include_directories(mbl_presentation PRIVATE src/presentation)

# The production SDK boundary. SDK headers and types stay behind it.
qt_add_library(mbl_sdk STATIC
    src/processing/sdk/sdkinfo.h
    src/processing/sdk/sdkinfo.cpp
    src/processing/sdk/sdksnapshot.h
    src/processing/sdk/sdksnapshot.cpp
    src/processing/sdk/metadatanormalize.h
    src/processing/sdk/metadatanormalize.cpp
    src/processing/sdk/sdkmetadataextractor.h
    src/processing/sdk/sdkmetadataextractor.cpp
    src/processing/sdk/sdkcommon.h
    src/processing/sdk/sdkcommon.cpp
    src/processing/sdk/contentsnormalize.h
    src/processing/sdk/contentsnormalize.cpp
    src/processing/sdk/pagelabels.h
    src/processing/sdk/pagelabels.cpp
    src/processing/sdk/sdkcontentsanalyzer.h
    src/processing/sdk/sdkcontentsanalyzer.cpp
    src/processing/sdk/sdkbookexporter.h
    src/processing/sdk/sdkbookexporter.cpp
)
target_link_libraries(mbl_sdk PUBLIC mbl_core pdfbookmark::pdfbookmark PRIVATE Qt6::Pdf)

qt_add_executable(appMyBooksLibrary
    main.cpp
    src/reader/readercheck.h
    src/reader/readercheck.cpp
)

qt_add_qml_module(appMyBooksLibrary
    URI MyBooksLibrary
    QML_FILES
        Main.qml
        qml/reader/ReaderCheckView.qml
        qml/inspector/BookInspectorPane.qml
        qml/inspector/MetadataFieldEditor.qml
        qml/library/LibrarySidebar.qml
        qml/library/BookListView.qml
        qml/library/OpenLibraryDialog.qml
        qml/search/SearchResultsView.qml
        qml/reader/ReaderPane.qml
        qml/export/ExportDialog.qml
        qml/backup/BackupDialog.qml
)

# The window's Qt Quick Controls style: FluentWinUI3 (docs/UI.md, "Theme").
# QT_QUICK_CONTROLS_STYLE and -style still override it; the QML tests set Basic.
qt_add_resources(appMyBooksLibrary "controls_style"
    PREFIX "/"
    FILES qtquickcontrols2.conf
)

set_target_properties(appMyBooksLibrary PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appMyBooksLibrary
    PRIVATE Qt6::Quick Qt6::Pdf mbl_core mbl_presentation mbl_presentationplugin mbl_sdk
)

# Copies pdfbookmark.dll, its dependency DLLs and the OCR models next to the .exe
pdfbookmark_deploy_runtime(appMyBooksLibrary)

option(MBL_BUILD_TESTS "Build the MyBooksLibrary tests" ON)
if(MBL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS appMyBooksLibrary
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)