#ifndef APPMENU_H
#define APPMENU_H

#include "appmenuservice.h"
#include "dbusmenumodel.h"

#include <QObject>
#include <QVariantList>
#include <qqmlregistration.h>

/**
 * 模块级 QML 单例：
 *
 *     import com.github.yigekuyou.appmenu
 *     AppMenu.menuModel          // 当前活动窗口的顶层菜单
 *     AppMenu.activatePid(pid)   // 跟随焦点窗口
 *
 * 纯 C++ 实现（QML_SINGLETON），工程里没有 .qml 文件；
 * 只是把 AppMenuService 包一层，好让 Bar/面板直接拿到一个全局对象，
 * 不用自己 AppMenuService { id: ... }。
 */
class AppMenu : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// 底层服务对象（要精细控制时直接用）
    Q_PROPERTY(AppMenuService *service READ service CONSTANT)

    /// registrar 是否在线（本模块自己占名，或别的进程已经占着）
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// 这个名字是不是本进程占住的（false = 用的是别人的 registrar）
    Q_PROPERTY(bool ownsRegistrar READ ownsRegistrar NOTIFY ownsRegistrarChanged)

    /// 已发现的菜单：[{ windowId, pid, service, path }, ...]
    Q_PROPERTY(QVariantList menus READ menus NOTIFY menusChanged)
    Q_PROPERTY(int menuCount READ menuCount NOTIFY menusChanged)

    /// 当前绑定的 pid / 总线名 / 对象路径
    Q_PROPERTY(int activePid READ activePid NOTIFY activeChanged)
    Q_PROPERTY(QString activeService READ activeService NOTIFY activeChanged)
    Q_PROPERTY(QString activePath READ activePath NOTIFY activeChanged)

    /// 当前活动窗口的菜单模型（null = 还没绑定）
    Q_PROPERTY(DBusMenuModel *menuModel READ menuModel NOTIFY menuModelChanged)

public:
    explicit AppMenu(QObject *parent = nullptr);

    AppMenuService *service() const;

    bool available() const;
    bool ownsRegistrar() const;
    QVariantList menus() const;
    int menuCount() const;
    int activePid() const;
    QString activeService() const;
    QString activePath() const;
    DBusMenuModel *menuModel() const;

    /// 绑定某个进程的菜单（窗口切换时调用）
    Q_INVOKABLE void activatePid(uint pid);
    /// 解除绑定（焦点窗口没有菜单时用）
    Q_INVOKABLE void clearActive();
    /// 重新向 registrar 拉取菜单列表
    Q_INVOKABLE void reload();
    /// 某个 pid 是否已注册菜单
    Q_INVOKABLE bool hasMenuFor(uint pid) const;

Q_SIGNALS:
    void availableChanged();
    void ownsRegistrarChanged();
    void menusChanged();
    void activeChanged();
    void menuModelChanged();

private:
    AppMenuService *m_service = nullptr;
};

#endif // APPMENU_H
