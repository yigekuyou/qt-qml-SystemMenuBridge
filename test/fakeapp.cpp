/**
 * appmenu-fakeapp —— 一个只导出 com.canonical.dbusmenu 的"假应用"
 *
 * 用途：在没有真实 Qt/Chromium/Firefox 应用时验证整条链路：
 *   内嵌 registrar 发现 /MenuBar/1 → AppMenuService 按 pid 绑定 → QML 渲染。
 *
 * --menus N 会在同一条总线上导出 /MenuBar/1 .. /MenuBar/N，模拟一个进程开
 * 多个窗口；/Control 上的 DropMenu(path) 可以在运行中"关掉一个窗口"。
 *
 * 它不创建任何窗口（QCoreApplication），菜单树写死在代码里，
 * 点击事件会打印到 stdout，勾选项会真的切换并广播 LayoutUpdated。
 */
#include "types.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QHash>
#include <QList>
#include <QTimer>
#include <QDebug>

#include <functional>

namespace {

struct FakeItem
{
    qint32 id = 0;
    QString label;
    bool separator = false;
    bool enabled = true;
    QString toggleType; // "" / "checkmark" / "radio"
    int toggleState = 0;
    QList<FakeItem> children;
};

FakeItem makeSeparator(qint32 id)
{
    FakeItem item;
    item.id = id;
    item.separator = true;
    return item;
}

QList<FakeItem> buildTree(int windowIndex = 0)
{
    FakeItem file;
    file.id = 1;
    // 多窗口测试时用窗口序号区分，方便肉眼确认绑定到了哪个 /MenuBar/<n>
    file.label = windowIndex > 0 ? QStringLiteral("_File %1").arg(windowIndex)
                                 : QStringLiteral("_File");
    file.children = {
        {10, QStringLiteral("_New Window"), false, true, {}, 0, {}},
        {11, QStringLiteral("_Open…"), false, true, {}, 0, {}},
        makeSeparator(12),
        {13, QStringLiteral("_Quit"), false, true, {}, 0, {}},
    };

    FakeItem edit;
    edit.id = 2;
    edit.label = QStringLiteral("_Edit");
    edit.children = {
        {20, QStringLiteral("_Undo"), false, true, {}, 0, {}},
        {21, QStringLiteral("_Redo"), false, false, {}, 0, {}}, // 故意禁用
        makeSeparator(22),
        {23, QStringLiteral("Cu_t"), false, true, {}, 0, {}},
        {24, QStringLiteral("_Paste"), false, true, {}, 0, {}},
    };

    FakeItem view;
    view.id = 3;
    view.label = QStringLiteral("_View");
    view.children = {
        {30, QStringLiteral("Show _Toolbar"), false, true, QStringLiteral("checkmark"), 1, {}},
        {31, QStringLiteral("Show _Statusbar"), false, true, QStringLiteral("checkmark"), 0, {}},
        makeSeparator(32),
        {33, QStringLiteral("_Theme"), false, true, QStringLiteral("radio"), 1, {}},
    };
    // Theme 下面再挂一层，顺便验证多级子菜单
    view.children[3].children = {
        {40, QStringLiteral("_Light"), false, true, QStringLiteral("radio"), 1, {}},
        {41, QStringLiteral("_Dark"), false, true, QStringLiteral("radio"), 0, {}},
    };

    return {file, edit, view};
}

} // namespace

class FakeMenu;

class FakeMenuAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version CONSTANT)
    Q_PROPERTY(QString TextDirection READ textDirection CONSTANT)
    Q_PROPERTY(QString Status READ status CONSTANT)

public:
    explicit FakeMenuAdaptor(FakeMenu *menu);

    uint version() const { return 3; }
    QString textDirection() const { return QStringLiteral("ltr"); }
    QString status() const { return QStringLiteral("normal"); }

public Q_SLOTS:
    uint GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames,
                   DBusMenuLayoutItem &layout);
    bool AboutToShow(int id);
    void Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp);
    QList<DBusMenuPropertySet> GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames);
    QDBusVariant GetProperty(int id, const QString &name);

Q_SIGNALS:
    void LayoutUpdated(uint revision, int parent);
    void ItemsPropertiesUpdated(const QList<DBusMenuPropertySet> &updated,
                                const QList<DBusMenuPropertySet> &removed);

private:
    FakeMenu *m_menu = nullptr;
};

class FakeMenu : public QObject
{
    Q_OBJECT

public:
    explicit FakeMenu(int windowIndex = 0, QObject *parent = nullptr)
        : QObject(parent)
        , m_items(buildTree(windowIndex))
    {
    }

    const QList<FakeItem> &items() const { return m_items; }

    const FakeItem *find(qint32 id, const QList<FakeItem> &items) const
    {
        for (const FakeItem &item : items) {
            if (item.id == id)
                return &item;
            if (const FakeItem *nested = find(id, item.children))
                return nested;
        }
        return nullptr;
    }

    bool toggle(qint32 id)
    {
        FakeItem *item = findMutable(id, m_items);
        if (!item || item->toggleType.isEmpty())
            return false;
        item->toggleState = item->toggleState == 1 ? 0 : 1;
        emit changed(id);
        return true;
    }

Q_SIGNALS:
    void changed(qint32 id);

public:
    QList<FakeItem> m_items;
    uint m_revision = 1;

private:
    FakeItem *findMutable(qint32 id, QList<FakeItem> &items)
    {
        for (FakeItem &item : items) {
            if (item.id == id)
                return &item;
            if (FakeItem *nested = findMutable(id, item.children))
                return nested;
        }
        return nullptr;
    }
};

FakeMenuAdaptor::FakeMenuAdaptor(FakeMenu *menu)
    : QDBusAbstractAdaptor(menu)
    , m_menu(menu)
{
}

uint FakeMenuAdaptor::GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames,
                               DBusMenuLayoutItem &layout)
{
    Q_UNUSED(propertyNames)

    layout = DBusMenuLayoutItem{};
    layout.id = parentId;

    if (parentId == 0) {
        layout.properties.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
        if (recursionDepth != 0) {
            for (const FakeItem &item : m_menu->items()) {
                DBusMenuLayoutItem child;
                child.id = item.id;
                child.properties.insert(QStringLiteral("label"), item.label);
                child.properties.insert(QStringLiteral("enabled"), item.enabled);
                child.properties.insert(QStringLiteral("visible"), true);
                if (!item.children.isEmpty())
                    child.properties.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
                if (!item.toggleType.isEmpty()) {
                    child.properties.insert(QStringLiteral("toggle-type"), item.toggleType);
                    child.properties.insert(QStringLiteral("toggle-state"), item.toggleState);
                }
                layout.children.append(child);
            }
        }
        return m_menu->m_revision;
    }

    const FakeItem *item = m_menu->find(parentId, m_menu->items());
    if (!item)
        return m_menu->m_revision;

    layout.properties.insert(QStringLiteral("label"), item->label);
    layout.properties.insert(QStringLiteral("enabled"), item->enabled);
    layout.properties.insert(QStringLiteral("visible"), true);
    if (item->separator)
        layout.properties.insert(QStringLiteral("type"), QStringLiteral("separator"));
    if (!item->toggleType.isEmpty()) {
        layout.properties.insert(QStringLiteral("toggle-type"), item->toggleType);
        layout.properties.insert(QStringLiteral("toggle-state"), item->toggleState);
    }

    if (recursionDepth != 0) {
        for (const FakeItem &child : item->children) {
            DBusMenuLayoutItem entry;
            entry.id = child.id;
            entry.properties.insert(QStringLiteral("label"), child.label);
            entry.properties.insert(QStringLiteral("enabled"), child.enabled);
            entry.properties.insert(QStringLiteral("visible"), true);
            if (child.separator)
                entry.properties.insert(QStringLiteral("type"), QStringLiteral("separator"));
            if (!child.children.isEmpty())
                entry.properties.insert(QStringLiteral("children-display"), QStringLiteral("submenu"));
            if (!child.toggleType.isEmpty()) {
                entry.properties.insert(QStringLiteral("toggle-type"), child.toggleType);
                entry.properties.insert(QStringLiteral("toggle-state"), child.toggleState);
            }
            layout.children.append(entry);
        }
    }

    return m_menu->m_revision;
}

bool FakeMenuAdaptor::AboutToShow(int id)
{
    qInfo() << "[fakeapp] AboutToShow" << id;
    return false;
}

void FakeMenuAdaptor::Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp)
{
    Q_UNUSED(data)
    Q_UNUSED(timestamp)

    const FakeItem *item = m_menu->find(id, m_menu->items());
    const QString label = item ? item->label : QString::number(id);
    qInfo().nospace() << "[fakeapp] Event id=" << id << " (" << label << ") event=" << eventId;

    if (eventId == QLatin1String("clicked")) {
        if (m_menu->toggle(id)) {
            ++m_menu->m_revision;
            // 通知客户端：这个项的属性变了，菜单结构也变了
            QList<DBusMenuPropertySet> updated;
            const FakeItem *toggled = m_menu->find(id, m_menu->items());
            DBusMenuPropertySet set;
            set.id = id;
            set.properties.insert(QStringLiteral("toggle-state"), toggled ? toggled->toggleState : 0);
            updated.append(set);
            emit ItemsPropertiesUpdated(updated, {});
            emit LayoutUpdated(m_menu->m_revision, 0);
        }
    }
}

QList<DBusMenuPropertySet> FakeMenuAdaptor::GetGroupProperties(const QList<int> &ids,
                                                               const QStringList &propertyNames)
{
    Q_UNUSED(propertyNames)

    QList<DBusMenuPropertySet> result;
    for (int id : ids) {
        DBusMenuPropertySet set;
        set.id = id;
        const FakeItem *item = m_menu->find(id, m_menu->items());
        if (item) {
            set.properties.insert(QStringLiteral("label"), item->label);
            set.properties.insert(QStringLiteral("enabled"), item->enabled);
            set.properties.insert(QStringLiteral("visible"), true);
        }
        result.append(set);
    }
    return result;
}

QDBusVariant FakeMenuAdaptor::GetProperty(int id, const QString &name)
{
    const FakeItem *item = m_menu->find(id, m_menu->items());
    if (!item)
        return QDBusVariant(QVariant());

    if (name == QLatin1String("label"))
        return QDBusVariant(item->label);
    if (name == QLatin1String("enabled"))
        return QDBusVariant(item->enabled);
    if (name == QLatin1String("toggle-state"))
        return QDBusVariant(item->toggleState);
    return QDBusVariant(QVariant());
}

/**
 * 测试用控制接口（/Control），用来在运行中"关掉一个窗口"：
 *   busctl --user call <fakeapp 唯一名> /Control test.appmenu.Control DropMenu s /MenuBar/2
 * 只服务于 --menus > 1 的多窗口场景，验证 registrar 按 pid 注销时不会
 * 连带删掉同进程其它窗口的菜单。
 */
class FakeControl : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "test.appmenu.Control")

public:
    using DropHandler = std::function<bool(const QString &)>;

    explicit FakeControl(DropHandler handler, QObject *parent = nullptr)
        : QObject(parent)
        , m_handler(std::move(handler))
    {
    }

public Q_SLOTS:
    bool DropMenu(const QString &path) { return m_handler && m_handler(path); }
    void Quit() { QCoreApplication::quit(); }

private:
    DropHandler m_handler;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("appmenu-fakeapp"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Fake dbusmenu exporter for testing the global menu chain"));
    parser.addHelpOption();
    QCommandLineOption registerOption(
        QStringList{QStringLiteral("g"), QStringLiteral("register")},
        QStringLiteral("Also call RegisterSurfaceMenu on the registrar (hypr-appmenu path)."));
    parser.addOption(registerOption);
    QCommandLineOption registerWindowOption(
        QStringList{QStringLiteral("w"), QStringLiteral("register-window")},
        QStringLiteral("Also call RegisterWindow(0, /MenuBar/1) (standard path, windowId 0 → pid)."));
    parser.addOption(registerWindowOption);
    QCommandLineOption menusOption(
        QStringList{QStringLiteral("m"), QStringLiteral("menus")},
        QStringLiteral("Export N menu bars (/MenuBar/1 .. /MenuBar/N) on one connection, "
                       "simulating one process with N windows."),
        QStringLiteral("n"), QStringLiteral("1"));
    parser.addOption(menusOption);
    parser.process(app);

    appmenuRegisterDBusTypes();

    QDBusConnection bus = QDBusConnection::sessionBus();

    // /MenuBar 这个中间节点用来让 Introspect 能列出子节点
    auto *menuBarNode = new QObject(&app);
    menuBarNode->setObjectName(QStringLiteral("MenuBar"));

    if (!bus.registerObject(QStringLiteral("/MenuBar"), menuBarNode)) {
        qCritical() << "cannot register /MenuBar";
        return 1;
    }

    // Qt 应用导出的菜单路径就是 /MenuBar/<n>；一个进程开多个窗口时会有多条
    const int menuCount = qMax(1, parser.value(menusOption).toInt());
    QHash<QString, FakeMenu *> menus;
    for (int i = 1; i <= menuCount; ++i) {
        const QString path = QStringLiteral("/MenuBar/%1").arg(i);
        auto *menu = new FakeMenu(menuCount > 1 ? i : 0, &app);
        auto *adaptor = new FakeMenuAdaptor(menu);
        Q_UNUSED(adaptor)

        if (!bus.registerObject(path, menu)) {
            qCritical() << "cannot register" << path;
            return 1;
        }
        menus.insert(path, menu);
    }

    auto *control = new FakeControl([&bus, &menus](const QString &path) {
        FakeMenu *menu = menus.take(path);
        if (!menu)
            return false;
        bus.unregisterObject(path);
        menu->deleteLater();
        qInfo() << "[fakeapp] dropped menu" << path;
        return true;
    }, &app);
    if (!bus.registerObject(QStringLiteral("/Control"), control,
                            QDBusConnection::ExportAllSlots)) {
        qCritical() << "cannot register /Control";
        return 1;
    }

    qInfo() << "[fakeapp] exporting" << menuCount << "menu bar(s) on" << bus.baseService();

    if (parser.isSet(registerOption)) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("/com/canonical/AppMenu/Registrar"),
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("RegisterSurfaceMenu"));
        call << uint(QCoreApplication::applicationPid()) << bus.baseService()
             << QStringLiteral("/MenuBar/1");
        QDBusConnection::sessionBus().asyncCall(call);
        qInfo() << "[fakeapp] RegisterSurfaceMenu sent";
    }

    if (parser.isSet(registerWindowOption)) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("/com/canonical/AppMenu/Registrar"),
            QStringLiteral("com.canonical.AppMenu.Registrar"),
            QStringLiteral("RegisterWindow"));
        call << uint(0) << QDBusObjectPath(QStringLiteral("/MenuBar/1"));
        QDBusConnection::sessionBus().asyncCall(call);
        qInfo() << "[fakeapp] RegisterWindow(0, /MenuBar/1) sent";
    }

    return app.exec();
}

#include "fakeapp.moc"
