#include "appmenuservice.h"
#include "dbusmenumodel.h"
#include "registrar.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QTimer>
#include <QDebug>

namespace {
// 与 Qt 的 qdbusmenubar.cpp / hypr-appmenu 插件保持一致
constexpr auto kRegistrarService = "com.canonical.AppMenu.Registrar";
constexpr auto kRegistrarPath = "/com/canonical/AppMenu/Registrar";
constexpr auto kRegistrarInterface = "com.canonical.AppMenu.Registrar";
} // namespace

AppMenuService::AppMenuService(QObject *parent)
    : QObject(parent)
{
    appmenuRegisterDBusTypes();

    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(50); // 合并短时间内的多次信号
    connect(m_reloadTimer, &QTimer::timeout, this, &AppMenuService::reload);

    m_watcher = new QDBusServiceWatcher(QString::fromLatin1(kRegistrarService),
                                        QDBusConnection::sessionBus(),
                                        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered,
            this, &AppMenuService::onRegistrarAppeared);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &AppMenuService::onRegistrarVanished);

    // 没有别人的 registrar 就自己占名：这样不需要再跑一个 appmenu-registrar 守护进程，
    // 菜单照样能被导出（Qt 只认这条总线名在不在，不关心是谁占着）。
    startEmbeddedRegistrar();

    const bool present = QDBusConnection::sessionBus().interface()
                             && QDBusConnection::sessionBus().interface()->isServiceRegistered(
                                 QString::fromLatin1(kRegistrarService));
    if (present)
        onRegistrarAppeared();
}

AppMenuService::~AppMenuService() = default;

bool AppMenuService::available() const
{
    return m_available;
}

bool AppMenuService::ownsRegistrar() const
{
    return m_ownsRegistrar;
}

void AppMenuService::setOwnsRegistrar(bool value)
{
    if (m_ownsRegistrar == value)
        return;
    m_ownsRegistrar = value;
    emit ownsRegistrarChanged();
}

void AppMenuService::startEmbeddedRegistrar()
{
    // APPMENU_NO_REGISTRAR=1：只当客户端，用别人（hypr-appmenu / KDE）的 registrar
    if (!qEnvironmentVariableIsEmpty("APPMENU_NO_REGISTRAR"))
        return;

    auto *registrar = new AppMenuRegistrar(this);
    QString error;
    if (registrar->start(&error, false)) {
        m_registrar = registrar;
        setOwnsRegistrar(true);
        return;
    }

    // 名字已经被别人占了（另一个 Quickshell 实例、KDE plasma 的 registrar ……）
    delete registrar;
    qInfo().noquote() << "appmenu: using the existing registrar (" << error << ")";
}

QVariantList AppMenuService::menus() const
{
    QVariantList list;
    list.reserve(m_entries.size());
    for (const AppMenuEntry &entry : m_entries) {
        QVariantMap map;
        map.insert(QStringLiteral("windowId"), entry.windowId);
        map.insert(QStringLiteral("pid"), entry.pid);
        map.insert(QStringLiteral("service"), entry.service);
        map.insert(QStringLiteral("path"), entry.path);
        list.append(map);
    }
    return list;
}

uint AppMenuService::activePid() const
{
    return m_activePid;
}

QString AppMenuService::activeService() const
{
    return m_activeService;
}

QString AppMenuService::activePath() const
{
    return m_activePath;
}

DBusMenuModel *AppMenuService::menuModel() const
{
    return m_model;
}

void AppMenuService::setAvailable(bool value)
{
    if (m_available == value)
        return;
    m_available = value;
    emit availableChanged();
}

void AppMenuService::onRegistrarAppeared()
{
    setAvailable(true);

    QDBusConnection bus = QDBusConnection::sessionBus();
    // 内嵌 registrar 上线时 QDBusServiceWatcher 也会通知一次，这里先断开避免重复连接
    bus.disconnect(QString::fromLatin1(kRegistrarService), QString::fromLatin1(kRegistrarPath),
                   QString::fromLatin1(kRegistrarInterface), QStringLiteral("WindowRegistered"),
                   this, SLOT(onRegistrarSignal(QDBusMessage)));
    bus.disconnect(QString::fromLatin1(kRegistrarService), QString::fromLatin1(kRegistrarPath),
                   QString::fromLatin1(kRegistrarInterface), QStringLiteral("WindowUnregistered"),
                   this, SLOT(onRegistrarSignal(QDBusMessage)));
    bus.connect(QString::fromLatin1(kRegistrarService), QString::fromLatin1(kRegistrarPath),
                QString::fromLatin1(kRegistrarInterface), QStringLiteral("WindowRegistered"),
                this, SLOT(onRegistrarSignal(QDBusMessage)));
    bus.connect(QString::fromLatin1(kRegistrarService), QString::fromLatin1(kRegistrarPath),
                QString::fromLatin1(kRegistrarInterface), QStringLiteral("WindowUnregistered"),
                this, SLOT(onRegistrarSignal(QDBusMessage)));

    reload();
}

void AppMenuService::onRegistrarVanished()
{
    setAvailable(false);
    m_entries.clear();
    emit menusChanged();

    // registrar 只是"索引"：菜单内容还在应用自己的总线名上，
    // 所以保留当前绑定，registrar 回来后刷新即可。
}

void AppMenuService::onRegistrarSignal(const QDBusMessage &message)
{
    Q_UNUSED(message)
    scheduleReload();
}

void AppMenuService::scheduleReload()
{
    m_reloadTimer->start();
}

void AppMenuService::reload()
{
    if (!m_available) {
        if (!m_entries.isEmpty()) {
            m_entries.clear();
            emit menusChanged();
        }
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        QString::fromLatin1(kRegistrarService), QString::fromLatin1(kRegistrarPath),
        QString::fromLatin1(kRegistrarInterface), QStringLiteral("GetMenuList"));

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();

        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "GetMenuList failed:" << reply.errorMessage();
            return;
        }

        const QVariantList args = reply.arguments();
        m_entries = args.isEmpty() ? QList<AppMenuEntry>() : appmenuEntries(args.at(0));
        emit menusChanged();

        // 活动应用重启 / 菜单对象重建后自动重新绑定
        if (m_pendingPid)
            activatePid(m_pendingPid);
        else if (m_activePid) {
            const AppMenuEntry entry = findEntryForPid(m_activePid);
            if (entry.windowId && entry.path != m_activePath)
                applyActive(entry);
        }
    });
}

AppMenuEntry AppMenuService::findEntryForPid(uint pid) const
{
    // 同一进程的多个窗口会有多条记录（同一个 pid、不同 path）。这里和
    // AppMenuRegistrar::entryForPid 用同一条规则（取 path 最小的一条），
    // 免得本地列表和 registrar 的 GetMenuForPid 结果不一致导致来回跳。
    AppMenuEntry best;
    for (const AppMenuEntry &entry : m_entries) {
        if (entry.pid != pid || entry.path.isEmpty())
            continue;
        if (best.path.isEmpty() || entry.path < best.path)
            best = entry;
    }
    return best;
}

void AppMenuService::applyActive(const AppMenuEntry &entry)
{
    const bool changed = entry.pid != m_activePid
                         || entry.service != m_activeService
                         || entry.path != m_activePath;

    m_activePid = entry.pid;
    m_activeService = entry.service;
    m_activePath = entry.path;
    m_pendingPid = 0;

    if (!m_model) {
        m_model = new DBusMenuModel(this);
        emit menuModelChanged();
    }
    m_model->setService(entry.service);
    m_model->setPath(entry.path);

    if (changed)
        emit activeChanged();
}

void AppMenuService::activatePid(uint pid)
{
    m_pendingPid = pid;

    if (pid == 0) {
        clearActive();
        return;
    }

    if (m_available) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            QString::fromLatin1(kRegistrarService), QString::fromLatin1(kRegistrarPath),
            QString::fromLatin1(kRegistrarInterface), QStringLiteral("GetMenuForPid"));
        call << pid;

        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, pid](QDBusPendingCallWatcher *w) {
            w->deleteLater();

            const QDBusMessage reply = w->reply();
            if (reply.type() == QDBusMessage::ErrorMessage) {
                qWarning() << "GetMenuForPid failed:" << reply.errorMessage();
                return;
            }

            const QVariantList args = reply.arguments();
            const AppMenuEntry entry = args.isEmpty() ? AppMenuEntry() : appmenuEntry(args.at(0));

            if (!entry.windowId || entry.path.isEmpty()) {
                // registrar 里还没有这个 pid 的菜单，等 WindowRegistered 信号再来一次
                if (pid == m_pendingPid && m_activePath.isEmpty())
                    qInfo() << "no menu registered for pid" << pid << "yet";
                return;
            }

            if (pid == m_pendingPid)
                applyActive(entry);
        });
    } else {
        // 没有 registrar 时退化：直接从已知列表里找（本地发现的结果仍然有效）
        const AppMenuEntry entry = findEntryForPid(pid);
        if (entry.windowId)
            applyActive(entry);
    }
}

void AppMenuService::clearActive()
{
    const bool had = m_activePid || !m_activeService.isEmpty();
    m_activePid = 0;
    m_pendingPid = 0;
    m_activeService.clear();
    m_activePath.clear();

    if (m_model) {
        m_model->setService(QString());
        m_model->setPath(QString());
    }

    if (had)
        emit activeChanged();
}
