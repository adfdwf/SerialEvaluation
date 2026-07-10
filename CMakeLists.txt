cmake_minimum_required(VERSION 3.16)

project(CommBenchPro VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network SerialPort)

set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/mainwindow.ui
    src/namespace.h
    src/icommunicationinterface.h
    src/tcpclientworker.h
    src/tcpclientworker.cpp
    src/serialclientworker.h
    src/serialclientworker.cpp
    src/statisticsmanager.h
    src/statisticsmanager.cpp
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_executable(CommBenchPro
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(CommBenchPro ${PROJECT_SOURCES})
endif()

target_link_libraries(CommBenchPro PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::SerialPort
)

# Make generated UI headers visible to Visual Studio IntelliSense and builds.
target_include_directories(CommBenchPro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/CommBenchPro_autogen/include
    ${CMAKE_CURRENT_BINARY_DIR}/CommBenchPro_autogen/include_Debug
    ${CMAKE_CURRENT_BINARY_DIR}/CommBenchPro_autogen/include_Release
    ${CMAKE_CURRENT_BINARY_DIR}/CommBenchPro_autogen/include_RelWithDebInfo
    ${CMAKE_CURRENT_BINARY_DIR}/CommBenchPro_autogen/include_MinSizeRel
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(CommBenchPro)
endif()