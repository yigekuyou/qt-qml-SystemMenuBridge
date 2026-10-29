# ---------------------------------------------------------------------------
# appmenu-fakeapp：只导出 com.canonical.dbusmenu 的假应用（链路测试用）
# 直接链接 QML 模块库，复用 types.h 里的线格式结构体
# ---------------------------------------------------------------------------
qt_add_executable(appmenu-fakeapp
    fakeapp.cpp
)
target_link_libraries(appmenu-fakeapp PRIVATE Qt6::Core Qt6::DBus appmenu)
target_include_directories(appmenu-fakeapp PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(appmenu-fakeapp PROPERTIES AUTOMOC ON)

# ---------------------------------------------------------------------------
# appmenu-test：顶栏演示程序（唯一用到 .qml 的地方）
# ---------------------------------------------------------------------------
qt_add_executable(appmenu-test
    main.cpp
)

qt_add_qml_module(appmenu-test
    URI yigekuyou.appmenu.test
    VERSION 1.0
    QML_FILES
        Main.qml
        MenuPopup.qml
)

target_link_libraries(appmenu-test PRIVATE Qt6::Quick Qt6::Qml appmenu)

# appmenu-test 直接链接了模块库（类型已注册），只需要自己的 QML 模块导入路径
target_compile_definitions(appmenu-test PRIVATE
    APPMENU_QML_IMPORT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

# ---------------------------------------------------------------------------
# appmenu-bind-test：托盘菜单 vs 应用菜单栏的回归测试
# 直接使用 AppMenuService（不需要 QML），由 fakeapp --tray 制造两份同 pid 菜单
# ---------------------------------------------------------------------------
qt_add_executable(appmenu-bind-test
    bindtest.cpp
)
target_link_libraries(appmenu-bind-test PRIVATE Qt6::Core Qt6::DBus Qt6::Qml appmenu)
target_include_directories(appmenu-bind-test PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(appmenu-bind-test PROPERTIES AUTOMOC ON)

# 用独立的 dbus-daemon，避免和桌面上已有的 registrar（Quickshell / KDE）冲突
find_program(DBUS_RUN_SESSION dbus-run-session)
if(DBUS_RUN_SESSION)
    add_test(NAME appmenu-tray-vs-menubar
        COMMAND ${DBUS_RUN_SESSION} --
                $<TARGET_FILE:appmenu-bind-test> $<TARGET_FILE:appmenu-fakeapp>
    )
endif()
