cmake_minimum_required(VERSION 3.16)

project(MyBooksLibrary VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

# --- pdfbookmark ---
set(PDFBOOKMARK_SDK "C:/Users/mouli/Desktop/Dev/pdfbookmarkSdk" CACHE PATH "pdfbookmark SDK folder")
list(APPEND CMAKE_PREFIX_PATH "${PDFBOOKMARK_SDK}")
find_package(pdfbookmark 0.1 CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.10)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

qt_add_executable(appMyBooksLibrary
    main.cpp
)

qt_add_qml_module(appMyBooksLibrary
    URI MyBooksLibrary
    QML_FILES
        Main.qml
)

set_target_properties(appMyBooksLibrary PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appMyBooksLibrary
    PRIVATE Qt6::Quick pdfbookmark::pdfbookmark        # ← added
)
if(MSVC)
    target_compile_options(appMyBooksLibrary PRIVATE /utf-8)   # UTF-8 string literals
endif()

# Copies pdfbookmark.dll, its dependency DLLs and the OCR models next to the .exe
pdfbookmark_deploy_runtime(appMyBooksLibrary)          # ← added

include(GNUInstallDirs)
install(TARGETS appMyBooksLibrary
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)