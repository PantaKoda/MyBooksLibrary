find_package(Qt6 REQUIRED COMPONENTS Test)

# Runs a test with Qt's DLLs on PATH so ctest works outside Qt Creator.
function(mbl_add_test name)
    qt_add_executable(${name} ${ARGN})
    # qt_add_executable defaults to the Windows GUI subsystem; tests need console output.
    set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(${name} PRIVATE mbl_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    endif()
endfunction()

mbl_add_test(tst_sqlitecapabilities infrastructure/tst_sqlitecapabilities.cpp)
