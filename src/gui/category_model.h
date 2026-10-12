#pragma once
#include <QAbstractItemModel>
#include <QSqlDatabase>

// 分类树模型：从 categories 表读 parent_id 形成层级。
// 同时每个节点显示该分类下 enabled skill 数量（包括未分类的根节点用 category IS NULL/'' 过滤）。
class CategoryModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit CategoryModel(QSqlDatabase db, QObject* parent = nullptr);
    ~CategoryModel() override;

    void reload();        // 重新从 DB 拉取 categories + 重算计数
    void refreshCounts(); // 只刷新 skillCount（启用/禁用后调用）

    // ---- QAbstractItemModel ----
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // 辅助：QModelIndex → category 名（根节点 "未分类" 会返回空字符串以便 DAO allSkills filter）
    QString categoryName(const QModelIndex& idx) const;
    bool    isUncategorized(const QModelIndex& idx) const;

private:
    struct TreeItem {
        QString name;
        int     id = 0;       // 0 = "未分类" 虚拟根节点
        TreeItem* parent = nullptr;
        QList<TreeItem*> children;
        int skillCount = 0;
    };

    TreeItem* m_rootItem = nullptr;
    QSqlDatabase m_db;

    void clearTree();
    void buildTree();
    void computeCounts();

    // 递归查找
    TreeItem* itemForIndex(const QModelIndex& idx) const;
};
