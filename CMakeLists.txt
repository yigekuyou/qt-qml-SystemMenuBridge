cmake_minimum_required(VERSION 3.21)

project(appmenu VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Qml Quick)

qt_standard_project_setup()

# QML 模块统一输出到 <build>/qml，运行时用 QML_IMPORT_PATH 指过去即可
# （test/main.cpp 里已经通过 ${CMAKE_BINARY_DIR}/qml 自动加好了）
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

add_subdirectory(src)
add_subdirectory(test)
