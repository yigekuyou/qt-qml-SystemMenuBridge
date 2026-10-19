cmake_minimum_required(VERSION 3.21)

project(appmenu VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core DBus Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------------------
# 输出目录 / 安装目录
# ---------------------------------------------------------------------------
set(APPMENU_BUILD_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/qml"
    CACHE PATH "Directory for generated QML module files")

set(APPMENU_QML_DIR
    "${CMAKE_INSTALL_LIBDIR}/qt6/qml/com/github/yigekuyou/appmenu"
    CACHE PATH "Install directory for the QML module")

# ---------------------------------------------------------------------------
# QML 模块
#
# 这个工程只提供 QML 接口，没有可执行文件：
#   DBusMenuModel  一层菜单模型（com.canonical.dbusmenu 客户端）
#   AppMenuService pid → 菜单绑定
#   AppMenu        QML 单例（C++ 实现，工程里没有 .qml 文件）
#
# registrar（com.canonical.AppMenu.Registrar）不是独立进程：AppMenuService
# 启动时如果这个名字没人占，就在本进程里占住，顺带做菜单发现。
# ---------------------------------------------------------------------------
set(APPMENU_URI "com.github.yigekuyou.appmenu")

qt_add_library(appmenu SHARED)

qt_add_qml_module(appmenu
    URI ${APPMENU_URI}
    VERSION 1.0
    OUTPUT_DIRECTORY ${APPMENU_BUILD_DIR}
    DEPENDENCIES QtQml
    SOURCES
        src/types.h          src/types.cpp
        src/dbusmenumodel.h  src/dbusmenumodel.cpp
        src/appmenuservice.h src/appmenuservice.cpp
        src/registrar.h      src/registrar.cpp
        src/appmenu.h        src/appmenu.cpp
)

target_include_directories(appmenu PRIVATE src)
target_link_libraries(appmenu PRIVATE Qt6::Core Qt6::DBus Qt6::Qml Qt6::Quick)

set_target_properties(appmenu PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

# 主库和插件都装到同一个 QML 目录，插件通过 $ORIGIN 找主库
set_target_properties(appmenu appmenuplugin PROPERTIES
    INSTALL_RPATH "$ORIGIN"
)

# ---------------------------------------------------------------------------
# 安装
# ---------------------------------------------------------------------------
install(TARGETS appmenu appmenuplugin
    RUNTIME DESTINATION ${APPMENU_QML_DIR}
    LIBRARY DESTINATION ${APPMENU_QML_DIR}
    ARCHIVE DESTINATION ${APPMENU_QML_DIR}
)

install(FILES
    ${APPMENU_BUILD_DIR}/qmldir
    ${APPMENU_BUILD_DIR}/appmenu.qmltypes
    DESTINATION ${APPMENU_QML_DIR}
)

# ---------------------------------------------------------------------------
# 测试 / 演示（可关掉：-DAPPMENU_BUILD_TESTS=OFF）
# ---------------------------------------------------------------------------
option(APPMENU_BUILD_TESTS "构建 test/ 下的演示程序与假应用" ON)

if(APPMENU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()