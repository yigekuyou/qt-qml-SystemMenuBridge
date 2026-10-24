#include "registrar.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QSet>
#include <QTimer>
#include <QXmlStreamReader>
#include <QDebug>

namespace {

constexpr auto kRegistrarService = "com.canonical.AppMenu.Registrar";
constexpr auto kRegistrarPath = "/com/canonical/AppMenu/Registrar";
constexpr auto kRegistrarInterface = "com.canonical.AppMenu.Registrar";
constexpr auto kMenuBarPath = "/MenuBar";

// 应用启动后可能过一会才创建菜单栏，所以对新出现的名字做有限次重试
constexpr int kMaxProbeAttempts = 15;
constexpr int kProbeIntervalMs = 2000;
// 之后每 60s 低频重扫一次，兜住"多窗口应用后来才导出 /MenuBar/2"这类情况
constexpr int kRescanIntervalMs = 60000;

// 单次 Introspect 的上限。Qt 默认是 25s：总线上长期存在一些不跑事件循环的
// 连接（Qt 里跨线程 QDBusConnection 很常见），对它们探测时调用会一直挂着，
// broker 替收发两端攒的未读消息越堆越多，攒过配额就把连接踢下线 —— 桌面
// 会话里最先遭殃的往往就是往总线上发请求的那个进程。给每次探测加硬超时，
// 挂起的调用最多堆 kMaxConcurrentProbes 个、每个 1.5s。
constexpr int kProbeTimeoutMs = 1500;

// 探测节流：同时最多 kMaxConcurrentProbes 个在飞，两次发车至少隔 kProbePumpMs。
// 开机会话里有上百条连接，一次性齐发会把总线打出一波尖峰。
constexpr int kMaxConcurrentProbes = 4;
constexpr int kProbePumpMs = 120;

// 同一条连接连续超时这么多次就判定为"叫不醒"，在它的 owner 变化前不再探测，
// 否则每 60s 的重扫会给这些死连接一直续期未读消息。
constexpr int kMaxProbeTimeouts = 3;
constexpr int kTimeoutBackoffMs = 60000;

/**
 * 解析 Introspect /MenuBar 的结果。现实中有两种导出方式，都要认：
 *   1. Qt 内建 QDBusMenuBar：菜单挂在 /MenuBar/<n> 子节点上（新版 Qt）
 *   2. libdbusmenu-qt / GTK appmenu：菜单直接挂在 /MenuBar 本身
 */
struct MenuBarIntrospection
{
    bool hasMenuInterface = false;
    QStringList childPaths;
};

MenuBarIntrospection parseMenuBarIntrospection(const QString &xml)
{
    MenuBarIntrospection result;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;

        if (reader.name() == QLatin1String("interface")) {
            if (reader.attributes().value(QLatin1String("name"))
                == QLatin1String("com.canonical.dbusmenu")) {
                result.hasMenuInterface = true;
            }
            continue;
        }

        if (reader.name() != QLatin1String("node"))
            continue;

        const QString name = reader.attributes().value(QLatin1String("name")).toString();
        if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('/')))
            continue; // 跳过根节点和奇怪的路径

        result.childPaths.append(QString::fromLatin1(kMenuBarPath) + QLatin1Char('/') + name);
    }
    return result;
}

} // namespace

// ===========================================================================
// RegistrarAdaptor
// ===========================================================================

RegistrarAdaptor::RegistrarAdaptor(AppMenuRegistrar *registrar)
    : QDBusAbstractAdaptor(registrar)
    , m_registrar(registrar)
{
    setAutoRelaySignals(false);

    connect(registrar, &AppMenuRegistrar::entryRegistered, this, [this](const AppMenuEntry &entry) {
        emit WindowRegistered(entry.windowId, entry.service, QDBusObjectPath(entry.path));
    });
    connect(registrar, &AppMenuRegistrar::entryUnregistered, this, [this](uint windowId) {
        emit WindowUnregistered(windowId);
    });
}

void RegistrarAdaptor::RegisterWindow(uint windowId, const QDBusObjectPath &menuObjectPath)
{
    if (!m_registrar->calledFromDBus()) {
        qWarning() << "RegisterWindow called outside of D-Bus";
        return;
    }

    // 发送者的唯一名就是菜单所在的总线名
    const QString sender = m_registrar->message().service();
    if (sender.isEmpty()) {
        m_registrar->sendErrorReply(QDBusError::InvalidArgs,
                                    QStringLiteral("cannot determine caller"));
        return;
    }

    m_registrar->handleRegisterWindow(windowId, sender, menuObjectPath.path());
}

void RegistrarAdaptor::RegisterSurfaceMenu(uint pid, const QString &serviceName, const QString &path)
{
    // hypr-appmenu 的扩展：pid 由合成器通过 wl_client_get_credentials 拿到
    m_registrar->handleRegisterSurface(pid, serviceName, path);
}

void RegistrarAdaptor::UnregisterWindow(uint windowId)
{
    m_registrar->handleUnregister(windowId);
}

QList<QPair<uint, QDBusObjectPath>> RegistrarAdaptor::GetMenus() const
{
    QList<QPair<uint, QDBusObjectPath>> result;
    const QList<AppMenuEntry> entries = m_registrar->entries();
    result.reserve(entries.size());
    for (const AppMenuEntry &entry : entries)
        result.append({entry.windowId, QDBusObjectPath(entry.path)});
    return result;
}

QDBusObjectPath RegistrarAdaptor::GetMenuForWindow(uint windowId) const
{
    const AppMenuEntry entry = m_registrar->entryForWindow(windowId);
    if (entry.path.isEmpty()) {
        // 空字符串不是合法的 object path，直接按错误回复（否则会 marshalling 失败）
        if (m_registrar->calledFromDBus()) {
            m_registrar->sendErrorReply(QDBusError::UnknownObject,
                                        QStringLiteral("no menu registered for this window"));
        }
        return QDBusObjectPath(QStringLiteral("/"));
    }
    return QDBusObjectPath(entry.path);
}

QList<AppMenuEntry> RegistrarAdaptor::GetMenuList() const
{
    return m_registrar->entries();
}

AppMenuEntry RegistrarAdaptor::GetMenuForPid(uint pid) const
{
    return m_registrar->entryForPid(pid);
}

// ===========================================================================
// AppMenuRegistrar
// ===========================================================================

AppMenuRegistrar::AppMenuRegistrar(QObject *parent)
    : QObject(parent)
{
}

AppMenuRegistrar::~AppMenuRegistrar() = default;

QString AppMenuRegistrar::menuKey(const QString &service, const QString &path)
{
    return service + QLatin1Char('|') + path;
}

bool AppMenuRegistrar::start(QString *errorMessage, bool replaceExisting)
{
    appmenuRegisterDBusTypes();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cannot connect to the session bus");
        return false;
    }

    m_ownUniqueName = bus.baseService();

    m_adaptor = new RegistrarAdaptor(this);

    const QString serviceName = QString::fromLatin1(kRegistrarService);
    bool owned = false;

    if (replaceExisting && bus.interface()) {
        const QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply =
            bus.interface()->registerService(serviceName,
                                             QDBusConnectionInterface::ReplaceExistingService,
                                             QDBusConnectionInterface::AllowReplacement);
        owned = reply.isValid()
                && reply.value() == QDBusConnectionInterface::ServiceRegistered;
    } else {
        owned = bus.registerService(serviceName);
    }

    if (!owned) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cannot own %1 (another registrar is already running, "
                                           "try --replace)")
                                .arg(serviceName);
        }
        return false;
    }

    // 引擎热重载（Quickshell 保存配置即重载）时，旧 QML 单例尚未销毁，
    // 旧 registrar 对象仍占着这条对象路径，同一条连接上重复注册必然失败。
    // 先反注册一次自己连接上的旧对象（没有也无副作用），再注册。
    bus.unregisterObject(QString::fromLatin1(kRegistrarPath));

    if (!bus.registerObject(QString::fromLatin1(kRegistrarPath), this,
                            QDBusConnection::ExportAdaptors)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("cannot register object %1")
                                .arg(QString::fromLatin1(kRegistrarPath));
        bus.unregisterService(serviceName);
        return false;
    }

    // 监听总线上的所有名字变化
    bus.connect(QString(), QStringLiteral("/org/freedesktop/DBus"),
                QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"),
                this, SLOT(onNameOwnerChanged(QString, QString, QString)));

    // 探测队列的发车定时器：一拍最多放一个探测出去
    m_pump = new QTimer(this);
    m_pump->setSingleShot(true);
    m_pump->setInterval(kProbePumpMs);
    connect(m_pump, &QTimer::timeout, this, &AppMenuRegistrar::pumpProbeQueue);

    scanExistingNames();

    // 低频重扫：只针对还没发现菜单的服务，而且每次只补一枪（attempt 已达上限，
    // 不会再触发 2s 一串的重试），用慢速轮询等应用后来才导出的菜单。
    auto *rescan = new QTimer(this);
    rescan->setInterval(kRescanIntervalMs);
    connect(rescan, &QTimer::timeout, this, [this]() {
        QSet<QString> withMenu;
        for (const AppMenuEntry &entry : std::as_const(m_menus))
            withMenu.insert(entry.service);

        for (const QString &service : std::as_const(m_services)) {
            if (withMenu.contains(service) || probeSuppressed(service))
                continue;
            enqueueProbe(service, kMaxProbeAttempts);
        }
    });
    rescan->start();

    qInfo() << "appmenu: owning" << kRegistrarService << "at" << kRegistrarPath;
    return true;
}

QList<AppMenuEntry> AppMenuRegistrar::entries() const
{
    QList<AppMenuEntry> list = m_menus.values();
    std::sort(list.begin(), list.end(), [](const AppMenuEntry &a, const AppMenuEntry &b) {
        if (a.pid != b.pid)
            return a.pid < b.pid;
        return a.path < b.path;
    });
    return list;
}

AppMenuEntry AppMenuRegistrar::entryForPid(uint pid) const
{
    // 一个进程可能有多个窗口，每个窗口导出自己的 /MenuBar/<n>，所以同一 pid
    // 会对应多条记录。m_menus 是 QHash，直接返回"第一条"会随哈希顺序变化；
    // 这里固定取 path 最小的一条，让 GetMenuForPid、GetMenuForWindow 和面板
    // 本地的 findEntryForPid 得到同一个结果（切换/刷新时不会来回跳）。
    AppMenuEntry best;
    for (const AppMenuEntry &entry : m_menus) {
        if (entry.pid != pid || entry.path.isEmpty())
            continue;
        if (best.path.isEmpty() || entry.path < best.path)
            best = entry;
    }
    return best;
}

AppMenuEntry AppMenuRegistrar::entryForWindow(uint windowId) const
{
    AppMenuEntry best;
    for (const AppMenuEntry &entry : m_menus) {
        if (entry.windowId != windowId || entry.path.isEmpty())
            continue;
        if (best.path.isEmpty() || entry.path < best.path)
            best = entry;
    }

    // Wayland 下 windowId 就是 pid（见 types.h），但应用如果自己用真实 XID
    // 注册过，windowId 就不再等于 pid。hypr-appmenu 仍按 pid 调用
    // GetMenuForWindow，所以精确匹配不到时退回按 pid 查。
    if (best.path.isEmpty() && windowId != 0)
        return entryForPid(windowId);
    return best;
}

void AppMenuRegistrar::handleRegisterWindow(uint windowId, const QString &service, const QString &path)
{
    if (service.isEmpty() || path.isEmpty())
        return;

    const uint pid = pidOf(service);

    AppMenuEntry entry;
    entry.service = service;
    entry.path = path;
    entry.pid = pid;
    // Wayland 下 Qt 可能用 windowId == 0 注册，此时用 pid 代替（与 hypr-appmenu 一致）
    entry.windowId = windowId != 0 ? windowId : (pid != 0 ? pid : nextSyntheticId());

    addOrUpdate(entry);
}

void AppMenuRegistrar::handleRegisterSurface(uint pid, const QString &service, const QString &path)
{
    if (service.isEmpty() || path.isEmpty())
        return;

    AppMenuEntry entry;
    entry.service = service;
    entry.path = path;
    entry.pid = pid;
    entry.windowId = pid != 0 ? pid : nextSyntheticId();

    addOrUpdate(entry);
}

void AppMenuRegistrar::handleUnregister(uint id)
{
    dropEntriesOwnedBy(id);
}

void AppMenuRegistrar::addOrUpdate(const AppMenuEntry &entry)
{
    const QString key = menuKey(entry.service, entry.path);

    const auto it = m_menus.find(key);
    if (it != m_menus.end()) {
        const uint oldWindowId = it->windowId;

        if (entry.pid)
            it->pid = entry.pid;
        // 真实 XID（RegisterWindow 传入）优先于 pid/合成 id
        if (entry.windowId && entry.windowId != entry.pid && entry.windowId != oldWindowId) {
            it->windowId = entry.windowId;
            // 旧 id 只是 pid 占位时不用广播注销：按 pid 绑定的客户端本来就不看它，
            // 广播反而会让面板短暂以为整个进程的菜单没了
            if (oldWindowId != it->pid)
                emit entryUnregistered(oldWindowId);
            emit entryRegistered(*it);
        } else if (it->windowId == 0) {
            it->windowId = entry.windowId;
            emit entryRegistered(*it);
        }
        return;
    }

    m_menus.insert(key, entry);
    qInfo().nospace() << "appmenu: menu " << entry.service << " " << entry.path
                      << " pid=" << entry.pid << " window=" << entry.windowId;
    emit entryRegistered(entry);
}

void AppMenuRegistrar::dropEntriesOwnedBy(uint id)
{
    if (id == 0)
        return;

    // 1) 真实 windowId（X11 XID / 合成 id）优先
    QList<AppMenuEntry> matches;
    for (const AppMenuEntry &entry : std::as_const(m_menus)) {
        if (entry.windowId == id)
            matches.append(entry);
    }

    // 2) 没有 windowId 命中时，按 hypr-appmenu 的 UnregisterWindow(pid) 语义
    if (matches.isEmpty()) {
        for (const AppMenuEntry &entry : std::as_const(m_menus)) {
            if (entry.pid == id)
                matches.append(entry);
        }
    }

    if (matches.isEmpty())
        return;

    // Wayland 下 windowId 就是 pid，一个进程开多个窗口时这些条目共享同一个 id。
    // 关掉一个窗口时根本无从判断关的是哪一个：不能把兄弟窗口的菜单一起删掉
    // （那会让应用剩下的窗口在没有菜单的状态下等 60s 重扫）。这里改为重新
    // Introspect 对账，syncMenus 只删真正消失的 path。
    if (matches.size() > 1) {
        QSet<QString> services;
        for (const AppMenuEntry &entry : std::as_const(matches))
            services.insert(entry.service);

        qInfo().noquote() << "appmenu: unregister" << id << "matches" << matches.size()
                          << "menus, re-probing" << services.size() << "service(s)";
        for (const QString &service : std::as_const(services)) {
            m_services.insert(service);
            enqueueProbe(service, 0);
        }
        return;
    }

    const AppMenuEntry entry = matches.first();
    m_menus.remove(menuKey(entry.service, entry.path));
    qInfo().nospace() << "appmenu: menu gone " << entry.service << " " << entry.path;
    emit entryUnregistered(entry.windowId);
}

void AppMenuRegistrar::removeService(const QString &service)
{
    QList<AppMenuEntry> removed;
    for (auto it = m_menus.begin(); it != m_menus.end();) {
        if (it->service == service) {
            removed.append(*it);
            it = m_menus.erase(it);
        } else {
            ++it;
        }
    }

    for (const AppMenuEntry &entry : std::as_const(removed)) {
        qInfo().nospace() << "appmenu: service left " << entry.service;
        emit entryUnregistered(entry.windowId);
    }
}

uint AppMenuRegistrar::pidOf(const QString &service) const
{
    const auto cached = m_pidCache.constFind(service);
    if (cached != m_pidCache.constEnd())
        return cached.value();

    // servicePid() 是走连接默认 25s 超时的同步调用；这里手搓一条带硬超时的，
    // 免得某个应用刚好卡住时把 GUI 线程按在那里。结果缓存，owner 变化时作废。
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"));
    call << service;

    const QDBusMessage reply =
        QDBusConnection::sessionBus().call(call, QDBus::Block, kProbeTimeoutMs);
    const QVariantList args = reply.arguments();
    const uint pid = reply.type() == QDBusMessage::ReplyMessage && !args.isEmpty()
            ? args.at(0).toUInt() : 0u;
    if (pid != 0)
        m_pidCache.insert(service, pid);
    return pid;
}

uint AppMenuRegistrar::nextSyntheticId()
{
    return ++m_synthetic;
}

void AppMenuRegistrar::scanExistingNames()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();

        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "ListNames failed:" << reply.errorMessage();
            return;
        }

        const QVariantList args = reply.arguments();
        const QStringList names = args.isEmpty() ? QStringList() : args.at(0).toStringList();

        for (const QString &name : names) {
            // 应用导出的菜单挂在连接的唯一名上；well-known 名跳过，避免同一条菜单被记两次
            if (!name.startsWith(QLatin1Char(':')))
                continue;
            if (name == m_ownUniqueName)
                continue;
            m_services.insert(name);
            // 开机扫描：这些应用早就跑起来了，没必要每个都给 15 次 2s 重试，
            // 一次不中就交给 60s 重扫；只有 NameOwnerChanged 新出现的名字
            // （应用刚启动）才走完整的重试链。
            enqueueProbe(name, kMaxProbeAttempts);
        }
        qInfo() << "appmenu: scanning" << m_services.size() << "bus connections (throttled)";
    });
}

void AppMenuRegistrar::scheduleProbe(const QString &service, int attempt)
{
    m_probeAttempts.insert(service, attempt);
    QTimer::singleShot(kProbeIntervalMs, this, [this, service, attempt]() {
        if (!m_services.contains(service))
            return; // 名字已经消失
        enqueueProbe(service, attempt);
    });
}

void AppMenuRegistrar::probe(const QString &service, int attempt)
{
    if (!m_services.contains(service) || probeSuppressed(service))
        return;

    QDBusMessage call = QDBusMessage::createMethodCall(
        service, QString::fromLatin1(kMenuBarPath),
        QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    ++m_inFlight;
    // 必须自带超时：连接默认是 25s，碰上一声不吭的对端会把挂起调用越堆越多，
    // broker 替两边攒的未读消息也跟着涨。
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call, kProbeTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, service, attempt](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                if (m_inFlight > 0)
                    --m_inFlight;
                pumpProbeQueue(); // 有空位了，放下一枪

                if (!m_services.contains(service))
                    return;

                if (w->isError()) {
                    const QString errorName = w->error().name();
                    const bool silent = errorName == QLatin1String("org.freedesktop.DBus.Error.NoReply")
                            || errorName == QLatin1String("org.freedesktop.DBus.Error.Timeout")
                            || errorName == QLatin1String("org.freedesktop.DBus.Error.TimedOut");
                    if (silent) {
                        // 对端根本没在读消息，重试只会继续给它堆未读
                        noteProbeTimeout(service);
                        return;
                    }

                    // 这个连接没有导出 /MenuBar，稍后再试几次
                    m_timeouts.remove(service);
                    if (attempt < kMaxProbeAttempts)
                        scheduleProbe(service, attempt + 1);
                    return;
                }

                const QDBusMessage reply = w->reply();
                const QVariantList args = reply.arguments();
                const QString xml = args.isEmpty() ? QString() : args.at(0).toString();
                const MenuBarIntrospection introspection = parseMenuBarIntrospection(xml);

                QStringList paths = introspection.childPaths;
                if (introspection.hasMenuInterface)
                    paths.prepend(QString::fromLatin1(kMenuBarPath));

                if (paths.isEmpty()) {
                    if (attempt < kMaxProbeAttempts)
                        scheduleProbe(service, attempt + 1);
                    return;
                }

                m_probeAttempts.remove(service);
                m_timeouts.remove(service);
                m_retryAfter.remove(service);
                syncMenus(service, paths);
            });
}

void AppMenuRegistrar::enqueueProbe(const QString &service, int attempt)
{
    if (!m_services.contains(service) || probeSuppressed(service))
        return;

    ProbeRequest req;
    req.service = service;
    req.attempt = attempt;
    m_probeQueue.enqueue(req);
    pumpProbeQueue();
}

void AppMenuRegistrar::pumpProbeQueue()
{
    while (!m_probeQueue.isEmpty()) {
        if (m_inFlight >= kMaxConcurrentProbes)
            return; // 满了，等某个探测回来时再发车

        const ProbeRequest req = m_probeQueue.dequeue();
        if (!m_services.contains(req.service) || probeSuppressed(req.service))
            continue; // 名字没了 / 已经判定叫不醒

        probe(req.service, req.attempt);
        if (m_pump)
            m_pump->start(); // 一拍只放一个，剩下的排队
        return;
    }
}

bool AppMenuRegistrar::probeSuppressed(const QString &service) const
{
    if (m_unreachable.contains(service))
        return true;

    const auto it = m_retryAfter.constFind(service);
    return it != m_retryAfter.constEnd() && QDateTime::currentMSecsSinceEpoch() < it.value();
}

void AppMenuRegistrar::noteProbeTimeout(const QString &service)
{
    const int count = m_timeouts.value(service) + 1;
    m_timeouts.insert(service, count);

    if (count >= kMaxProbeTimeouts) {
        // 判定为叫不醒：在它的 owner 变化（应用重启、换连接）之前不再探测。
        // 总线上这种连接常年存在（Qt 多线程 QDBusConnection、沙箱里的代理等），
        // 每 60s 重扫一次只会一直给 broker 续期未读消息，攒过配额就是断线。
        m_unreachable.insert(service);
        m_retryAfter.remove(service);
        m_probeAttempts.remove(service);
        qInfo().noquote() << "appmenu: giving up on unresponsive connection" << service
                          << "after" << count << "timeouts";
        return;
    }

    m_retryAfter.insert(service,
                        QDateTime::currentMSecsSinceEpoch() + qint64(kTimeoutBackoffMs) * count);
}

void AppMenuRegistrar::syncMenus(const QString &service, const QStringList &paths)
{
    // 先清掉已经不存在的路径（例如菜单对象重建后编号变了）
    QList<AppMenuEntry> stale;
    for (auto it = m_menus.begin(); it != m_menus.end();) {
        if (it->service == service && !paths.contains(it->path)) {
            stale.append(*it);
            it = m_menus.erase(it);
        } else {
            ++it;
        }
    }
    for (const AppMenuEntry &entry : std::as_const(stale))
        emit entryUnregistered(entry.windowId);

    const uint pid = pidOf(service);

    for (const QString &path : paths) {
        const QString key = menuKey(service, path);
        if (m_menus.contains(key))
            continue; // 应用自己已经注册过（或者之前已发现）

        AppMenuEntry entry;
        entry.service = service;
        entry.path = path;
        entry.pid = pid;
        entry.windowId = pid != 0 ? pid : nextSyntheticId();
        addOrUpdate(entry);
    }
}

void AppMenuRegistrar::onNameOwnerChanged(const QString &name, const QString &oldOwner,
                                          const QString &newOwner)
{
    Q_UNUSED(oldOwner)
    if (name == QLatin1String(kRegistrarService) || name == m_ownUniqueName
        || name == QLatin1String("org.freedesktop.DBus")) {
        return;
    }
    if (name.startsWith(QLatin1Char(':')) == false)
        return; // 只关心唯一名，见 scanExistingNames 的说明

    if (!newOwner.isEmpty()) {
        if (m_services.contains(name))
            return;
        m_services.insert(name);
        // 换了一个 owner，之前"叫不醒"的判断作废，重新给机会
        m_unreachable.remove(name);
        m_timeouts.remove(name);
        m_retryAfter.remove(name);
        m_pidCache.remove(name);
        enqueueProbe(name, 0);
    } else {
        m_services.remove(name);
        m_probeAttempts.remove(name);
        m_unreachable.remove(name);
        m_timeouts.remove(name);
        m_retryAfter.remove(name);
        m_pidCache.remove(name);
        removeService(name);
    }
}
