#ifndef APPMENUSERVICE_H
#define APPMENUSERVICE_H

#include "types.h"

#include <QDBusMessage>
#include <QObject>
#include <QVariantList>
#include <qqmlregistration.h>

#include "dbusmenumodel.h"
class AppMenuRegistrar;
class QDBusServiceWatcher;
class QTimer;

/**
 * QML 侧的全局菜单入口。
 *
 * 负责：
 *  - 监视 com.canonical.AppMenu.Registrar 是否在线；
 *  - 通过扩展方法 GetMenuList / GetMenuForPid 拿到 pid → (service, path) 映射；
 *  - 按 pid 绑定"当前活动窗口"的菜单，并给出可直接渲染的 DBusMenuModel。
 *
 * 典型用法（Quickshell / 任意 Qt Quick 环境）：
 *
 *     AppMenuService { id: appMenu }
 *     Repeater { model: appMenu.menuModel; ... }
 *     // 窗口切换时：
 *     appMenu.activatePid(activeWindow.pid)
 *
 * 为什么按 pid：Wayland 没有 X11 XID，RegisterWindow 的 windowId 不可用，
 * 而合成器（Hyprland 等）能给出活动窗口的 pid，这里用 pid 做关联键。
 */
class AppMenuService : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool ownsRegistrar READ ownsRegistrar NOTIFY ownsRegistrarChanged)
    Q_PROPERTY(QVariantList menus READ menus NOTIFY menusChanged)
    Q_PROPERTY(uint activePid READ activePid NOTIFY activeChanged)
    Q_PROPERTY(QString activeService READ activeService NOTIFY activeChanged)
    Q_PROPERTY(QString activePath READ activePath NOTIFY activeChanged)
    Q_PROPERTY(DBusMenuModel *menuModel READ menuModel NOTIFY menuModelChanged)

public:
    explicit AppMenuService(QObject *parent = nullptr);
    ~AppMenuService() override;

    bool available() const;
    /// 本进程是否占住了 com.canonical.AppMenu.Registrar
    bool ownsRegistrar() const;
    QVariantList menus() const;
    uint activePid() const;
    QString activeService() const;
    QString activePath() const;
    DBusMenuModel *menuModel() const;

    /// 绑定某个进程的菜单（窗口切换时调用）
    Q_INVOKABLE void activatePid(uint pid);
    /// 解除绑定
    Q_INVOKABLE void clearActive();
    /// 重新向 registrar 拉取菜单列表
    Q_INVOKABLE void reload();

Q_SIGNALS:
    void availableChanged();
    void ownsRegistrarChanged();
    void menusChanged();
    void activeChanged();
    void menuModelChanged();

private Q_SLOTS:
    void onRegistrarAppeared();
    void onRegistrarVanished();
    void onRegistrarSignal(const QDBusMessage &message);

private:
    void setAvailable(bool value);
    void setOwnsRegistrar(bool value);
    /// 没人占用 registrar 名字时，就在本进程里把它占住（不再需要单独的守护进程）
    void startEmbeddedRegistrar();
    void scheduleReload();
    void applyActive(const AppMenuEntry &entry);
    AppMenuEntry findEntryForPid(uint pid) const;

    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer *m_reloadTimer = nullptr;
    QList<AppMenuEntry> m_entries;
    DBusMenuModel *m_model = nullptr;
    AppMenuRegistrar *m_registrar = nullptr;

    bool m_available = false;
    bool m_ownsRegistrar = false;
    uint m_activePid = 0;
    uint m_pendingPid = 0;
    QString m_activeService;
    QString m_activePath;
};

#endif // APPMENUSERVICE_H
