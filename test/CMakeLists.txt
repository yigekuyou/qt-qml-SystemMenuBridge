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
