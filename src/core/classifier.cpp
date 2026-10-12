#include "classifier.h"
#include "../db/dao_skill.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>

// 轻量繁体→简体常用字映射（覆盖书名/人名中高频繁体字）
static QString toSimplified(const QString& s) {
    static const QMap<QChar, QChar> map = {
        {QChar(u'澤'), QChar(u'泽')}, {QChar(u'東'), QChar(u'东')}, {QChar(u'華'), QChar(u'华')},
        {QChar(u'國'), QChar(u'国')}, {QChar(u'軍'), QChar(u'军')}, {QChar(u'戰'), QChar(u'战')},
        {QChar(u'長'), QChar(u'长')}, {QChar(u'龍'), QChar(u'龙')}, {QChar(u'馬'), QChar(u'马')},
        {QChar(u'鳳'), QChar(u'凤')}, {QChar(u'會'), QChar(u'会')}, {QChar(u'東'), QChar(u'东')},
        {QChar(u'蔣'), QChar(u'蒋')}, {QChar(u'毛'), QChar(u'毛')}, {QChar(u'澤'), QChar(u'泽')},
        {QChar(u'東'), QChar(u'东')}, {QChar(u'邊'), QChar(u'边')}, {QChar(u'過'), QChar(u'过')},
        {QChar(u'後'), QChar(u'后')}, {QChar(u'裡'), QChar(u'里')}, {QChar(u'時'), QChar(u'时')},
        {QChar(u'與'), QChar(u'与')}, {QChar(u'個'), QChar(u'个')}, {QChar(u'點'), QChar(u'点')},
        {QChar(u'發'), QChar(u'发')}, {QChar(u'書'), QChar(u'书')}, {QChar(u'無'), QChar(u'无')},
        {QChar(u'體'), QChar(u'体')}, {QChar(u'開'), QChar(u'开')}, {QChar(u'關'), QChar(u'关')},
        {QChar(u'進'), QChar(u'进')}, {QChar(u'出'), QChar(u'出')}, {QChar(u'愛'), QChar(u'爱')},
        {QChar(u'為'), QChar(u'为')}, {QChar(u'麼'), QChar(u'么')}, {QChar(u'讓'), QChar(u'让')},
        {QChar(u'這'), QChar(u'这')}, {QChar(u'樣'), QChar(u'样')}, {QChar(u'種'), QChar(u'种')},
        {QChar(u'還'), QChar(u'还')}, {QChar(u'簡'), QChar(u'简')}, {QChar(u'單'), QChar(u'单')},
        {QChar(u'號'), QChar(u'号')}, {QChar(u'對'), QChar(u'对')}, {QChar(u'產'), QChar(u'产')},
        {QChar(u'動'), QChar(u'动')}, {QChar(u'勞'), QChar(u'劳')}, {QChar(u'運'), QChar(u'运')},
        {QChar(u'會'), QChar(u'会')}, {QChar(u'當'), QChar(u'当')}, {QChar(u'內'), QChar(u'内')},
        {QChar(u'外'), QChar(u'外')}, {QChar(u'處'), QChar(u'处')}, {QChar(u'務'), QChar(u'务')},
        {QChar(u'說'), QChar(u'说')}, {QChar(u'語'), QChar(u'语')}, {QChar(u'讀'), QChar(u'读')},
        {QChar(u'寫'), QChar(u'写')}, {QChar(u'認'), QChar(u'认')}, {QChar(u'識'), QChar(u'识')},
        {QChar(u'問'), QChar(u'问')}, {QChar(u'答'), QChar(u'答')}, {QChar(u'請'), QChar(u'请')},
        {QChar(u'謝'), QChar(u'谢')}, {QChar(u'試'), QChar(u'试')}, {QChar(u'驗'), QChar(u'验')},
        {QChar(u'學'), QChar(u'学')}, {QChar(u'國'), QChar(u'国')}, {QChar(u'黨'), QChar(u'党')},
        {QChar(u'權'), QChar(u'权')}, {QChar(u'爭'), QChar(u'争')}, {QChar(u'義'), QChar(u'义')},
        {QChar(u'鄉'), QChar(u'乡')}, {QChar(u'親'), QChar(u'亲')}, {QChar(u'縣'), QChar(u'县')},
        {QChar(u'區'), QChar(u'区')}, {QChar(u'團'), QChar(u'团')}, {QChar(u'體'), QChar(u'体')},
        {QChar(u'復'), QChar(u'复')}, {QChar(u'舊'), QChar(u'旧')}, {QChar(u'歲'), QChar(u'岁')},
        {QChar(u'葉'), QChar(u'叶')}, {QChar(u'萬'), QChar(u'万')}, {QChar(u'無'), QChar(u'无')},
    };
    QString r = s;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        r.replace(it.key(), it.value());
    return r;
}

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
    // 拼成大字符串 + 繁体→简体转换
    QString raw = s.name + " " + s.description + " " + s.body.left(2000);
    QString text = toSimplified(raw).toLower();

    // ---- 辅助：计算某个分类的命中数 ----
    auto countHits = [&](const QStringList& kws) -> int {
        int n = 0;
        for (const QString& kw : kws) {
            if (kw.isEmpty()) continue;
            if (text.contains(kw.toLower())) n++;
        }
        return n;
    };

    // ========== 第一阶段：匹配内容类（排除 Agent工具） ==========
    QMap<QString, int> hits;
    QStringList contentCats;
    for (auto it = m_dict.cbegin(); it != m_dict.cend(); ++it) {
        if (it.key() == "Agent工具") continue;
        const QStringList& kws = it.value();
        int hit = countHits(kws);
        if (hit >= 1) {
            hits.insert(it.key(), hit);
            contentCats.append(it.key());
        }
    }

    if (!contentCats.isEmpty()) {
        // 内容类命中 → 选命中数最多的
        std::sort(contentCats.begin(), contentCats.end(), [&hits](const QString& a, const QString& b) {
            return hits.value(a) > hits.value(b);
        });
        QString mainCat = contentCats.first();
        if (outTags) {
            QStringList rest;
            for (int i = 1; i < contentCats.size(); ++i) rest.append(contentCats.at(i));
            *outTags = rest.join(",");
        }
        return mainCat;
    }

    // ========== 第二阶段：内容类全无命中 → Agent工具兜底（hitCount ≥ 1 即归入） ==========
    auto it = m_dict.find("Agent工具");
    if (it != m_dict.end()) {
        int agentHit = countHits(it.value());
        if (agentHit >= 1) {
            if (outTags) outTags->clear();
            return "Agent工具";
        }
    }

    if (outTags) outTags->clear();
    return "未分类";
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
