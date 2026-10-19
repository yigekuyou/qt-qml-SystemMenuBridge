import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * 单层菜单弹窗；子菜单通过 Loader 再加载一个自己（文件级递归），
 * 因此任意层级菜单都能展开。
 */
Popup {
    id: root

    property var menuModel: null
    property int openedRow: -1

    width: 280
    padding: 0
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#2f3136"
        border.color: "#45484f"
        border.width: 1
        radius: 6
    }

    contentItem: ListView {
        id: list
        model: root.menuModel

        implicitHeight: contentHeight
        height: Math.min(contentHeight, 420)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        delegate: Item {
            id: rowItem
            width: list.width
            height: model.separator ? 9 : 30

            // 分隔线
            Rectangle {
                visible: model.separator
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                height: 1
                color: "#4b4e56"
            }

            // 悬停高亮
            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: 4
                color: hover.containsMouse && !model.separator ? "#3d4149" : "transparent"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 8
                visible: !model.separator

                Label {
                    visible: model.toggleType !== ""
                    text: model.toggleType === "radio"
                          ? (model.toggleState === 1 ? "◉" : "○")
                          : (model.toggleState === 1 ? "✓" : "")
                    color: "#9cdcfe"
                    font.pixelSize: 12
                }

                Label {
                    Layout.fillWidth: true
                    text: model.text
                    color: model.enabled ? "#e6e6e6" : "#7c8088"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Label {
                    visible: model.shortcut !== ""
                    text: model.shortcut
                    color: "#8d9199"
                    font.pixelSize: 11
                }

                Label {
                    visible: model.hasSubMenu
                    text: "▸"
                    color: "#8d9199"
                    font.pixelSize: 12
                }
            }

            MouseArea {
                id: hover
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                enabled: !model.separator && model.enabled

                onClicked: {
                    if (model.hasSubMenu) {
                        const sub = root.menuModel.subMenu(index)
                        if (!sub)
                            return
                        root.openedRow = index
                        root.menuModel.subMenuOpened(index)

                        if (!childLoader.active)
                            childLoader.active = true
                        const popup = childLoader.item
                        if (!popup)
                            return
                        popup.menuModel = sub
                        popup.parent = rowItem
                        popup.x = rowItem.width - 8
                        popup.y = 0
                        popup.open()
                    } else {
                        root.menuModel.activate(index)
                        root.close()
                    }
                }
            }
        }
    }

    // 子菜单（文件级递归；不在 delegate 里创建，避免列表滚动时被销毁）
    Loader {
        id: childLoader
        active: false          // 按需实例化，避免无限递归
        source: "MenuPopup.qml"
    }

    Connections {
        target: childLoader.item
        ignoreUnknownSignals: true
        function onClosed() {
            if (root.openedRow >= 0 && root.menuModel)
                root.menuModel.subMenuClosed(root.openedRow)
            root.openedRow = -1
        }
    }
}
