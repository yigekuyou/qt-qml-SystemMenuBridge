/**
 * appmenu-bind-test —— 回归测试：托盘菜单不能抢走应用菜单栏
 *
 * 场景（和 QSystemTrayIcon / KStatusNotifierItem 完全一致）：
 *   - pid 相同的同一条应用连接导出真正的菜单栏 /MenuBar/1；
 *   - 另一条连接导出 org.kde.StatusNotifierItem + 裸 /MenuBar 的托盘菜单。
 * 旧实现按 path 字典序会选中 /MenuBar，面板显示"显示主窗口 / 退出"；
 * 现在必须绑定 /MenuBar/1，并且注册表里不能留下裸 /MenuBar。
 *
 * CTest 通过 dbus-run-session 启动，避免和桌面上正在运行的 registrar 抢名字：
 *   dbus-run-session -- appmenu-bind-test <path-to-appmenu-fakeapp>
 */
#include "appmenuservice.h"
#include "dbusmenumodel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QProcess>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("appmenu-bind-test"));

    if (argc < 2) {
        qCritical() << "usage: appmenu-bind-test <appmenu-fakeapp>";
        return 2;
    }

    AppMenuService service;

    QProcess fake;
    fake.setProcessChannelMode(QProcess::ForwardedChannels);
    fake.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("--tray")});
    if (!fake.waitForStarted(5000)) {
        qCritical() << "cannot start fakeapp:" << fake.errorString();
        return 2;
    }
    const uint pid = uint(fake.processId());

    bool activated = false;
    bool passed = false;

    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (!activated) {
            if (!service.hasMenuFor(pid))
                return;
            service.activatePid(pid);
            activated = true;
            return;
        }

        if (service.activePath() != QLatin1String("/MenuBar/1"))
            return;

        DBusMenuModel *model = service.menuModel();
        if (!model || !model->isReady() || model->count() < 3)
            return;

        QStringList texts;
        for (int row = 0; row < model->count(); ++row)
            texts.append(model->data(model->index(row, 0), DBusMenuModel::TextRole).toString());

        QStringList problems;

        // 托盘菜单不应进入注册表
        const QVariantList menus = service.menus();
        for (const QVariant &item : menus) {
            const QVariantMap map = item.toMap();
            if (map.value(QStringLiteral("pid")).toUInt() == pid
                && map.value(QStringLiteral("path")).toString() == QLatin1String("/MenuBar")) {
                problems.append(QStringLiteral("tray menu /MenuBar was registered"));
            }
        }

        if (texts != QStringList{QStringLiteral("File"), QStringLiteral("Edit"),
                                 QStringLiteral("View")}) {
            problems.append(QStringLiteral("unexpected top-level items: %1")
                                .arg(texts.join(QStringLiteral(", "))));
        }

        if (!problems.isEmpty()) {
            qCritical().noquote() << "FAIL:" << problems.join(QStringLiteral("; "));
            return;
        }

        passed = true;
        qInfo().noquote() << "PASS: bound" << service.activePath() << "with" << texts.join('/');
        poll.stop();
        fake.terminate();
        QCoreApplication::exit(0);
    });
    poll.start();

    // 15 秒总超时：正常情况下 1 秒内就能完成
    QTimer::singleShot(15000, &app, [&]() {
        qCritical().noquote()
            << "FAIL: timeout; activePath =" << service.activePath()
            << "; menus =" << service.menus()
            << "; modelCount =" << (service.menuModel() ? service.menuModel()->count() : -1);
        fake.kill();
        QCoreApplication::exit(1);
    });

    const int result = app.exec();
    if (!passed && result == 0)
        return 1;
    return result;
}
