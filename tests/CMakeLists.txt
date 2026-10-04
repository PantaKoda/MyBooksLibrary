find_package(Qt6 REQUIRED COMPONENTS Test)

# Runs a test with Qt's DLLs on PATH so ctest works outside Qt Creator.
function(mbl_add_test name)
    qt_add_executable(${name} ${ARGN})
    # qt_add_executable defaults to the Windows GUI subsystem; tests need console output.
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE mbl_core Qt6::Test)
    # The Qt Test plain-text logger prints nothing on this Windows kit; JUnit XML to stdout does,
    # so ctest --output-on-failure shows the failing assertion.
    add_test(NAME ${name} COMMAND ${name} -o -,junitxml)
    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    endif()
endfunction()

# A test that calls the SDK needs its DLLs and OCR models next to it. The tests
# are built into the app's folder (qt_standard_project_setup sets
# CMAKE_RUNTIME_OUTPUT_DIRECTORY on Windows), so they wait for the app's one
# deployment there instead of each running its own: several file(COPY) runs
# into one folder at once raced, and the build failed copying pdfium.dll (CI on
# PR #36) or models/rec/charset.txt (a local Debug build for PR #33).
function(mbl_use_sdk_runtime name)
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        message(FATAL_ERROR "${name}: the tests must be built into the app's folder to use its SDK runtime.")
    endif()
    add_dependencies(${name} appMyBooksLibrary_pdfbookmark_runtime)
endfunction()

mbl_add_test(tst_sqlitecapabilities infrastructure/tst_sqlitecapabilities.cpp)
mbl_add_test(tst_databaseexecutor infrastructure/tst_databaseexecutor.cpp)
mbl_add_test(tst_ftsquery search/tst_ftsquery.cpp)
mbl_add_test(tst_migrations catalog/tst_migrations.cpp)
mbl_add_test(tst_catalog catalog/tst_catalog.cpp)
mbl_add_test(tst_libraryfolder catalog/tst_libraryfolder.cpp)
mbl_add_test(tst_tocedits catalog/tst_tocedits.cpp)
mbl_add_test(tst_organization catalog/tst_organization.cpp)
mbl_add_test(tst_exports catalog/tst_exports.cpp)
mbl_add_test(tst_exportplan domain/tst_exportplan.cpp)
mbl_add_test(tst_exportdestination storage/tst_exportdestination.cpp)
mbl_add_test(tst_backup storage/tst_backup.cpp)
target_compile_definitions(tst_backup PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
mbl_add_test(tst_importservice storage/tst_importservice.cpp)
target_compile_definitions(tst_importservice PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
mbl_add_test(tst_checkverdict reader/tst_checkverdict.cpp)
mbl_add_test(tst_librarycontroller presentation/tst_librarycontroller.cpp)
target_link_libraries(tst_librarycontroller PRIVATE mbl_presentation)
target_compile_definitions(tst_librarycontroller PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Links the SDK: runs next to the app, whose build deploys the SDK runtime there.
mbl_add_test(tst_sdksnapshot processing/tst_sdksnapshot.cpp)
target_link_libraries(tst_sdksnapshot PRIVATE mbl_sdk)
mbl_use_sdk_runtime(tst_sdksnapshot)
target_compile_definitions(tst_sdksnapshot PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_processingcoordinator processing/tst_processingcoordinator.cpp)
target_compile_definitions(tst_processingcoordinator PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_exportjobs processing/tst_exportjobs.cpp)
target_compile_definitions(tst_exportjobs PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_sdkmetadataextractor processing/tst_sdkmetadataextractor.cpp)
target_link_libraries(tst_sdkmetadataextractor PRIVATE mbl_sdk)
mbl_use_sdk_runtime(tst_sdkmetadataextractor)
target_compile_definitions(tst_sdkmetadataextractor PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_sdkcontentsanalyzer processing/tst_sdkcontentsanalyzer.cpp)
target_link_libraries(tst_sdkcontentsanalyzer PRIVATE mbl_sdk)
mbl_use_sdk_runtime(tst_sdkcontentsanalyzer)
target_compile_definitions(tst_sdkcontentsanalyzer PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_sdkbookexporter processing/tst_sdkbookexporter.cpp)
target_link_libraries(tst_sdkbookexporter PRIVATE mbl_sdk)
mbl_use_sdk_runtime(tst_sdkbookexporter)
target_compile_definitions(tst_sdkbookexporter PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_toctreemodel presentation/tst_toctreemodel.cpp)
target_link_libraries(tst_toctreemodel PRIVATE mbl_presentation)
mbl_add_test(tst_jobmodels presentation/tst_jobmodels.cpp)
target_link_libraries(tst_jobmodels PRIVATE mbl_presentation)

# Back up… and Restore…: the session on a real library, and the real BackupDialog.qml (offscreen).
mbl_add_test(tst_backupcontroller presentation/tst_backupcontroller.cpp)
target_link_libraries(tst_backupcontroller PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_backupcontroller PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                                                        MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# The Export dialog's session end to end with the real SDK, and the real
# ExportDialog.qml (offscreen).
mbl_add_test(tst_exportcontroller presentation/tst_exportcontroller.cpp)
target_link_libraries(tst_exportcontroller PRIVATE mbl_presentation mbl_presentationplugin mbl_sdk Qt6::Quick)
mbl_use_sdk_runtime(tst_exportcontroller)
target_compile_definitions(tst_exportcontroller PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                                                        MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Loads the real inspector QML (offscreen) against the presentation module.
mbl_add_test(tst_inspectorpane presentation/tst_inspectorpane.cpp)
target_link_libraries(tst_inspectorpane PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_inspectorpane PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Loads the real book list QML (offscreen) with a controller on imported fixtures.
mbl_add_test(tst_booklistview presentation/tst_booklistview.cpp)
target_link_libraries(tst_booklistview PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_booklistview PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                                                   MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# The views' Theme (UI overhaul PR 1): readable text colours in light and dark,
# an ordered type ramp, and no fixed colours or sizes left in the views.
mbl_add_test(tst_theme presentation/tst_theme.cpp)
target_link_libraries(tst_theme PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_theme PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# The theme and accent (Appearance) and the real AppearanceDialog.qml (offscreen).
mbl_add_test(tst_appearance presentation/tst_appearance.cpp)
target_link_libraries(tst_appearance PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_appearance PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Loads the real library sidebar QML (offscreen) against the presentation module.
mbl_add_test(tst_librarysidebar presentation/tst_librarysidebar.cpp)
target_link_libraries(tst_librarysidebar PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_librarysidebar PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Open library… (issue #30): the switcher on real folders, and the real
# OpenLibraryDialog.qml (offscreen). No process is started.
mbl_add_test(tst_libraryswitcher presentation/tst_libraryswitcher.cpp)
target_link_libraries(tst_libraryswitcher PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_libraryswitcher PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

mbl_add_test(tst_searchcontroller presentation/tst_searchcontroller.cpp)
target_link_libraries(tst_searchcontroller PRIVATE mbl_presentation)

# Loads the real search results QML (offscreen) against the presentation module.
mbl_add_test(tst_searchresultsview presentation/tst_searchresultsview.cpp)
target_link_libraries(tst_searchresultsview PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_searchresultsview PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

mbl_add_test(tst_readercontroller reader/tst_readercontroller.cpp)
target_link_libraries(tst_readercontroller PRIVATE mbl_presentation)

# The real reader QML with Qt PDF, offscreen, on imported fixtures: pages and
# repeated open/switch/close (docs/READER.md, "Teardown").
mbl_add_test(tst_readerpane reader/tst_readerpane.cpp)
target_link_libraries(tst_readerpane PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_readerpane PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
                           MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Updates (src/update/): releases, the downloader (Windows' tar.exe, local
# files) and the folder swap.
mbl_add_test(tst_releases update/tst_releases.cpp)
target_link_libraries(tst_releases PRIVATE mbl_update)
mbl_add_test(tst_updateinstall update/tst_updateinstall.cpp)
target_link_libraries(tst_updateinstall PRIVATE mbl_update)
mbl_add_test(tst_updatedownloader update/tst_updatedownloader.cpp)
target_link_libraries(tst_updatedownloader PRIVATE mbl_update)
# The update controller and the real UpdateDialog.qml (offscreen).
mbl_add_test(tst_updatecontroller presentation/tst_updatecontroller.cpp)
target_link_libraries(tst_updatecontroller PRIVATE mbl_presentation mbl_presentationplugin Qt6::Quick)
target_compile_definitions(tst_updatecontroller PRIVATE MBL_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
