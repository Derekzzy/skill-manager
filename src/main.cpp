#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QTimer>
#include <QtConcurrent>
#include <QDebug>

#include "db/database.h"
#include "db/seed.h"
#include "db/dao_skill.h"
#include "core/skill_scanner.h"
#include "core/classifier.h"
#include "core/repo_manager.h"
#include "core/repo.h"
#include "api/deepseek_client.h"
#include "api/agent_bridge.h"

#ifdef SKILL_MANAGER_GUI
#include "gui/main_window.h"
#endif

#include <QDir>
#include <QFile>

static const char* kDoubaoSkillsRoot = "F:/doubao-skills";

// 在 GUI 首次启动时，自动将 doubaoSkillsRoot 下已有的 git 仓库添加到 DB。
static void autoSeedExistingRepos(RepoManager* mgr, QSqlDatabase& db) {
    QDir root(kDoubaoSkillsRoot);
    if (!root.exists()) return;

    for (const QString& entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString full = root.filePath(entry);
        if (QDir(full + "/.git").exists()) {
            Repo existing = DaoSkill::repoByName(entry, db);
            if (existing.id == 0) {
                Repo r = mgr->addRepo("https://github.com/unknown/" + entry + ".git", kDoubaoSkillsRoot);
                Q_UNUSED(r); // addRepo 内部如果 clone 失败会返回 id=0 的空 Repo，DB 不会被污染
            }
        }
    }
}

int main(int argc, char* argv[]) {
    // 先仅用 QCoreApplication 能跑的方式解析 argv，判断是否走 CLI
    QStringList args = QCoreApplication::arguments();
    QStringList sub = args.mid(1); // 去掉程序名

    QStringList cliCmds = { "list", "show", "content", "query", "translate",
                            "repos", "repo-add", "repo-check", "repo-pull", "serve" };
    bool isCli = !sub.isEmpty() && cliCmds.contains(sub[0]);

    if (isCli) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("SkillManager");
        app.setOrganizationName("SkillManager");

        if (!Database::instance().init()) {
            qCritical() << "Database init failed";
            return 1;
        }
        QSqlDatabase db = Database::db();
    Seed::run(db);

    DeepSeekClient ai;
    AgentBridge bridge(db, &ai);

        return bridge.runCli(sub);
    }

#ifdef SKILL_MANAGER_GUI
    QApplication app(argc, argv);
    app.setApplicationName("SkillManager");
    app.setOrganizationName("SkillManager");

    // 加载 QSS 主题（从 exe 同目录的 resources/style.qss，兼容开发期与部署期）
    QString qssPath = QCoreApplication::applicationDirPath() + "/resources/style.qss";
    if (!QFile::exists(qssPath)) {
        qssPath = QCoreApplication::applicationDirPath() + "/../resources/style.qss";
    }
    QFile qssFile(qssPath);
    if (qssFile.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(qssFile.readAll());
    }

    if (!Database::instance().init()) {
        qCritical() << "Database init failed";
        return 1;
    }
    QSqlDatabase db = Database::db();
    Seed::run(db);

    auto* scanner    = new SkillScanner(&app);
    auto* classifier = new Classifier(db, &app);
    auto* repoMgr    = new RepoManager(db, &app);
    auto* ai         = new DeepSeekClient(&app);

    autoSeedExistingRepos(repoMgr, db);

    auto* mainWin = new MainWindow(scanner, classifier, repoMgr, ai, db);
    mainWin->show();

    // 异步启动时自动扫描一次 + 自动分类
    QTimer::singleShot(0, mainWin, &MainWindow::onAutoBootScan);

    // RepoManager 30 分钟轮询一次 checkUpdate
    auto* pollTimer = new QTimer(mainWin);
    QObject::connect(pollTimer, &QTimer::timeout, repoMgr, [repoMgr, db]() {
        QList<Repo> repos = DaoSkill::allRepos(db);
        for (Repo r : repos) {
            if (r.enabled) {
                QtConcurrent::run([repoMgr, r]() mutable {
                    repoMgr->checkUpdate(r);
                });
            }
        }
    });
    pollTimer->start(30 * 60 * 1000);

    return app.exec();
#else
    qCritical() << "This build was compiled without GUI support (SKILL_MANAGER_GUI not defined). "
                   "Use CLI subcommands instead.";
    return 1;
#endif
}
