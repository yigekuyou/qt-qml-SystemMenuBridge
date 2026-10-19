/**
 * appmenu-fakeapp —— 一个只导出 com.canonical.dbusmenu 的"假应用"
 *
 * 用途：在没有真实 Qt/Chromium/Firefox 应用时验证整条链路：
 *   appmenu-registrar 发现 /MenuBar/1 → AppMenuService 按 pid 绑定 → QML 渲染。
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
#include <QList>
#include <QTimer>
#include <QDebug>

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

QList<FakeItem> buildTree()
{
    FakeItem file;
    file.id = 1;
    file.label = QStringLiteral("_File");
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
    explicit FakeMenu(QObject *parent = nullptr)
        : QObject(parent)
        , m_items(buildTree())
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
    parser.process(app);

    appmenuRegisterDBusTypes();

    FakeMenu menu;
    auto *adaptor = new FakeMenuAdaptor(&menu);
    Q_UNUSED(adaptor)

    QDBusConnection bus = QDBusConnection::sessionBus();

    // /MenuBar 这个中间节点用来让 Introspect 能列出子节点
    auto *menuBarNode = new QObject(&app);
    menuBarNode->setObjectName(QStringLiteral("MenuBar"));

    if (!bus.registerObject(QStringLiteral("/MenuBar"), menuBarNode)) {
        qCritical() << "cannot register /MenuBar";
        return 1;
    }
    // Qt 应用导出的菜单路径就是 /MenuBar/<n>
    if (!bus.registerObject(QStringLiteral("/MenuBar/1"), &menu)) {
        qCritical() << "cannot register /MenuBar/1";
        return 1;
    }

    qInfo() << "[fakeapp] exporting com.canonical.dbusmenu at /MenuBar/1 on" << bus.baseService();

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
