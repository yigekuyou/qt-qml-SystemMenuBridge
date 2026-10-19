# ---------------------------------------------------------------------------
# 公共部分：dbusmenu 的线格式结构体 + 解析工具
# ---------------------------------------------------------------------------
add_library(appmenu-common STATIC
    types.h
    types.cpp
)
target_link_libraries(appmenu-common PUBLIC Qt6::Core Qt6::DBus)
target_include_directories(appmenu-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(appmenu-common PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# QML 模块 com.github.yigekuyou.appmenu
#   对外暴露：DBusMenuModel（一层菜单）、AppMenuService（pid → 菜单绑定）
# ---------------------------------------------------------------------------
qt_add_library(appmenu SHARED)

qt_add_qml_module(appmenu
    URI com.github.yigekuyou.appmenu
    VERSION 1.0
    SOURCES
        dbusmenumodel.h
        dbusmenumodel.cpp
        appmenuservice.h
        appmenuservice.cpp
)

target_link_libraries(appmenu
    PRIVATE
        Qt6::Qml
        appmenu-common
)

# ---------------------------------------------------------------------------
# registrar 守护进程
# ---------------------------------------------------------------------------
qt_add_executable(appmenu-registrar
    main.cpp
    registrar.h
    registrar.cpp
)
target_link_libraries(appmenu-registrar PRIVATE Qt6::Core Qt6::DBus appmenu-common)

include(GNUInstallDirs)
install(TARGETS appmenu-registrar
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
