#include "classifier.h"
#include "../db/dao_skill.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>

Classifier::Classifier(QSqlDatabase db, QObject* parent)
    : QObject(parent)
    , m_db(std::move(db))
{
    loadDict();
}

void Classifier::reload() {
    loadDict();
}

void Classifier::loadDict() {
    m_dict.clear();

    QSqlQuery q(m_db);
    if (!q.exec("SELECT name, keywords FROM categories WHERE auto_assign = 1")) {
        qWarning() << "Classifier::loadDict failed:" << q.lastError().text();
        return;
    }

    while (q.next()) {
        QString name     = q.value("name").toString();
        QString keywords = q.value("keywords").toString();

        // keywords 是 JSON 数组字符串
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(keywords.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) {
            qWarning() << "Classifier::loadDict invalid JSON for category" << name << ":" << keywords;
            continue;
        }

        QStringList list;
        QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            if (v.isString()) list.append(v.toString());
        }
        if (!list.isEmpty()) {
            m_dict.insert(name, list);
        }
    }
}

QString Classifier::classify(const Skill& s, QString* outTags) {
    // 拼成大字符串
    QString text = s.name + " " + s.description + " " + s.body.left(2000);
    text = text.toLower();

    // 每个分类的命中数
    QMap<QString, int> hits;
    QStringList matchedCats;

    for (auto it = m_dict.cbegin(); it != m_dict.cend(); ++it) {
        const QString& catName = it.key();
        const QStringList& kws = it.value();
        int hitCount = 0;
        for (const QString& kw : kws) {
            if (kw.isEmpty()) continue;
            if (text.contains(kw.toLower())) {
                hitCount++;
            }
        }
        // 命中 ≥ 1 且关键词数 ≥ 3（即这个分类至少有 3 个关键词且命中了至少 1 个）
        if (hitCount >= 1 && kws.size() >= 3) {
            hits.insert(catName, hitCount);
            matchedCats.append(catName);
        }
    }

    if (matchedCats.isEmpty()) {
        if (outTags) outTags->clear();
        return "未分类";
    }

    // 按命中数降序排
    std::sort(matchedCats.begin(), matchedCats.end(), [&hits](const QString& a, const QString& b) {
        return hits.value(a) > hits.value(b);
    });

    QString mainCat = matchedCats.first();
    if (outTags) {
        QStringList rest;
        for (int i = 1; i < matchedCats.size(); ++i) rest.append(matchedCats.at(i));
        *outTags = rest.join(",");
    }
    return mainCat;
}

void Classifier::reclassifyAll() {
    QList<Skill> all = DaoSkill::allSkills(m_db);
    int total = all.size();
    for (int i = 0; i < total; ++i) {
        Skill& s = all[i];
        QString tags;
        s.category = classify(s, &tags);
        if (!tags.isEmpty()) {
            // 保留原有 tags 里非分类部分（逗号分隔），追加新匹配的分类
            QStringList existing = s.tags.isEmpty() ? QStringList{}
                                                    : s.tags.split(',', Qt::SkipEmptyParts);
            existing.removeDuplicates();
            QStringList newOnes = tags.split(',');
            for (const QString& t : newOnes) {
                if (!existing.contains(t)) existing.append(t);
            }
            s.tags = existing.join(",");
        }
        DaoSkill::updateSkill(s, m_db);
        emit progress(i + 1, total);
    }
}
