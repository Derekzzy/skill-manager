#pragma once
#include <QObject>
#include <QMap>
#include <QStringList>
#include <QSqlDatabase>
#include "skill.h"

// 自动分类器：根据 Skill 的 name + description + body 前 2000 字，匹配 DB categories 表关键词词典
class Classifier : public QObject {
    Q_OBJECT
public:
    explicit Classifier(QSqlDatabase db, QObject* parent = nullptr);

    // 返回主分类名；outTags 非空则填充所有匹配分类（逗号分隔）
    QString classify(const Skill& s, QString* outTags = nullptr);

    // 批量重分类已入库的所有 skill（读 DB → classify → update DB）
    void reclassifyAll();

    // 重新加载词典（categories 表变化后调用）
    void reload();

signals:
    void progress(int current, int total);

private:
    QSqlDatabase m_db;
    // 缓存词典：category name → keywords 列表（从 JSON 数组解析）
    QMap<QString, QStringList> m_dict;

    void loadDict();
};
