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

mbl_add_test(tst_sqlitecapabilities infrastructure/tst_sqlitecapabilities.cpp)
mbl_add_test(tst_databaseexecutor infrastructure/tst_databaseexecutor.cpp)
mbl_add_test(tst_ftsquery search/tst_ftsquery.cpp)
mbl_add_test(tst_migrations catalog/tst_migrations.cpp)
mbl_add_test(tst_catalog catalog/tst_catalog.cpp)
mbl_add_test(tst_importservice storage/tst_importservice.cpp)
target_compile_definitions(tst_importservice PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
mbl_add_test(tst_checkverdict reader/tst_checkverdict.cpp)
mbl_add_test(tst_librarycontroller presentation/tst_librarycontroller.cpp)
target_link_libraries(tst_librarycontroller PRIVATE mbl_presentation)
target_compile_definitions(tst_librarycontroller PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# Links the SDK: runs next to the app, whose build deploys the SDK runtime there;
# deploying again for this target keeps it runnable on its own.
mbl_add_test(tst_sdksnapshot processing/tst_sdksnapshot.cpp)
target_link_libraries(tst_sdksnapshot PRIVATE mbl_sdk)
pdfbookmark_deploy_runtime(tst_sdksnapshot)
target_compile_definitions(tst_sdksnapshot PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_processingcoordinator processing/tst_processingcoordinator.cpp)
target_compile_definitions(tst_processingcoordinator PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_sdkmetadataextractor processing/tst_sdkmetadataextractor.cpp)
target_link_libraries(tst_sdkmetadataextractor PRIVATE mbl_sdk)
pdfbookmark_deploy_runtime(tst_sdkmetadataextractor)
target_compile_definitions(tst_sdkmetadataextractor PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

mbl_add_test(tst_sdkcontentsanalyzer processing/tst_sdkcontentsanalyzer.cpp)
target_link_libraries(tst_sdkcontentsanalyzer PRIVATE mbl_sdk)
pdfbookmark_deploy_runtime(tst_sdkcontentsanalyzer)
target_compile_definitions(tst_sdkcontentsanalyzer PRIVATE MBL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
