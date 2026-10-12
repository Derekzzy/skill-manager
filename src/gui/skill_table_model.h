#pragma once
#include <QAbstractTableModel>
#include <QSqlDatabase>
#include "../core/skill.h"

// Skill 列表表格模型：内部缓存 QList<Skill>，按 name/category/mtime 排序。
class SkillTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColName     = 0,
        ColCategory = 1,
        ColSource   = 2,
        ColEnabled  = 3,
        ColSize     = 4,
        ColMtime    = 5,
        ColCount
    };

    explicit SkillTableModel(QSqlDatabase db, QObject* parent = nullptr);

    // 设置过滤条件并 reload
    void setFilter(const QString& category, const QString& search);
    void reload(); // 用当前 filter 重新查 DAO

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // 直接访问当前选中的 Skill（主窗口用来读详情）
    const Skill& skillAt(int row) const;
    Skill skillAtCopy(int row) const; // 拷贝一份，后续修改会回写 DAO

    static QString formatFileSize(qint64 bytes);

private:
    QSqlDatabase m_db;
    QList<Skill> m_data;
    QString m_category;
    QString m_search;
};
