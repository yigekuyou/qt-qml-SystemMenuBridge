#include "types.h"

#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDebug>

// ---------------------------------------------------------------------------
// (ia{sv}av) —— 菜单布局项
// ---------------------------------------------------------------------------

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusMenuLayoutItem &item)
{
    item = DBusMenuLayoutItem{};

    arg.beginStructure();
    arg >> item.id >> item.properties;

    arg.beginArray(); // av：每个元素是 variant<(ia{sv}av)>
    while (!arg.atEnd()) {
        QDBusVariant wrapped;
        arg >> wrapped;

        const QVariant inner = wrapped.variant();
        if (inner.metaType() == QMetaType::fromType<DBusMenuLayoutItem>()) {
            item.children.append(inner.value<DBusMenuLayoutItem>());
        } else if (inner.metaType() == QMetaType::fromType<QDBusArgument>()) {
            QDBusArgument childArg = inner.value<QDBusArgument>();
            DBusMenuLayoutItem child;
            childArg >> child;
            item.children.append(child);
        }
    }
    arg.endArray();

    arg.endStructure();
    return arg;
}

const QDBusArgument &operator<<(QDBusArgument &arg, const DBusMenuLayoutItem &item)
{
    arg.beginStructure();
    arg << item.id << item.properties;

    arg.beginArray(QMetaType::fromType<QDBusVariant>());
    for (const DBusMenuLayoutItem &child : item.children)
        arg << QDBusVariant(QVariant::fromValue(child));
    arg.endArray();

    arg.endStructure();
    return arg;
}

// ---------------------------------------------------------------------------
// (ia{sv}) —— 属性集合
// ---------------------------------------------------------------------------

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusMenuPropertySet &set)
{
    set = DBusMenuPropertySet{};
    arg.beginStructure();
    arg >> set.id >> set.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator<<(QDBusArgument &arg, const DBusMenuPropertySet &set)
{
    arg.beginStructure();
    arg << set.id << set.properties;
    arg.endStructure();
    return arg;
}

// ---------------------------------------------------------------------------
// (uuss) —— 注册表记录
// ---------------------------------------------------------------------------

const QDBusArgument &operator>>(const QDBusArgument &arg, AppMenuEntry &entry)
{
    entry = AppMenuEntry{};
    arg.beginStructure();
    arg >> entry.windowId >> entry.pid >> entry.service >> entry.path;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator<<(QDBusArgument &arg, const AppMenuEntry &entry)
{
    arg.beginStructure();
    arg << entry.windowId << entry.pid << entry.service << entry.path;
    arg.endStructure();
    return arg;
}

// ---------------------------------------------------------------------------

void appmenuRegisterDBusTypes()
{
    static const bool once = []() {
        // 结构体本身：签名由序列化运算符推导，例如 (ia{sv}av) / (uuss)
        qDBusRegisterMetaType<DBusMenuLayoutItem>();
        qDBusRegisterMetaType<DBusMenuPropertySet>();
        qDBusRegisterMetaType<AppMenuEntry>();

        // 容器类型必须显式注册，否则 Qt 无法为 a(uuss) / a(ia{sv}) / a(uo)
        // 推导签名，adaptor 里的方法会被静默丢弃（introspection 里看不到）。
        qDBusRegisterMetaType<QList<DBusMenuLayoutItem>>();
        qDBusRegisterMetaType<QList<DBusMenuPropertySet>>();
        qDBusRegisterMetaType<QList<AppMenuEntry>>();
        // GetMenus 的 a(uo)：QPair 本身也要先注册，容器才能推出签名
        qDBusRegisterMetaType<QPair<uint, QDBusObjectPath>>();
        qDBusRegisterMetaType<QList<QPair<uint, QDBusObjectPath>>>();
        return true;
    }();
    Q_UNUSED(once)
}

namespace {

/// QDBusArgument 里的签名可能是数组本身 "a(x)"，"beginArray() 之后"是元素 "(x)"
bool signatureMatches(const QDBusArgument &arg, const char *elementSignature)
{
    const QString sig = arg.currentSignature();
    return sig == QLatin1String(elementSignature)
           || sig == QLatin1String("a") + QLatin1String(elementSignature);
}

} // namespace

DBusMenuLayoutItem appmenuLayoutItem(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<DBusMenuLayoutItem>())
        return value.value<DBusMenuLayoutItem>();

    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        if (signatureMatches(arg, "(ia{sv}av)"))
            return qdbus_cast<DBusMenuLayoutItem>(arg);
    }
    return {};
}

QList<DBusMenuPropertySet> appmenuPropertySets(const QVariant &value)
{
    QList<DBusMenuPropertySet> list;

    if (value.metaType() == QMetaType::fromType<QList<DBusMenuPropertySet>>())
        return value.value<QList<DBusMenuPropertySet>>();

    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return list;

    const QDBusArgument arg = value.value<QDBusArgument>();
    if (!signatureMatches(arg, "(ia{sv})"))
        return list; // 例如 a(i)：没有属性名，交给调用者按 id 处理

    return qdbus_cast<QList<DBusMenuPropertySet>>(arg);
}

AppMenuEntry appmenuEntry(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<AppMenuEntry>())
        return value.value<AppMenuEntry>();

    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        if (signatureMatches(arg, "(uuss)"))
            return qdbus_cast<AppMenuEntry>(arg);
    }
    return {};
}

QList<AppMenuEntry> appmenuEntries(const QVariant &value)
{
    QList<AppMenuEntry> list;

    if (value.metaType() == QMetaType::fromType<QList<AppMenuEntry>>())
        return value.value<QList<AppMenuEntry>>();

    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return list;

    const QDBusArgument arg = value.value<QDBusArgument>();
    if (!signatureMatches(arg, "(uuss)"))
        return list;

    return qdbus_cast<QList<AppMenuEntry>>(arg);
}
