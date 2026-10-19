#ifndef DBUSMENUMODEL_H
#define DBUSMENUMODEL_H

#include "types.h"

#include <QAbstractListModel>
#include <QDBusMessage>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <qqmlregistration.h>

class DBusMenuModel;

/**
 * com.canonical.dbusmenu 的客户端模型。
 *
 * 一个实例 = 一层菜单（parentId 为父项 id，顶栏是 0）。
 * 布局通过 GetLayout(parentId, 1) 拉取，子菜单按需由 subMenu(row) 懒加载；
 * 点击走 Event(id, "clicked")，展开/收起走 AboutToShow + Event("opened"/"closed")，
 * 应用侧 LayoutUpdated / ItemsPropertiesUpdated 会实时刷新。
 */
class DBusMenuModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString service READ service WRITE setService NOTIFY serviceChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(int parentId READ parentId WRITE setParentId NOTIFY parentIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, // 菜单项 id（打开子菜单/触发事件用）
        LabelRole,                 // 原始 label，含 '_' 助记符
        TextRole,                  // 去掉助记符后的显示文本
        EnabledRole,
        VisibleRole,
        SeparatorRole,
        IconNameRole,
        ToggleTypeRole,   // "" / "checkmark" / "radio"
        ToggleStateRole,  // 0 未选中，1 选中
        ToggleableRole,
        HasSubMenuRole,
        ShortcutRole,
        ItemTypeRole      // "standard" / "separator"
    };
    Q_ENUM(Roles)

    explicit DBusMenuModel(QObject *parent = nullptr);
    ~DBusMenuModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString service() const;
    void setService(const QString &service);

    QString path() const;
    void setPath(const QString &path);

    int parentId() const;
    void setParentId(int id);

    int count() const;
    bool isReady() const;

    /// 触发一个叶子菜单项（等价于点击）
    Q_INVOKABLE void activate(int row);
    /// 悬停通知，部分应用会用它更新状态
    Q_INVOKABLE void hover(int row);
    /// 取得（必要时创建）第 row 项的子菜单模型
    Q_INVOKABLE QObject *subMenu(int row);
    /// 子菜单即将展开：AboutToShow + Event("opened")
    Q_INVOKABLE void subMenuOpened(int row);
    /// 子菜单已收起：Event("closed")
    Q_INVOKABLE void subMenuClosed(int row);
    /// 重新拉取布局
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void serviceChanged();
    void pathChanged();
    void parentIdChanged();
    void countChanged();
    void readyChanged();
    void activated(int row);

private Q_SLOTS:
    void onLayoutUpdated(const QDBusMessage &message);
    void onItemsPropertiesUpdated(const QDBusMessage &message);

private:
    struct Entry
    {
        qint32 id = 0;
        QVariantMap properties;
    };

    void scheduleRefresh();
    void doRefresh();
    void applyRoot(const DBusMenuLayoutItem &root);
    void clearRows();
    void clearSubModels();
    void setReady(bool ready);
    void resubscribeSignals();
    void sendEvent(qint32 id, const QString &eventId);
    int rowForId(qint32 id) const;

    QString m_service;
    QString m_path;
    int m_parentId = 0;

    QList<Entry> m_items;
    QHash<qint32, QPointer<DBusMenuModel>> m_subModels;
    QTimer m_refreshTimer;
    quint64 m_generation = 0;
    bool m_ready = false;
    bool m_signalsConnected = false;
};

#endif // DBUSMENUMODEL_H
