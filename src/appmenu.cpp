#include "appmenu.h"

AppMenu::AppMenu(QObject *parent)
    : QObject(parent)
    , m_service(new AppMenuService(this))
{
    connect(m_service, &AppMenuService::availableChanged, this, &AppMenu::availableChanged);
    connect(m_service, &AppMenuService::ownsRegistrarChanged, this, &AppMenu::ownsRegistrarChanged);
    connect(m_service, &AppMenuService::menusChanged, this, &AppMenu::menusChanged);
    connect(m_service, &AppMenuService::activeChanged, this, &AppMenu::activeChanged);
    connect(m_service, &AppMenuService::menuModelChanged, this, &AppMenu::menuModelChanged);
}

AppMenuService *AppMenu::service() const
{
    return m_service;
}

bool AppMenu::available() const
{
    return m_service->available();
}

bool AppMenu::ownsRegistrar() const
{
    return m_service->ownsRegistrar();
}

QVariantList AppMenu::menus() const
{
    return m_service->menus();
}

int AppMenu::menuCount() const
{
    return int(m_service->menus().size());
}

int AppMenu::activePid() const
{
    return int(m_service->activePid());
}

QString AppMenu::activeService() const
{
    return m_service->activeService();
}

QString AppMenu::activePath() const
{
    return m_service->activePath();
}

DBusMenuModel *AppMenu::menuModel() const
{
    return m_service->menuModel();
}

void AppMenu::activatePid(uint pid)
{
    m_service->activatePid(pid);
}

void AppMenu::clearActive()
{
    m_service->clearActive();
}

void AppMenu::reload()
{
    m_service->reload();
}

bool AppMenu::hasMenuFor(uint pid) const
{
    const QVariantList list = m_service->menus();
    for (const QVariant &item : list) {
        if (item.toMap().value(QStringLiteral("pid")).toUInt() == pid)
            return true;
    }
    return false;
}
