#pragma once
#include <QObject>
#include <QSqlDatabase>
#include "repo.h"

// GitHub/Gitee 仓库管理：封装 git clone / fetch / pull / remove
// 耗时操作 wrap 在 QtConcurrent::run 里返回 QFuture，用 QFutureWatcher 发信号
class RepoManager : public QObject {
    Q_OBJECT
public:
    explicit RepoManager(QSqlDatabase db, QObject* parent = nullptr);

    // 添加仓库：git clone 到 doubaoSkillsRoot + repo_name
    // 同步执行（clone 本身很快因为 --depth=1），失败返回空 Repo（id=0）
    Repo addRepo(const QString& url, const QString& doubaoSkillsRoot);

    // 检查远端是否有更新（git ls-remote，不修改工作区）
    // 同步：先跑 ls-remote 拿远端 HEAD hash，对比 repo.currentCommit
    void checkUpdate(Repo& repo);

    // 执行 pull 更新（git pull --ff-only）
    // 更新 currentCommit、清空 hasUpdate
    void pullUpdate(Repo& repo);

    // 删除仓库：删目录 + DB 记录 + 对应 skill 记录
    bool removeRepo(Repo& repo, QString* err = nullptr);

signals:
    void repoAdded(const Repo& r);
    void repoUpdated(const Repo& r);
    void updateChecked(const Repo& r, bool hasUpdate);
    void error(const QString& msg);

private:
    QSqlDatabase m_db;

    struct GitResult { int code; QString out; QString err; };
    GitResult runGit(const QString& workDir,
                     const QStringList& args,
                     int timeoutMs = 60000);

    static QString repoNameFromUrl(const QString& url);
    static QString providerFromUrl(const QString& url);
};
