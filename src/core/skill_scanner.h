#pragma once
#include <QObject>
#include <QList>
#include <QSet>
#include "skill.h"
#include "repo.h"

// 整个系统的入口扫描器：给定根目录递归找所有 SKILL.md，解析后返回 Skill 列表
class SkillScanner : public QObject {
    Q_OBJECT
public:
    explicit SkillScanner(QObject* parent = nullptr);

    // 扫描一个根目录
    // repo: 如果来自 git 仓库（有 remote_repos 记录）传入 Repo 指针，否则 nullptr
    QList<Skill> scanRoot(const QString& rootPath, Repo* repo = nullptr);

    // 扫描整个 doubaoSkillsRoot：先遍历 git 仓库目录，再扫纯本地目录
    // existingRepos 是 RepoManager 里已知的 git repo 列表，用来标记 source=git-sub
    QList<Skill> scanAll(const QString& doubaoSkillsRoot,
                         const QList<Repo>& existingRepos);

signals:
    void progress(int current, int total, const QString& currentPath);

private:
    // 在目录下递归找所有 SKILL.md 路径，返回绝对路径列表
    QStringList collectSkillMdPaths(const QString& rootPath);

    // 解析一个 SKILL.md 文件
    Skill parseSkillMd(const QString& skillMdPath, Repo* repo);

    // ---- frontmatter 解析 ----
    struct Frontmatter {
        QMap<QString, QString> scalarFields;   // 简单 key: value
        QMap<QString, QStringList> listFields; // key: - v1\n- v2
        QString body;
    };
    static Frontmatter parseFrontmatter(const QString& mdContent);

    // ---- 字段处理 ----
    static QString cleanName(const QString& rawName);
    static QString canonicalName(const QString& cleanNameIn);
    static bool    containsNewMark(const QString& s);
    static QString inferStandard(const QString& skillMdPath,
                                 const QString& description,
                                 bool hasAllowedToolsOrCompat);
};
