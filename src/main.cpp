#include "registrar.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

/**
 * appmenu-registrar —— 全局菜单 registrar 守护进程
 *
 * 它做的唯一一件事：在会话总线上占住 com.canonical.AppMenu.Registrar，
 * 让 Qt / Chromium / Firefox 把菜单树导出到 /MenuBar/<n>，然后把
 * (pid, service, path) 整理好交给面板/bar（AppMenuService）。
 *
 * 之所以必须由外部进程占名：QGenericUnixTheme 只有在总线上存在这个
 * registrar 时才会创建 QDBusMenuBar，否则 Qt 应用根本不导出菜单。
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("appmenu-registrar"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("D-Bus global menu registrar for Wayland/Hyprland (no compositor protocol needed)"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption replaceOption(
        QStringList{QStringLiteral("r"), QStringLiteral("replace")},
        QStringLiteral("Replace an already running registrar (e.g. a stale one)."));
    parser.addOption(replaceOption);
    parser.process(app);

    AppMenuRegistrar registrar;
    QString error;
    if (!registrar.start(&error, parser.isSet(replaceOption))) {
        qCritical().noquote() << "appmenu-registrar:" << error;
        return 1;
    }

    return app.exec();
}
