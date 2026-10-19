import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import com.github.yigekuyou.appmenu

Window {
    id: window
    width: 1100
    height: 480
    visible: true
    color: "#1b1c1f"
    title: "全局菜单 / com.github.yigekuyou.appmenu"

    AppMenuService {
        id: appMenu

        // 演示用：没有指定 pid 且还没绑定时，自动选第一个发现的应用
        onMenusChanged: {
            if (window.requestedPid === 0 && activePid === 0 && menus.length > 0)
                activatePid(menus[0].pid)
        }

    }

    MenuPopup {
        id: menuPopup
    }

    // 演示 / 截图模式：`appmenu-test --demo` 自动展开第一个带子菜单的项
    readonly property bool demoMode: Qt.application.arguments.indexOf("--demo") >= 0
    property bool demoDone: false
    property int requestedPid: 0

    // 调试：`appmenu-test --pid 1234` 直接把菜单绑定到某个进程
    Component.onCompleted: {
        const args = Qt.application.arguments
        const index = args.indexOf("--pid")
        if (index >= 0 && args.length > index + 1) {
            const pid = parseInt(args[index + 1])
            if (pid > 0) {
                window.requestedPid = pid
                appMenu.activatePid(pid)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------- 顶栏 ----------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#26272b"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 2

                Label {
                    text: "🌐"
                    font.pixelSize: 15
                    Layout.rightMargin: 6
                }

                Repeater {
                    model: appMenu.menuModel

                    delegate: ItemDelegate {
                        id: menuButton
                        text: model.text.length > 0 ? model.text : "…"
                        font.pixelSize: 13
                        implicitHeight: 30
                        leftPadding: 10
                        rightPadding: 10
                        enabled: model.enabled
                        opacity: model.enabled ? 1.0 : 0.5

                        background: Rectangle {
                            radius: 5
                            color: menuButton.hovered ? "#3a3d44" : "transparent"
                        }

                        Component.onCompleted: {
                            if (window.demoMode && !window.demoDone && model.hasSubMenu) {
                                window.demoDone = true
                                demoTimer.start()
                            }
                        }

                        Timer {
                            id: demoTimer
                            interval: 400
                            onTriggered: menuButton.clicked()
                        }

                        onClicked: {
                            if (model.hasSubMenu) {
                                const sub = appMenu.menuModel.subMenu(index)
                                if (!sub)
                                    return
                                appMenu.menuModel.subMenuOpened(index)
                                menuPopup.menuModel = sub
                                menuPopup.parent = menuButton
                                menuPopup.x = 0
                                menuPopup.y = menuButton.height
                                menuPopup.open()
                            } else {
                                appMenu.menuModel.activate(index)
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: appMenu.activePid > 0
                          ? "pid " + appMenu.activePid + " · " + appMenu.activePath
                          : "未绑定窗口"
                    color: "#8d9199"
                    font.pixelSize: 11
                }

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    Layout.leftMargin: 10
                    color: appMenu.available ? "#7ddc7d" : "#ff7b72"
                }

                Label {
                    text: appMenu.available ? "registrar 在线" : "registrar 离线"
                    color: "#8d9199"
                    font.pixelSize: 11
                }
            }
        }

        // ---------------- 内容区 ----------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Label {
                    text: "已发现的全局菜单"
                    color: "#c9cdd4"
                    font.pixelSize: 13
                    font.bold: true
                }

                Label {
                    text: "窗口切换时调用 appMenu.activatePid(窗口 pid) 即可切换顶栏菜单。"
                    color: "#7c8088"
                    font.pixelSize: 11
                }

                ListView {
                    id: menuList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: appMenu.menus

                    delegate: Rectangle {
                        width: menuList.width
                        height: 46
                        radius: 6
                        color: modelData.pid === appMenu.activePid ? "#2d4763" : "#242529"
                        border.color: modelData.pid === appMenu.activePid ? "#4b7bb5" : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 18

                            Label {
                                text: "window " + modelData.windowId
                                color: "#e6e6e6"
                                font.pixelSize: 12
                                Layout.preferredWidth: 110
                            }
                            Label {
                                text: "pid " + modelData.pid
                                color: "#e6e6e6"
                                font.pixelSize: 12
                                Layout.preferredWidth: 90
                            }
                            Label {
                                text: modelData.service
                                color: "#9cdcfe"
                                font.pixelSize: 12
                                Layout.preferredWidth: 150
                                elide: Text.ElideRight
                            }
                            Label {
                                text: modelData.path
                                color: "#c9cdd4"
                                font.pixelSize: 12
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Button {
                                text: "设为活动"
                                font.pixelSize: 11
                                onClicked: appMenu.activatePid(modelData.pid)
                            }
                        }
                    }
                }

                Label {
                    visible: appMenu.menus.length === 0
                    text: "还没有发现任何菜单：\n"
                          + "1) 启动 appmenu-registrar\n"
                          + "2) 启动一个导出 com.canonical.dbusmenu 的应用\n"
                          + "   （没有真实应用时可以跑 appmenu-fakeapp）"
                    color: "#7c8088"
                    font.pixelSize: 12
                    lineHeight: 1.4
                }
            }
        }
    }
}
