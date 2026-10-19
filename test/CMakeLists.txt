# ---------------------------------------------------------------------------
# appmenu-fakeapp：只导出 com.canonical.dbusmenu 的假应用（链路测试用）
# ---------------------------------------------------------------------------
qt_add_executable(appmenu-fakeapp
    fakeapp.cpp
)
target_link_libraries(appmenu-fakeapp PRIVATE Qt6::Core Qt6::DBus appmenu-common)
set_target_properties(appmenu-fakeapp PROPERTIES AUTOMOC ON)

# ---------------------------------------------------------------------------
# appmenu-test：顶栏演示程序
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

# QML 模块（com.github.yigekuyou.appmenu）在构建树里的位置
target_compile_definitions(appmenu-test PRIVATE
    APPMENU_QML_IMPORT_DIR="${CMAKE_BINARY_DIR}/qml"
)
