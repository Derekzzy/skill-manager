#include "category_model.h"
#include "../db/dao_skill.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QSet>
#include <QDebug>

CategoryModel::CategoryModel(QSqlDatabase db, QObject* parent)
    : QAbstractItemModel(parent)
    , m_db(db)
{
    m_rootItem = new TreeItem();
    reload();
}

CategoryModel::~CategoryModel() {
    clearTree();
    delete m_rootItem;
}

void CategoryModel::clearTree() {
    // 递归释放所有子节点
    std::function<void(TreeItem*)> del = [&](TreeItem* p) {
        for (auto* c : p->children) del(c);
        p->children.clear();
    };
    for (auto* c : m_rootItem->children) del(c);
    m_rootItem->children.clear();
}

void CategoryModel::reload() {
    beginResetModel();
    clearTree();
    buildTree();
    computeCounts();
    endResetModel();
}

void CategoryModel::refreshCounts() {
    // 仅重新计算 count，然后通知 dataChanged
    computeCounts();
    // 为每一行发一个 dataChanged，触发视图重绘
    for (int i = 0; i < m_rootItem->children.size(); ++i) {
        QModelIndex idx = index(i, 0, QModelIndex());
        emit dataChanged(idx, idx, { Qt::DisplayRole });
    }
}

void CategoryModel::buildTree() {
    if (!m_db.isOpen()) return;

    // 1. 查所有 categories
    QList<TreeItem*> flat;
    {
        QSqlQuery q(m_db);
        q.prepare("SELECT id, name, parent_id FROM categories ORDER BY sort_order ASC, name ASC");
        if (!q.exec()) {
            qWarning() << "CategoryModel buildTree query failed:" << q.lastError().text();
            return;
        }
        while (q.next()) {
            auto* it = new TreeItem();
            it->id = q.value(0).toInt();
            it->name = q.value(1).toString();
            it->parent = nullptr; // 稍后回填
            flat.append(it);
        }
    }

    // 2. 先把 "未分类" 虚拟根节点塞进去（永远显示在最顶部）
    {
        auto* uncat = new TreeItem();
        uncat->id = -1;
        uncat->name = QObject::tr("未分类");
        uncat->parent = m_rootItem;
        m_rootItem->children.append(uncat);
    }

    // 3. 按 id 建映射，再根据 parent_id 把节点链起来
    QMap<int, TreeItem*> byId;
    for (auto* it : flat) byId[it->id] = it;

    for (auto* it : flat) {
        int pid = 0; // parent_id=0 表示挂根
        if (byId.contains(pid)) {
            it->parent = byId.value(pid);
            byId.value(pid)->children.append(it);
        } else {
            // parent_id 指向不存在的节点，直接挂根
            it->parent = m_rootItem;
            m_rootItem->children.append(it);
        }
    }
}

void CategoryModel::computeCounts() {
    if (!m_db.isOpen()) return;

    QMap<QString, int> counts;
    QSqlQuery q(m_db);
    q.prepare("SELECT category, COUNT(*) FROM skills WHERE enabled = 1 GROUP BY category");
    if (!q.exec()) return;
    while (q.next()) {
        QString cat = q.value(0).toString();
        if (cat.isNull()) cat.clear(); // NULL 也归到空串
        counts[cat] = q.value(1).toInt();
    }

    std::function<void(TreeItem*)> walk = [&](TreeItem* node) {
        // "未分类" 对应 category='' 或 NULL
        if (node->id == -1) {
            node->skillCount = counts.value(QString(), 0);
        } else {
            node->skillCount = counts.value(node->name, 0);
        }
        for (auto* c : node->children) walk(c);
    };
    for (auto* c : m_rootItem->children) walk(c);
}

CategoryModel::TreeItem* CategoryModel::itemForIndex(const QModelIndex& idx) const {
    if (!idx.isValid()) return m_rootItem;
    return static_cast<TreeItem*>(idx.internalPointer());
}

QModelIndex CategoryModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) return {};
    TreeItem* parentItem = itemForIndex(parent);
    if (row >= parentItem->children.size()) return {};
    return createIndex(row, column, parentItem->children.at(row));
}

QModelIndex CategoryModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    TreeItem* childItem = itemForIndex(child);
    TreeItem* parentItem = childItem->parent;
    if (!parentItem || parentItem == m_rootItem) return {};
    // 找到 parentItem 在它爸爸那里的 row
    TreeItem* grand = parentItem->parent;
    if (!grand) grand = m_rootItem;
    int row = grand->children.indexOf(parentItem);
    return createIndex(row, 0, parentItem);
}

int CategoryModel::rowCount(const QModelIndex& parent) const {
    TreeItem* p = itemForIndex(parent);
    return p ? p->children.size() : 0;
}

int CategoryModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return 1;
}

QVariant CategoryModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || role != Qt::DisplayRole) return {};
    TreeItem* it = itemForIndex(idx);
    if (!it) return {};
    if (it->skillCount > 0) {
        return QString("%1 (%2)").arg(it->name).arg(it->skillCount);
    }
    return it->name;
}

QVariant CategoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0) {
        return QObject::tr("分类");
    }
    return {};
}

QString CategoryModel::categoryName(const QModelIndex& idx) const {
    if (!idx.isValid()) return QString();
    TreeItem* it = itemForIndex(idx);
    if (!it) return QString();
    if (it->id == -1) return QString(); // 未分类 → 空 category
    return it->name;
}

bool CategoryModel::isUncategorized(const QModelIndex& idx) const {
    if (!idx.isValid()) return false;
    TreeItem* it = itemForIndex(idx);
    return it && it->id == -1;
}
