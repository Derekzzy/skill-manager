#include "repo_manager.h"
#include "../db/dao_skill.h"

#include <QProcess>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlError>

RepoManager::RepoManager(QSqlDatabase db, QObject* parent)
    : QObject(parent)
    , m_db(std::move(db))
{}

// ============================================================
// runGit：同步执行 git 命令
// ============================================================

RepoManager::GitResult RepoManager::runGit(const QString& workDir,
                                           const QStringList& args,
                                           int timeoutMs) {
    GitResult r{ -1, {}, {} };

    QProcess proc;
    proc.setProgram("git");
    proc.setArguments(args);
    if (!workDir.isEmpty()) proc.setWorkingDirectory(workDir);

    proc.start(QIODevice::ReadOnly);
    if (!proc.waitForStarted(3000)) {
        r.err = tr("git 启动失败：%1").arg(proc.errorString());
        return r;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        r.err = tr("git 执行超时");
        return r;
    }

    r.code = proc.exitCode();
    r.out  = QString::fromUtf8(proc.readAllStandardOutput());
    r.err  = QString::fromUtf8(proc.readAllStandardError());
    return r;
}

// ============================================================
// URL → repoName / provider
// ============================================================

QString RepoManager::repoNameFromUrl(const QString& url) {
    // 处理 https://github.com/a/b.git 和 git@github.com:a/b.git
    QString s = url.trimmed();
    // 去 .git 后缀
    if (s.endsWith(".git")) s.chop(4);

    // 提取最后一段路径
    int lastSlash = qMax(s.lastIndexOf('/'), s.lastIndexOf(':'));
    if (lastSlash >= 0) {
        return s.mid(lastSlash + 1);
    }
    return s;
}

QString RepoManager::providerFromUrl(const QString& url) {
    QString lower = url.toLower();
    if (lower.contains("github.com")) return "github";
    if (lower.contains("gitee.com"))  return "gitee";
    if (lower.contains("gitlab.com")) return "gitlab";
    if (lower.contains("gitlab."))    return "gitlab";
    return "generic";
}

// ============================================================
// addRepo
// ============================================================

Repo RepoManager::addRepo(const QString& url, const QString& doubaoSkillsRoot) {
    Repo r;
    r.url      = url;
    r.name     = repoNameFromUrl(url);
    r.provider = providerFromUrl(url);
    r.branch   = "main";

    // 路径：doubaoSkillsRoot / repoName
    QDir rootDir(doubaoSkillsRoot);
    if (!rootDir.exists()) {
        rootDir.mkpath(".");
    }
    r.localPath = rootDir.absoluteFilePath(r.name);

    // 如果目录已存在（可能有残留），先删
    QDir existing(r.localPath);
    if (existing.exists()) {
        existing.removeRecursively();
    }

    // git clone --depth=1
    GitResult gr = runGit(doubaoSkillsRoot,
                          QStringList{ "clone", "--depth=1", url, r.name },
                          120000);
    if (gr.code != 0) {
        qWarning() << "addRepo clone failed:" << gr.err;
        emit error(tr("clone 失败：%1").arg(gr.err));
        return Repo{};
    }

    // 检测分支（origin/HEAD 指向哪个分支）
    // 简单处理：先查远端默认分支
    GitResult gr2 = runGit(r.localPath,
                           QStringList{ "symbolic-ref", "refs/remotes/origin/HEAD" },
                           10000);
    if (gr2.code == 0) {
        // refs/remotes/origin/main → main
        QString ref = gr2.out.trimmed();
        if (ref.startsWith("refs/remotes/origin/")) {
            r.branch = ref.mid(20);
        }
    } else {
        // 尝试 remote show origin
        GitResult gr3 = runGit(r.localPath,
                               QStringList{ "remote", "show", "origin" },
                               10000);
        if (gr3.code == 0) {
            // 找 "HEAD branch: xxx"
            QRegularExpression re("HEAD branch:\\s*(\\S+)");
            auto m = re.match(gr3.out);
            if (m.hasMatch()) r.branch = m.captured(1);
        }
    }

    // 拿 current commit hash
    GitResult gr4 = runGit(r.localPath,
                           QStringList{ "rev-parse", "HEAD" },
                           10000);
    if (gr4.code == 0) {
        r.currentCommit = gr4.out.trimmed();
        r.latestCommit  = r.currentCommit;
    }

    r.lastPoll    = QDateTime::currentSecsSinceEpoch();
    r.hasUpdate   = false;
    r.enabled     = true;

    // 写入 DB
    int id = DaoSkill::insertRepo(r, m_db);
    if (id < 0) {
        emit error(tr("Repo 入库失败"));
        return Repo{};
    }
    r.id = id;

    emit repoAdded(r);
    return r;
}

// ============================================================
// checkUpdate
// ============================================================

void RepoManager::checkUpdate(Repo& repo) {
    if (repo.url.isEmpty()) {
        emit error(tr("Repo URL 为空"));
        return;
    }

    // git ls-remote origin HEAD —— 不用先 fetch，远端一次性返回 HEAD hash
    GitResult gr = runGit(repo.localPath,
                          QStringList{ "ls-remote", "origin", "HEAD" },
                          30000);
    if (gr.code != 0) {
        // 可能 repo 还没 clone 或 remote 不存在
        // 退而求其次：fetch + rev-parse
        GitResult gr2 = runGit(repo.localPath,
                               QStringList{ "fetch", "origin" },
                               30000);
        if (gr2.code != 0) {
            emit error(tr("fetch 失败：%1").arg(gr.err.isEmpty() ? gr2.err : gr.err));
            return;
        }
        GitResult gr3 = runGit(repo.localPath,
                               QStringList{ "rev-parse", QString("origin/%1").arg(repo.branch) },
                               10000);
        if (gr3.code != 0) {
            emit error(tr("rev-parse 远端分支失败：%1").arg(gr3.err));
            return;
        }
        repo.latestCommit = gr3.out.trimmed();
    } else {
        // ls-remote 输出：<hash>\tHEAD
        QString line = gr.out.trimmed();
        int tab = line.indexOf('\t');
        if (tab < 0) tab = line.indexOf(' ');
        if (tab < 0) {
            emit error(tr("ls-remote 输出解析失败"));
            return;
        }
        repo.latestCommit = line.left(tab);
    }

    repo.lastPoll  = QDateTime::currentSecsSinceEpoch();
    repo.hasUpdate = (repo.currentCommit != repo.latestCommit);

    if (!DaoSkill::updateRepo(repo, m_db)) {
        emit error(tr("Repo updateRepo 入库失败"));
    }

    emit updateChecked(repo, repo.hasUpdate);
}

// ============================================================
// pullUpdate
// ============================================================

void RepoManager::pullUpdate(Repo& repo) {
    // git pull --ff-only 或 merge --ff-only
    GitResult gr = runGit(repo.localPath,
                          QStringList{ "pull", "--ff-only", "origin", repo.branch },
                          60000);

    if (gr.code != 0) {
        // --ff-only 失败 → 退到 reset --hard（放弃本地改动）
        emit error(tr("pull --ff-only 失败，尝试 reset --hard：%1").arg(gr.err));

        // 先 fetch
        GitResult grf = runGit(repo.localPath,
                               QStringList{ "fetch", "origin" },
                               30000);
        if (grf.code != 0) {
            emit error(tr("fetch 失败：%1").arg(grf.err));
            return;
        }
        GitResult grh = runGit(repo.localPath,
                               QStringList{ "reset", "--hard",
                                            QString("origin/%1").arg(repo.branch) },
                               30000);
        if (grh.code != 0) {
            emit error(tr("reset --hard 失败：%1").arg(grh.err));
            return;
        }
    }

    // 重新拿 current commit
    GitResult gr2 = runGit(repo.localPath,
                           QStringList{ "rev-parse", "HEAD" },
                           10000);
    if (gr2.code != 0) {
        emit error(tr("rev-parse HEAD 失败：%1").arg(gr2.err));
        return;
    }
    repo.currentCommit = gr2.out.trimmed();
    repo.latestCommit  = repo.currentCommit;
    repo.hasUpdate     = false;
    repo.lastPoll      = QDateTime::currentSecsSinceEpoch();

    DaoSkill::updateRepo(repo, m_db);
    emit repoUpdated(repo);
}

// ============================================================
// removeRepo
// ============================================================

bool RepoManager::removeRepo(Repo& repo, QString* err) {
    auto setErr = [&](const QString& msg) {
        if (err) *err = msg;
        emit error(msg);
    };

    // 1. 删除对应的 skill 记录（repo_id = repo.id）
    if (repo.id > 0) {
        QSqlQuery q(m_db);
        q.prepare("DELETE FROM skills WHERE repo_id = :rid");
        q.bindValue(":rid", repo.id);
        if (!q.exec()) {
            setErr(tr("删除 skill 记录失败：%1").arg(q.lastError().text()));
            return false;
        }
    }

    // 2. 删除 DB 里的 repo 记录
    if (repo.id > 0) {
        if (!DaoSkill::deleteRepo(repo.id, m_db)) {
            setErr(tr("删除 repo 记录失败"));
            return false;
        }
    }

    // 3. 删除本地目录
    if (!repo.localPath.isEmpty()) {
        QDir dir(repo.localPath);
        if (dir.exists()) {
            if (!dir.removeRecursively()) {
                setErr(tr("删除目录失败：%1").arg(repo.localPath));
                return false;
            }
        }
    }

    return true;
}
