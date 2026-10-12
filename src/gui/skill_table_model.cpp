#include "skill_table_model.h"
#include "../db/dao_skill.h"
#include <QDateTime>
#include <QIcon>
#include <QBrush>
#include <QFont>
#include <QDebug>

SkillTableModel::SkillTableModel(QSqlDatabase db, QObject* parent)
    : QAbstractTableModel(parent)
    , m_db(db)
{
}

void SkillTableModel::setFilter(const QString& category, const QString& search) {
    m_category = category;
    m_search = search;
    reload();
}

void SkillTableModel::reload() {
    beginResetModel();
    m_data = DaoSkill::allSkills(m_db, /*enabledOnly=*/false, m_category, m_search);
    // 排序：isNew 优先 → 再按 name
    std::sort(m_data.begin(), m_data.end(), [](const Skill& a, const Skill& b) {
        if (a.isNew != b.isNew) return a.isNew > b.isNew;
        return a.name.toLower() < b.name.toLower();
    });
    endResetModel();
}

int SkillTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_data.size();
}

int SkillTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant SkillTableModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid()) return {};
    int row = idx.row();
    if (row < 0 || row >= m_data.size()) return {};
    const Skill& s = m_data.at(row);

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case ColName:     return s.name;
            case ColCategory: return s.category.isEmpty() ? QObject::tr("未分类") : s.category;
            case ColSource:   return s.source.isEmpty() ? QStringLiteral("local") : s.source;
            case ColEnabled:  return s.enabled ? QStringLiteral("✓") : QStringLiteral("✗");
            case ColSize:     return formatFileSize(s.fileSize);
            case ColMtime:    return QDateTime::fromSecsSinceEpoch(s.mtime).toString("yyyy-MM-dd HH:mm");
        }
    }
    if (role == Qt::ToolTipRole) {
        return QString("%1\n目录: %2\n描述: %3")
            .arg(s.name, s.directory, s.description);
    }
    if (role == Qt::ForegroundRole && idx.column() == ColName && s.isNew) {
        return QBrush(Qt::darkBlue);
    }
    if (role == Qt::FontRole && idx.column() == ColName && s.isNew) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled) {
        return s.enabled ? Qt::Checked : Qt::Unchecked;
    }
    return {};
}

QVariant SkillTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case ColName:     return QObject::tr("名称");
        case ColCategory: return QObject::tr("分类");
        case ColSource:   return QObject::tr("来源");
        case ColEnabled:  return QObject::tr("启用");
        case ColSize:     return QObject::tr("大小");
        case ColMtime:    return QObject::tr("更新时间");
    }
    return {};
}

const Skill& SkillTableModel::skillAt(int row) const {
    return m_data.at(row);
}

Skill SkillTableModel::skillAtCopy(int row) const {
    return m_data.at(row);
}

QString SkillTableModel::formatFileSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}
