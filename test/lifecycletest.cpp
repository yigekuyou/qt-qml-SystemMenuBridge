#include "appmenuservice.h"
#include "dbusmenumodel.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QProcess>
#include <QTimer>

// 不发送 Register/Unregister，不更换连接，模拟 Konsole 在运行中重建菜单。
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 2;
    AppMenuService service;
    QProcess fake;
    fake.setProcessChannelMode(QProcess::ForwardedChannels);
    fake.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--tray")});
    if (!fake.waitForStarted(5000))
        return 2;
    const uint pid = uint(fake.processId());
    QString owner;
    int phase = 0;
    bool activated = false;
    auto paths = [&]() {
        QStringList result;
        for (const QVariant &item : service.menus()) {
            const auto entry = item.toMap();
            if (entry.value(QStringLiteral("pid")).toUInt() == pid) {
                result.append(entry.value(QStringLiteral("path")).toString());
                if (owner.isEmpty())
                    owner = entry.value(QStringLiteral("service")).toString();
            }
        }
        return result;
    };
    auto control = [&](const QString &method, const QString &path) {
        QDBusInterface iface(owner, QStringLiteral("/Control"),
                             QStringLiteral("test.appmenu.Control"));
        const QDBusReply<bool> reply = iface.call(method, path);
        if (!reply.isValid() || !reply.value()) {
            qCritical() << "Control failed" << method << path << reply.error();
            app.exit(1);
            return false;
        }
        return true;
    };
    auto ready = [&](const QString &path) {
        return service.activePath() == path && service.menuModel()
               && service.menuModel()->isReady() && service.menuModel()->count() == 3;
    };
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        const QStringList current = paths();
        // 裸 /MenuBar 是另一条连接的托盘菜单，整个生命周期都不能进入注册表。
        if (current.contains(QStringLiteral("/MenuBar"))) {
            qCritical() << "Tray menu was registered";
            app.exit(1);
            return;
        }
        if (!activated && service.hasMenuFor(pid)) {
            activated = true;
            service.activatePid(pid);
            return;
        }
        if (phase == 0 && current == QStringList{QStringLiteral("/MenuBar/1")}
            && ready(QStringLiteral("/MenuBar/1"))) {
            if (control(QStringLiteral("AddMenu"), QStringLiteral("/MenuBar/2")))
                phase = 1;
        } else if (phase == 1 && current.size() == 2) {
            if (control(QStringLiteral("DropMenu"), QStringLiteral("/MenuBar/1")))
                phase = 2;
        } else if (phase == 2 && current == QStringList{QStringLiteral("/MenuBar/2")}
                   && ready(QStringLiteral("/MenuBar/2"))) {
            if (control(QStringLiteral("DropMenu"), QStringLiteral("/MenuBar/2")))
                phase = 3;
        } else if (phase == 3 && current.isEmpty() && service.activePath().isEmpty()
                   && (!service.menuModel() || service.menuModel()->count() == 0)) {
            if (control(QStringLiteral("AddMenu"), QStringLiteral("/MenuBar/3")))
                phase = 4;
        } else if (phase == 4 && current == QStringList{QStringLiteral("/MenuBar/3")}
                   && ready(QStringLiteral("/MenuBar/3"))) {
            if (control(QStringLiteral("DropMenu"), QStringLiteral("/MenuBar")))
                phase = 5;
        } else if (phase == 5 && current.isEmpty() && service.activePath().isEmpty()) {
            if (control(QStringLiteral("AddMenu"), QStringLiteral("/MenuBar/4")))
                phase = 6;
        } else if (phase == 6 && current == QStringList{QStringLiteral("/MenuBar/4")}
                   && ready(QStringLiteral("/MenuBar/4"))) {
            qInfo() << "PASS: new path discovered, stale paths removed, empty/absent subtree cleared, binding restored";
            app.exit(0);
        }
    });
    poll.start();
    QTimer::singleShot(20000, &app, [&]() {
        qCritical() << "Lifecycle timeout, phase" << phase << service.menus()
                    << service.activePath();
        app.exit(1);
    });
    const int result = app.exec();
    fake.terminate();
    if (!fake.waitForFinished(3000)) {
        fake.kill();
        fake.waitForFinished(3000);
    }
    return result;
}
