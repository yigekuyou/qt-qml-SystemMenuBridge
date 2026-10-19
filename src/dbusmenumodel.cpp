#include "dbusmenumodel.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QDebug>

#include <utility>

namespace {

constexpr auto kMenuInterface = "com.canonical.dbusmenu";

// 只请求渲染需要的属性：QT 应用可能附带 base64 图标（icon-data），
// 全量拉取会让每次刷新传输几百 KB。
const QStringList kMenuProperties = {
    QStringLiteral("label"),
    QStringLiteral("enabled"),
    QStringLiteral("visible"),
    QStringLiteral("type"),
    QStringLiteral("icon-name"),
    QStringLiteral("children-display"),
    QStringLiteral("toggle-type"),
    QStringLiteral("toggle-state"),
    QStringLiteral("shortcut"),
};

/// dbusmenu 用 '_' 标记助记符，'__' 表示一个字面量下划线
QString stripMnemonics(const QString &label)
{
    QString out;
    out.reserve(label.size());

    for (int i = 0; i < label.size(); ++i) {
        const QChar ch = label.at(i);
        if (ch != QLatin1Char('_')) {
            out += ch;
            continue;
        }
        if (i + 1 < label.size() && label.at(i + 1) == QLatin1Char('_')) {
            out += QLatin1Char('_');
            ++i;
        } else if (i + 1 == label.size()) {
            out += QLatin1Char('_'); // 末尾孤立的下划线按字面量处理
        }
        // 其它情况：单个 '_' 只是助记符，丢弃
    }
    return out;
}

/// 把 shortcut 属性（签名 a(as)）格式化成 "Ctrl+Shift+S" 这样的文本
QString shortcutToString(const QVariant &value)
{
    QList<QStringList> chords;

    if (value.metaType() == QMetaType::fromType<QList<QStringList>>()) {
        chords = value.value<QList<QStringList>>();
    } else if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        // a(as)：不要手动 beginArray()，只读的 QDBusArgument 会报警告
        chords = qdbus_cast<QList<QStringList>>(value.value<QDBusArgument>());
    }

    QStringList parts;
    for (const QStringList &chord : std::as_const(chords)) {
        QStringList tokens;
        tokens.reserve(chord.size());
        for (const QString &token : chord) {
            if (token == QLatin1String("Control"))
                tokens << QStringLiteral("Ctrl");
            else if (token == QLatin1String("Super"))
                tokens << QStringLiteral("Meta");
            else
                tokens << token;
        }
        if (!tokens.isEmpty())
            parts << tokens.join(QLatin1Char('+'));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

DBusMenuModel::DBusMenuModel(QObject *parent)
    : QAbstractListModel(parent)
{
    appmenuRegisterDBusTypes();

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(0);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DBusMenuModel::doRefresh);
}

DBusMenuModel::~DBusMenuModel() = default;

int DBusMenuModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant DBusMenuModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const Entry &entry = m_items.at(index.row());
    const QVariantMap &p = entry.properties;

    switch (role) {
    case IdRole:
        return entry.id;
    case LabelRole:
        return p.value(QStringLiteral("label"));
    case TextRole:
        return stripMnemonics(p.value(QStringLiteral("label")).toString());
    case EnabledRole:
        return p.value(QStringLiteral("enabled"), true).toBool();
    case VisibleRole:
        return p.value(QStringLiteral("visible"), true).toBool();
    case SeparatorRole:
        return p.value(QStringLiteral("type")).toString() == QLatin1String("separator");
    case IconNameRole:
        return p.value(QStringLiteral("icon-name"));
    case ToggleTypeRole:
        return p.value(QStringLiteral("toggle-type"));
    case ToggleStateRole:
        return p.value(QStringLiteral("toggle-state"), 0).toInt();
    case ToggleableRole:
        return !p.value(QStringLiteral("toggle-type")).toString().isEmpty();
    case HasSubMenuRole:
        return p.value(QStringLiteral("children-display")).toString() == QLatin1String("submenu");
    case ShortcutRole:
        return shortcutToString(p.value(QStringLiteral("shortcut")));
    case ItemTypeRole:
        return p.value(QStringLiteral("type"), QStringLiteral("standard")).toString();
    default:
        return {};
    }
}

QHash<int, QByteArray> DBusMenuModel::roleNames() const
{
    return {
        {IdRole, "itemId"},
        {LabelRole, "label"},
        {TextRole, "text"},
        {EnabledRole, "enabled"},
        {VisibleRole, "visible"},
        {SeparatorRole, "separator"},
        {IconNameRole, "iconName"},
        {ToggleTypeRole, "toggleType"},
        {ToggleStateRole, "toggleState"},
        {ToggleableRole, "toggleable"},
        {HasSubMenuRole, "hasSubMenu"},
        {ShortcutRole, "shortcut"},
        {ItemTypeRole, "itemType"},
    };
}

QString DBusMenuModel::service() const
{
    return m_service;
}

void DBusMenuModel::setService(const QString &service)
{
    if (m_service == service)
        return;

    m_service = service;
    emit serviceChanged();

    clearSubModels();
    resubscribeSignals();
    scheduleRefresh();
}

QString DBusMenuModel::path() const
{
    return m_path;
}

void DBusMenuModel::setPath(const QString &path)
{
    if (m_path == path)
        return;

    m_path = path;
    emit pathChanged();

    clearSubModels();
    resubscribeSignals();
    scheduleRefresh();
}

int DBusMenuModel::parentId() const
{
    return m_parentId;
}

void DBusMenuModel::setParentId(int id)
{
    if (m_parentId == id)
        return;

    m_parentId = id;
    emit parentIdChanged();

    clearSubModels();
    scheduleRefresh();
}

int DBusMenuModel::count() const
{
    return int(m_items.size());
}

bool DBusMenuModel::isReady() const
{
    return m_ready;
}

void DBusMenuModel::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit readyChanged();
}

void DBusMenuModel::clearSubModels()
{
    const auto models = m_subModels;
    m_subModels.clear();
    for (const QPointer<DBusMenuModel> &model : models) {
        if (model)
            model->deleteLater();
    }
}

void DBusMenuModel::resubscribeSignals()
{
    auto bus = QDBusConnection::sessionBus();

    if (m_signalsConnected) {
        bus.disconnect(m_service, m_path, QLatin1String(kMenuInterface),
                       QStringLiteral("LayoutUpdated"), this, SLOT(onLayoutUpdated(QDBusMessage)));
        bus.disconnect(m_service, m_path, QLatin1String(kMenuInterface),
                       QStringLiteral("ItemsPropertiesUpdated"), this,
                       SLOT(onItemsPropertiesUpdated(QDBusMessage)));
        m_signalsConnected = false;
    }

    if (m_service.isEmpty() || m_path.isEmpty())
        return;

    const bool ok1 = bus.connect(m_service, m_path, QLatin1String(kMenuInterface),
                                 QStringLiteral("LayoutUpdated"), this,
                                 SLOT(onLayoutUpdated(QDBusMessage)));
    const bool ok2 = bus.connect(m_service, m_path, QLatin1String(kMenuInterface),
                                 QStringLiteral("ItemsPropertiesUpdated"), this,
                                 SLOT(onItemsPropertiesUpdated(QDBusMessage)));

    m_signalsConnected = ok1 || ok2;
}

void DBusMenuModel::scheduleRefresh()
{
    m_refreshTimer.start();
}

void DBusMenuModel::refresh()
{
    doRefresh();
}

void DBusMenuModel::doRefresh()
{
    if (m_service.isEmpty() || m_path.isEmpty()) {
        clearRows();
        setReady(false);
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        m_service, m_path, QLatin1String(kMenuInterface), QStringLiteral("GetLayout"));
    // 只取一层：子菜单在展开时再按需拉取，避免大菜单一次性全量传输。
    call << m_parentId << 1 << kMenuProperties;

    const quint64 generation = ++m_generation;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                if (generation != m_generation)
                    return; // 已经有更新的请求，丢弃这次结果

                const QDBusMessage reply = w->reply();
                if (reply.type() == QDBusMessage::ErrorMessage) {
                    // 应用退出或菜单对象被销毁
                    clearRows();
                    setReady(false);
                    return;
                }

                const QVariantList args = reply.arguments();
                if (args.size() < 2) {
                    clearRows();
                    setReady(false);
                    return;
                }

                applyRoot(appmenuLayoutItem(args.at(1)));
            });
}

void DBusMenuModel::applyRoot(const DBusMenuLayoutItem &root)
{
    QList<Entry> items;
    items.reserve(root.children.size());
    for (const DBusMenuLayoutItem &child : root.children) {
        // 不可见项直接不进模型（和桌面环境里的菜单行为一致）
        if (!child.properties.value(QStringLiteral("visible"), true).toBool())
            continue;
        items.append(Entry{child.id, child.properties});
    }

    bool sameIds = items.size() == m_items.size();
    if (sameIds) {
        for (int i = 0; i < items.size(); ++i) {
            if (items.at(i).id != m_items.at(i).id) {
                sameIds = false;
                break;
            }
        }
    }

    if (sameIds) {
        m_items = items;
        if (!m_items.isEmpty())
            emit dataChanged(index(0), index(int(m_items.size()) - 1));
    } else {
        beginResetModel();
        m_items = items;
        endResetModel();
        emit countChanged();

        // 子菜单模型对应不存在的 id 时释放掉
        for (auto it = m_subModels.begin(); it != m_subModels.end();) {
            bool alive = false;
            for (const Entry &e : std::as_const(m_items)) {
                if (e.id == it.key()) {
                    alive = true;
                    break;
                }
            }
            if (alive) {
                ++it;
            } else {
                if (it.value())
                    it.value()->deleteLater();
                it = m_subModels.erase(it);
            }
        }
    }

    setReady(true);
}

void DBusMenuModel::clearRows()
{
    if (m_items.isEmpty())
        return;

    beginResetModel();
    m_items.clear();
    endResetModel();

    clearSubModels();

    emit countChanged();
}

void DBusMenuModel::onLayoutUpdated(const QDBusMessage &message)
{
    // 签名 (u i)：revision, parent
    const QVariantList args = message.arguments();
    const int parent = args.size() > 1 ? args.at(1).toInt() : -1;
    if (parent < 0 || parent == m_parentId)
        scheduleRefresh();
}

void DBusMenuModel::onItemsPropertiesUpdated(const QDBusMessage &message)
{
    const QVariantList args = message.arguments();
    if (args.size() < 2)
        return;

    const QList<DBusMenuPropertySet> updated = appmenuPropertySets(args.at(0));
    const QList<DBusMenuPropertySet> removed = appmenuPropertySets(args.at(1));

    // libdbusmenu-qt / GTK 的第二个参数是 a(ias)（id + 变化的属性名），
    // 结构不同，单独解析成 id 列表。
    QList<qint32> changedIds;
    if (removed.isEmpty() && args.at(1).metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument arg = args.at(1).value<QDBusArgument>();
        const QString signature = arg.currentSignature();
        if (signature == QLatin1String("(ias)") || signature == QLatin1String("a(ias)")) {
            const auto keys = qdbus_cast<QList<std::pair<int, QStringList>>>(arg);
            for (const auto &key : keys)
                changedIds.append(key.first);
        }
    }

    if (updated.isEmpty() && removed.isEmpty() && changedIds.isEmpty())
        return;

    bool changed = false;

    for (const DBusMenuPropertySet &set : updated) {
        const int row = rowForId(set.id);
        if (row < 0)
            continue;
        for (auto it = set.properties.cbegin(); it != set.properties.cend(); ++it)
            m_items[row].properties.insert(it.key(), it.value());
        changed = true;
    }

    for (const DBusMenuPropertySet &set : removed) {
        const int row = rowForId(set.id);
        if (row < 0)
            continue;
        for (auto it = set.properties.cbegin(); it != set.properties.cend(); ++it)
            m_items[row].properties.remove(it.key());
        changed = true;
    }

    for (qint32 id : std::as_const(changedIds)) {
        if (rowForId(id) >= 0)
            changed = true;
    }

    if (changed && !m_items.isEmpty())
        emit dataChanged(index(0), index(int(m_items.size()) - 1));
}

int DBusMenuModel::rowForId(qint32 id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

void DBusMenuModel::sendEvent(qint32 id, const QString &eventId)
{
    if (m_service.isEmpty() || m_path.isEmpty())
        return;

    QDBusMessage call = QDBusMessage::createMethodCall(
        m_service, m_path, QLatin1String(kMenuInterface), QStringLiteral("Event"));
    call << id << eventId
         << QVariant::fromValue(QDBusVariant(QVariant(QString())))
         << uint(QDateTime::currentSecsSinceEpoch());

    QDBusConnection::sessionBus().asyncCall(call);
}

void DBusMenuModel::activate(int row)
{
    if (row < 0 || row >= m_items.size())
        return;

    const Entry &entry = m_items.at(row);
    if (entry.properties.value(QStringLiteral("type")).toString() == QLatin1String("separator"))
        return;
    if (!entry.properties.value(QStringLiteral("enabled"), true).toBool())
        return;

    sendEvent(entry.id, QStringLiteral("clicked"));
    emit activated(row);
}

void DBusMenuModel::hover(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    sendEvent(m_items.at(row).id, QStringLiteral("hovered"));
}

QObject *DBusMenuModel::subMenu(int row)
{
    if (row < 0 || row >= m_items.size())
        return nullptr;

    const qint32 id = m_items.at(row).id;
    if (DBusMenuModel *existing = m_subModels.value(id).data())
        return existing;

    auto *model = new DBusMenuModel(this);
    model->setService(m_service);
    model->setPath(m_path);
    model->setParentId(id);

    m_subModels.insert(id, model);
    return model;
}

void DBusMenuModel::subMenuOpened(int row)
{
    if (row < 0 || row >= m_items.size())
        return;

    const qint32 id = m_items.at(row).id;

    // 应用可以在这里惰性填充菜单项，need_update == true 时重新拉一次布局
    QDBusMessage call = QDBusMessage::createMethodCall(
        m_service, m_path, QLatin1String(kMenuInterface), QStringLiteral("AboutToShow"));
    call << id;

    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                const QDBusMessage reply = w->reply();
                if (reply.type() == QDBusMessage::ErrorMessage)
                    return;
                const QVariantList args = reply.arguments();
                if (!args.isEmpty() && args.at(0).toBool() && generation == m_generation)
                    scheduleRefresh();
            });

    sendEvent(id, QStringLiteral("opened"));
}

void DBusMenuModel::subMenuClosed(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    sendEvent(m_items.at(row).id, QStringLiteral("closed"));
}
