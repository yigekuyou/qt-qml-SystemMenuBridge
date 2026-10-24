#ifndef REGISTRAR_H
#define REGISTRAR_H

#include "types.h"

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QString>

class AppMenuRegistrar;

/**
 * com.canonical.AppMenu.Registrar 的 D-Bus 适配器。
 *
 * 对象路径固定为 /com/canonical/AppMenu/Registrar —— Qt 的 qdbusmenubar.cpp
 * （REGISTRAR_PATH）和 memtable/hypr-appmenu 插件都用这个路径。
 */
class RegistrarAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.AppMenu.Registrar")

public:
    explicit RegistrarAdaptor(AppMenuRegistrar *registrar);

public Q_SLOTS:
    // ---- 标准接口 ----
    void RegisterWindow(uint windowId, const QDBusObjectPath &menuObjectPath);
    void UnregisterWindow(uint windowId);
    QList<QPair<uint, QDBusObjectPath>> GetMenus() const;
    QDBusObjectPath GetMenuForWindow(uint windowId) const;

    // ---- 非标准扩展（hypr-appmenu 插件调用，签名 uss）----
    void RegisterSurfaceMenu(uint pid, const QString &serviceName, const QString &path);

    // ---- 非标准扩展（给 AppMenuService / QML 用）----
    QList<AppMenuEntry> GetMenuList() const;
    AppMenuEntry GetMenuForPid(uint pid) const;

Q_SIGNALS:
    void WindowRegistered(uint windowId, const QString &service, const QDBusObjectPath &menuObjectPath);
    void WindowUnregistered(uint windowId);

private:
    AppMenuRegistrar *m_registrar = nullptr;
};

/**
 * registrar 守护进程本体。
 *
 * 继承 QDBusContext：Qt 在分发 D-Bus 调用时，是把调用上下文挂在
 * "被 registerObject 的对象"上（adaptor 的 parent），而不是 adaptor 本身。
 * 所以 RegisterWindow 需要从 registrar 这里取发送者的唯一名。
 *
 * 除了提供标准注册表，它还负责"发现"：Wayland 下 Qt 应用不会调用
 * RegisterWindow（windowId 是 X11 XID），但会在自己的唯一总线名上导出
 * /MenuBar/<n>。这里监听 NameOwnerChanged，对每个唯一名 Introspect /MenuBar，
 * 把 (pid, service, path) 收进注册表 —— 用发现代替注册，全程不依赖任何
 * Wayland 全局菜单协议。
 */
class AppMenuRegistrar : public QObject, public QDBusContext
{
    Q_OBJECT

public:
    explicit AppMenuRegistrar(QObject *parent = nullptr);
    ~AppMenuRegistrar() override;

    /// 注册服务名与对象路径，并开始扫描；失败时通过 errorMessage 返回原因
    bool start(QString *errorMessage = nullptr, bool replaceExisting = false);

    // 供适配器调用
    void handleRegisterWindow(uint windowId, const QString &service, const QString &path);
    void handleRegisterSurface(uint pid, const QString &service, const QString &path);
    void handleUnregister(uint id);

    QList<AppMenuEntry> entries() const;
    AppMenuEntry entryForPid(uint pid) const;
    AppMenuEntry entryForWindow(uint windowId) const;

Q_SIGNALS:
    void entryRegistered(const AppMenuEntry &entry);
    void entryUnregistered(uint windowId);

private Q_SLOTS:
    void onNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    static QString menuKey(const QString &service, const QString &path);

    void scanExistingNames();
    void probe(const QString &service, int attempt);
    void scheduleProbe(const QString &service, int attempt);
    void syncMenus(const QString &service, const QStringList &paths);
    void removeService(const QString &service);
    void addOrUpdate(const AppMenuEntry &entry);
    void dropEntriesOwnedBy(uint windowId);
    uint pidOf(const QString &service) const;
    uint nextSyntheticId();

    /// 一次排队等发车的 Introspect。所有探测都从这里走：开机会话里有上百条
    /// 总线连接，一次性齐发会在总线上打出一波尖峰，也会让 broker 的未读消息
    /// 记账暴涨（见 registrar.cpp 里 kProbeTimeoutMs 的说明）。
    struct ProbeRequest {
        QString service;
        int attempt = 0;
    };
    void enqueueProbe(const QString &service, int attempt);
    void pumpProbeQueue();
    void noteProbeTimeout(const QString &service);
    bool probeSuppressed(const QString &service) const;

    RegistrarAdaptor *m_adaptor = nullptr;
    QHash<QString, AppMenuEntry> m_menus; // key = service|path
    QHash<QString, int> m_probeAttempts;
    QSet<QString> m_services; // 当前在总线上的唯一名
    QString m_ownUniqueName;
    uint m_synthetic = 0x80000000u;

    QQueue<ProbeRequest> m_probeQueue;
    QTimer *m_pump = nullptr;   // 探测发车节流用的单次定时器
    int m_inFlight = 0;         // 还没回来的 Introspect 数
    QSet<QString> m_unreachable;        // 叫不醒的连接：owner 变化前不再探测
    QHash<QString, int> m_timeouts;     // 连续超时次数
    QHash<QString, qint64> m_retryAfter; // 下次最早可以再试的时间（ms since epoch）
    mutable QHash<QString, uint> m_pidCache;
};

#endif // REGISTRAR_H
