#ifndef APPMENU_TYPES_H
#define APPMENU_TYPES_H

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVariantMap>

/**
 * com.canonical.dbusmenu 的布局项，D-Bus 签名 (ia{sv}av)
 *
 * 根节点 id 恒为 0；av 里的每个子项又是一个完整的 (ia{sv}av)，
 * 所以 >> 运算符需要递归解析。行为和 Qt 私有实现
 * qtbase/src/gui/platform/unix/dbusmenu/qdbusmenutypes.cpp 保持一致，
 * 这样 Qt 应用导出的菜单树可以被原样还原。
 */
struct DBusMenuLayoutItem
{
    qint32 id = 0;
    QVariantMap properties;
    QList<DBusMenuLayoutItem> children;
};

/// a(ia{sv}) 的元素：ItemsPropertiesUpdated / GetGroupProperties 使用
struct DBusMenuPropertySet
{
    qint32 id = 0;
    QVariantMap properties;
};

/**
 * 注册表里的一条记录，签名 (uuss)
 *
 * Wayland 下没有 X11 XID，windowId 直接复用 pid（和 hypr-appmenu 的
 * UnregisterWindow(pid) 语义一致），X11 客户端注册时才是真正的 XID。
 */
struct AppMenuEntry
{
    uint windowId = 0;
    uint pid = 0;
    QString service;
    QString path;
};

Q_DECLARE_METATYPE(DBusMenuLayoutItem)
Q_DECLARE_METATYPE(DBusMenuPropertySet)
Q_DECLARE_METATYPE(AppMenuEntry)

// 序列化运算符必须对模板容器（QList<T>、QDBusPendingReply<T>）可见
const QDBusArgument &operator>>(const QDBusArgument &arg, DBusMenuLayoutItem &item);
const QDBusArgument &operator<<(QDBusArgument &arg, const DBusMenuLayoutItem &item);

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusMenuPropertySet &set);
const QDBusArgument &operator<<(QDBusArgument &arg, const DBusMenuPropertySet &set);

const QDBusArgument &operator>>(const QDBusArgument &arg, AppMenuEntry &entry);
const QDBusArgument &operator<<(QDBusArgument &arg, const AppMenuEntry &entry);

/// 进程内幂等地注册自定义 D-Bus 类型（必须在 registerObject 之前调用）
void appmenuRegisterDBusTypes();

// 把 QVariant 里可能是注册类型、也可能是裸 QDBusArgument 的值转成 C++ 结构体。
// 应用重启 / 老版本 Qt 的签名差异都靠这层兜底。
DBusMenuLayoutItem appmenuLayoutItem(const QVariant &value);
QList<DBusMenuPropertySet> appmenuPropertySets(const QVariant &value);
AppMenuEntry appmenuEntry(const QVariant &value);
QList<AppMenuEntry> appmenuEntries(const QVariant &value);

#endif // APPMENU_TYPES_H
