#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

/**
 * appmenu-test —— 用一条顶栏验证整条链路：
 *   appmenu-registrar → AppMenuService(pid) → DBusMenuModel → QML 渲染
 *
 * 设置 APPMENU_TEST_SCREENSHOT=/tmp/menu.png 时会在 1.5s 后截图并退出，
 * 方便在无头环境做冒烟测试。
 */
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("appmenu-test"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(APPMENU_QML_IMPORT_DIR));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("yigekuyou.appmenu.test", "Main");

    const QByteArray screenshot = qgetenv("APPMENU_TEST_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        QTimer::singleShot(1500, &app, [&engine, screenshot]() {
            for (QObject *root : engine.rootObjects()) {
                if (auto *window = qobject_cast<QQuickWindow *>(root)) {
                    const QImage image = window->grabWindow();
                    if (!image.isNull())
                        image.save(QString::fromLocal8Bit(screenshot));
                }
            }
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
