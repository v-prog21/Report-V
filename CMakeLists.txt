cmake_minimum_required(VERSION 3.16)
project(BugTracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(BugTracker
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/reportmanager.cpp
    src/reportmanager.h
    src/reportmodel.cpp
    src/reportmodel.h
    src/settingsmanager.cpp
    src/settingsmanager.h
    resources/style.qss
    resources/resources.qrc
)

target_link_libraries(BugTracker PRIVATE Qt6::Widgets Qt6::Network)