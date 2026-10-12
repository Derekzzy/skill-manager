#include "main_window.h"
#include "category_model.h"
#include "skill_table_model.h"
#include "dialogs.h"

#include "core/skill_scanner.h"
#include "core/classifier.h"
#include "core/repo_manager.h"
#include "core/repo.h"
#include "api/deepseek_client.h"
#include "db/dao_skill.h"

#include <QSplitter>
#include <QTreeView>
#include <QTableView>
#include <QTabWidget>
#include <QLineEdit>
#include <QTextBrowser>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QStatusBar>
#include <QFormLayout>
#include <QItemSelectionModel>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QDesktopServices>
#include <QMessageBox>
#include <QAbstractItemView>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTimer>
#include <QProcess>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QDateTime>
#include <QDebug>
#include <QScrollArea>
#include <QRegularExpression>
#include <QSize>

static const char* kDoubaoSkillsRoot = "F:/doubao-skills";

// 简单 markdown → HTML 转码（只处理标题、代码块、列表、换行）
static QString simpleMarkdownToHtml(const QString& md) {
    QString out = md.toHtmlEscaped();
    // 代码块
    out.replace(QRegularExpression("```([\\s\\S]*?)```"), "<pre style='background:#282c34;color:#abb2bf;padding:8px;border-radius:4px;white-space:pre-wrap;font-family:Consolas;'>\\1</pre>");
    // 标题
    out.replace(QRegularExpression("^### (.+)$", QRegularExpression::MultilineOption), "<h3 style='color:#333;margin:8px 0;'>\\1</h3>");
    out.replace(QRegularExpression("^## (.+)$",  QRegularExpression::MultilineOption), "<h2 style='color:#222;margin:10px 0;'>\\1</h2>");
    out.replace(QRegularExpression("^# (.+)$",   QRegularExpression::MultilineOption), "<h1 style='color:#111;margin:12px 0;'>\\1</h1>");
    // 无序列表
    out.replace(QRegularExpression("^- (.+)$", QRegularExpression::MultilineOption), "• \\1<br>");
    // 有序列表
    out.replace(QRegularExpression("^\\d+\\. (.+)$", QRegularExpression::MultilineOption), "\\1<br>");
    // 粗体
    out.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<b>\\1</b>");
    // 斜体
    out.replace(QRegularExpression("\\*(.+?)\\*"), "<i>\\1</i>");
    // 链接
    out.replace(QRegularExpression("\\[(.+?)\\]\\((.+?)\\)"), "<a href='\\2' style='color:#1976d2;text-decoration:underline;'>\\1</a>");
    // 换行
    out.replace("\n", "<br>");
    return "<div style='font-family:Segoe UI,Microsoft YaHei;line-height:1.6;'>" + out + "</div>";
}

MainWindow::MainWindow(SkillScanner* scanner,
                       Classifier* classifier,
                       RepoManager* repoMgr,
                       DeepSeekClient* ai,
                       QSqlDatabase db,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_scanner(scanner)
    , m_classifier(classifier)
    , m_repoMgr(repoMgr)
    , m_ai(ai)
    , m_db(db)
{
    setWindowTitle(tr("Skill Manager"));
    resize(1280, 800);

    m_catModel   = new CategoryModel(m_db, this);
    m_tableModel = new SkillTableModel(m_db, this);

    buildUi();
    connectSignals();

    // 初始加载：分类树 + 列表
    refreshCategoryTree();
    refreshTable();

    statusBar()->showMessage(tr("就绪"));
}

// ========================================================================
// UI 构建
// ========================================================================

void MainWindow::buildUi() {
    // ---- 顶部工具栏 ----
    auto* toolBar = addToolBar(tr("工具"));
    toolBar->setMovable(false);
    toolBar->setIconSize(QSize(16, 16));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("🔍 搜索技能..."));
    m_searchEdit->setMinimumWidth(260);
    toolBar->addWidget(m_searchEdit);

    toolBar->addSeparator();

    m_scanBtn    = new QPushButton(tr("扫描"), this);
    m_refreshBtn = new QPushButton(tr("刷新"), this);
    m_checkBtn   = new QPushButton(tr("检查更新"), this);
    m_addRepoBtn = new QPushButton(tr("添加仓库"), this);
    m_apiBtn     = new QPushButton(tr("API 设置"), this);

    toolBar->addWidget(m_scanBtn);
    toolBar->addWidget(m_refreshBtn);
    toolBar->addWidget(m_checkBtn);
    toolBar->addWidget(m_addRepoBtn);
    toolBar->addWidget(m_apiBtn);

    // ---- 三栏主布局 ----
    auto* central = new QWidget(this);
    setCentralWidget(central);

    m_hSplitter = new QSplitter(Qt::Horizontal, central);

    // ---- 左栏：分类树 ----
    auto* leftPanel = new QWidget(m_hSplitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 4, 0, 0);

    auto* catLabel = new QLabel(tr("分类"), leftPanel);
    catLabel->setStyleSheet("font-weight:bold;padding:4px 8px;");
    leftLayout->addWidget(catLabel);

    m_catTree = new QTreeView(leftPanel);
    m_catTree->setModel(m_catModel);
    m_catTree->setHeaderHidden(true);
    m_catTree->setAnimated(true);
    m_catTree->expandAll();
    m_catTree->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_catTree, 1);

    m_hSplitter->addWidget(leftPanel);

    // ---- 中栏：Skill 列表 ----
    auto* midPanel = new QWidget(m_hSplitter);
    auto* midLayout = new QVBoxLayout(midPanel);
    midLayout->setContentsMargins(0, 4, 0, 0);

    auto* tableLabel = new QLabel(tr("技能列表"), midPanel);
    tableLabel->setStyleSheet("font-weight:bold;padding:4px 8px;");
    midLayout->addWidget(tableLabel);

    m_skillTable = new QTableView(midPanel);
    m_skillTable->setModel(m_tableModel);
    m_skillTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_skillTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_skillTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_skillTable->setAlternatingRowColors(true);
    m_skillTable->verticalHeader()->setVisible(false);
    m_skillTable->horizontalHeader()->setStretchLastSection(true);
    m_skillTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_skillTable->horizontalHeader()->resizeSection(0, 220); // 名称
    m_skillTable->horizontalHeader()->resizeSection(1, 120); // 分类
    m_skillTable->horizontalHeader()->resizeSection(2, 100); // 来源
    m_skillTable->horizontalHeader()->resizeSection(3, 60);  // 启用
    m_skillTable->horizontalHeader()->resizeSection(4, 80);  // 大小
    m_skillTable->horizontalHeader()->resizeSection(5, 140); // mtime
    midLayout->addWidget(m_skillTable, 1);

    auto* countLabel = new QLabel(tr("合计: 0"), midPanel);
    countLabel->setStyleSheet("padding:2px 8px;color:#666;");
    midLayout->addWidget(countLabel);
    // 把 countLabel 保存下来：通过 dynamicProperty 或者在 onRefresh 时重新设置。
    // 这里用 setProperty 存下来方便访问。
    countLabel->setProperty("countLabel", true);

    m_hSplitter->addWidget(midPanel);

    // ---- 右栏：TabWidget 详情 ----
    m_detailTabs = new QTabWidget(m_hSplitter);

    // Tab 1: 概览
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);

        m_overviewName = new QLabel(tab);
        m_overviewName->setStyleSheet("font-size:18px;font-weight:bold;padding:4px 2px;");
        lay->addWidget(m_overviewName);

        m_overviewCategory = new QLabel(tab);
        m_overviewCategory->setStyleSheet("color:#333;padding:2px;");
        lay->addWidget(m_overviewCategory);

        m_overviewSource = new QLabel(tab);
        m_overviewSource->setStyleSheet("color:#666;font-size:12px;padding:2px;");
        lay->addWidget(m_overviewSource);

        m_overviewStatus = new QLabel(tab);
        m_overviewStatus->setStyleSheet("padding:2px;");
        lay->addWidget(m_overviewStatus);

        m_overviewDesc = new QTextBrowser(tab);
        m_overviewDesc->setOpenExternalLinks(true);
        lay->addWidget(m_overviewDesc, 1);

        m_detailTabs->addTab(tab, tr("概览"));
    }

    // Tab 2: SKILL.md
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        m_skillMdView = new QTextBrowser(tab);
        m_skillMdView->setOpenExternalLinks(true);
        lay->addWidget(m_skillMdView);
        m_detailTabs->addTab(tab, tr("SKILL.md"));
    }

    // Tab 3: 章节/术语
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        m_chapterList = new QListWidget(tab);
        lay->addWidget(m_chapterList, 1);
        m_chapterContent = new QTextBrowser(tab);
        m_chapterContent->setOpenExternalLinks(true);
        m_chapterContent->setMaximumHeight(260);
        lay->addWidget(m_chapterContent);
        m_detailTabs->addTab(tab, tr("章节/术语"));
    }

    // Tab 4: RAG 问答
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        m_ragView = new QTextBrowser(tab);
        m_ragView->setOpenExternalLinks(true);
        lay->addWidget(m_ragView, 1);

        auto* inputRow = new QHBoxLayout();
        m_ragInput   = new QLineEdit(tab);
        m_ragInput->setPlaceholderText(tr("问点什么吧，例如：如何让 skill 更稳定？"));
        m_ragSendBtn = new QPushButton(tr("发送"), tab);
        inputRow->addWidget(m_ragInput, 1);
        inputRow->addWidget(m_ragSendBtn);
        lay->addLayout(inputRow);

        m_detailTabs->addTab(tab, tr("RAG 问答"));
    }

    // Tab 5: 元数据/操作
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);

        m_metaDirectory = new QLabel(tab);
        m_metaDirectory->setWordWrap(true);
        m_metaSkillMd   = new QLabel(tab);
        m_metaSkillMd->setWordWrap(true);
        m_metaSourceSub = new QLabel(tab);
        m_metaSourceSub->setWordWrap(true);

        m_metaFiles = new QTextBrowser(tab);
        m_metaFiles->setOpenExternalLinks(false);
        m_metaFiles->setMaximumHeight(200);

        auto* form = new QFormLayout();
        form->addRow(tr("目录:"), m_metaDirectory);
        form->addRow(tr("SKILL.md:"), m_metaSkillMd);
        form->addRow(tr("source_subpath:"), m_metaSourceSub);
        lay->addLayout(form);
        lay->addWidget(m_metaFiles);

        auto* btnRow = new QHBoxLayout();
        m_translateBtn = new QPushButton(tr("翻译描述"), tab);
        m_enableBtn    = new QPushButton(tr("启用 / 禁用"), tab);
        m_deleteBtn    = new QPushButton(tr("删除"), tab);
        m_openDirBtn   = new QPushButton(tr("打开目录"), tab);
        btnRow->addWidget(m_translateBtn);
        btnRow->addWidget(m_enableBtn);
        btnRow->addWidget(m_deleteBtn);
        btnRow->addStretch();
        btnRow->addWidget(m_openDirBtn);
        lay->addLayout(btnRow);
        lay->addStretch();

        m_detailTabs->addTab(tab, tr("元数据/操作"));
    }

    m_hSplitter->addWidget(m_detailTabs);

    // 设置比例：左 20%，中间+右侧 80%
    m_hSplitter->setStretchFactor(0, 1);
    m_hSplitter->setStretchFactor(1, 2);
    m_hSplitter->setStretchFactor(2, 3);
    m_hSplitter->setSizes(QList<int>() << 220 << 460 << 600);
}

// ========================================================================
// 信号连接
// ========================================================================

void MainWindow::connectSignals() {
    connect(m_catTree, &QTreeView::clicked, this, &MainWindow::onCategoryClicked);
    connect(m_skillTable->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onSkillSelectionChanged);

    // 搜索 debounce (300ms)
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() {
        m_tableModel->setFilter(m_catModel->categoryName(m_catTree->currentIndex()), m_pendingSearch);
        refreshCategoryTree();
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_pendingSearch = t;
        m_searchDebounce->start(300);
    });

    connect(m_scanBtn,    &QPushButton::clicked, this, &MainWindow::onScanNew);
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefresh);
    connect(m_checkBtn,   &QPushButton::clicked, this, &MainWindow::onCheckUpdates);
    connect(m_addRepoBtn, &QPushButton::clicked, this, &MainWindow::onAddRepo);
    connect(m_apiBtn,     &QPushButton::clicked, this, &MainWindow::onApiSettings);

    connect(m_ragSendBtn, &QPushButton::clicked, this, &MainWindow::onRagSend);
    connect(m_ragInput,   &QLineEdit::returnPressed, this, &MainWindow::onRagSend);

    connect(m_chapterList, &QListWidget::currentRowChanged, this, &MainWindow::onChapterClicked);

    connect(m_translateBtn, &QPushButton::clicked, this, &MainWindow::onTranslateCurrent);
    connect(m_enableBtn,    &QPushButton::clicked, this, &MainWindow::onEnableDisable);
    connect(m_deleteBtn,    &QPushButton::clicked, this, &MainWindow::onDeleteSkill);
    connect(m_openDirBtn,   &QPushButton::clicked, this, &MainWindow::onOpenDirectory);

    // RepoManager 信号
    connect(m_repoMgr, &RepoManager::repoAdded,       this, &MainWindow::onRepoAdded);
    connect(m_repoMgr, &RepoManager::repoUpdated,      this, [this](const Repo& r) {
        DaoSkill::updateRepo(r, m_db);
        statusBar()->showMessage(tr("仓库已更新: %1").arg(r.name), 3000);
    });
    connect(m_repoMgr, &RepoManager::updateChecked,    this, &MainWindow::onRepoUpdateChecked);
    connect(m_repoMgr, &RepoManager::error,            this, &MainWindow::onRepoError);
}

// ========================================================================
// 数据刷新
// ========================================================================

void MainWindow::refreshCategoryTree() {
    m_catModel->reload();
    m_catTree->expandAll();
}

void MainWindow::refreshTable() {
    // 保留当前分类过滤
    m_tableModel->setFilter(m_catModel->categoryName(m_catTree->currentIndex()), m_searchEdit->text());
    int n = m_tableModel->rowCount();

    auto* label = findChild<QLabel*>("countLabel");
    if (!label) {
        // fallback: 遍历找带属性的
        for (auto* child : centralWidget()->findChildren<QLabel*>()) {
            if (child->property("countLabel").isValid()) { label = child; break; }
        }
    }
    if (label) label->setText(tr("合计: %1").arg(n));
}

// ========================================================================
// 槽：分类 / 搜索 / 选择
// ========================================================================

void MainWindow::onCategoryClicked(const QModelIndex& idx) {
    QString cat = m_catModel->categoryName(idx);
    m_tableModel->setFilter(cat, m_searchEdit->text());
}

void MainWindow::onRefresh() {
    refreshTable();
    refreshCategoryTree();
    statusBar()->showMessage(tr("已刷新"), 2000);
}

void MainWindow::onSearchChanged(const QString& /*text*/) {
    // 实际搜索走 connectSignals 里的 debounce lambda，这里保留空实现占位
}

void MainWindow::onSkillSelectionChanged(const QModelIndex& current, const QModelIndex& previous) {
    Q_UNUSED(previous);
    if (!current.isValid()) { clearDetail(); return; }
    Skill s = m_tableModel->skillAtCopy(current.row());
    // 重新从 DB 取一份（列表里的可能不是最新）
    Skill fresh = DaoSkill::skillById(s.id, m_db);
    if (fresh.id != 0) showSkillDetail(fresh);
    else showSkillDetail(s);
}

// ========================================================================
// 详情展示
// ========================================================================

Skill MainWindow::currentSkill() const {
    QModelIndex idx = m_skillTable->currentIndex();
    if (!idx.isValid()) return Skill{};
    return m_tableModel->skillAtCopy(idx.row());
}

void MainWindow::clearDetail() {
    m_overviewName->clear();
    m_overviewCategory->clear();
    m_overviewSource->clear();
    m_overviewDesc->clear();
    m_overviewStatus->clear();
    m_skillMdView->clear();
    m_chapterList->clear();
    m_chapterContent->clear();
    m_ragView->clear();
    m_ragInput->clear();
    m_metaDirectory->clear();
    m_metaSkillMd->clear();
    m_metaSourceSub->clear();
    m_metaFiles->clear();
}

void MainWindow::showSkillDetail(const Skill& s) {
    // Tab 1 概览
    QString title = s.name;
    if (s.isNew) title = QString("🆕 ") + title;
    m_overviewName->setText(title);

    QString catLine = s.category.isEmpty() ? tr("未分类") : s.category;
    if (!s.tags.isEmpty()) catLine += tr("  |  标签: ") + s.tags;
    m_overviewCategory->setText(catLine);

    QString sourceText = s.source.isEmpty() ? "local" : s.source;
    if (s.repoId > 0) {
        Repo repo = DaoSkill::repoById(s.repoId, m_db);
        if (repo.id != 0) sourceText += QString("  (repo: %1)").arg(repo.name);
    }
    m_overviewSource->setText(tr("来源: %1  |  目录: %2").arg(sourceText, s.directory));

    m_overviewStatus->setText(s.enabled
        ? QString("<span style='color:#2e7d32;'>● %1</span>").arg(tr("已启用"))
        : QString("<span style='color:#c62828;'>● %1</span>").arg(tr("已禁用")));

    QString descHtml;
    if (!s.descriptionZh.isEmpty()) {
        descHtml = simpleMarkdownToHtml(s.descriptionZh);
        if (!s.description.isEmpty()) {
            descHtml += QString("<hr><p style='color:#888;font-size:12px;'>%1</p>")
                            .arg(s.description.toHtmlEscaped());
        }
    } else if (!s.description.isEmpty()) {
        descHtml = simpleMarkdownToHtml(s.description);
        descHtml += QString("<p style='color:#999;font-size:12px;margin-top:8px;'>%1</p>")
                        .arg(tr("（点「翻译描述」可翻译成中文）"));
    } else {
        descHtml = QString("<p style='color:#aaa;'>%1</p>").arg(tr("（无描述）"));
    }
    m_overviewDesc->setHtml(descHtml);

    // Tab 2 SKILL.md
    QString body = s.body;
    if (body.isEmpty()) {
        QFile f(s.skillMdPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            body = QString::fromUtf8(f.readAll());
        }
    }
    m_skillMdView->setHtml(simpleMarkdownToHtml(body));

    // Tab 3: 收集 chapters 文件
    m_chapterList->clear();
    m_chapterContent->clear();
    QDir dir(s.directory);
    QStringList filters = { "chapters/*.md", "glossary.md", "patterns.md", "cheatsheet.md",
                           "examples/*.md", "faq/*.md", "notes/*.md", "*.md" };
    QStringList seen;
    for (const QString& pat : filters) {
        QStringList files = dir.entryList({pat}, QDir::Files | QDir::NoSymLinks);
        for (const QString& fn : files) {
            QString rel = fn; // 对于子目录 entryList 会返回 "chapters/foo.md"
            if (seen.contains(rel)) continue;
            if (rel.compare("SKILL.md", Qt::CaseInsensitive) == 0) continue;
            seen.append(rel);
            m_chapterList->addItem(rel);
        }
    }

    // Tab 5 元数据
    m_metaDirectory->setText(s.directory);
    m_metaSkillMd->setText(s.skillMdPath);
    m_metaSourceSub->setText(s.sourceSubpath.isEmpty() ? "-" : s.sourceSubpath);

    QStringList fileListHtml;
    for (const QString& p : seen) {
        fileListHtml.append(QString("• %1").arg(p));
    }
    m_metaFiles->setHtml(simpleMarkdownToHtml(fileListHtml.join("\n")));

    m_enableBtn->setText(s.enabled ? tr("禁用") : tr("启用"));
}

void MainWindow::onChapterClicked(int row) {
    auto* item = m_chapterList->item(row);
    if (!item) { m_chapterContent->clear(); return; }
    QString path = QDir(currentSkill().directory).filePath(item->text());
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_chapterContent->setPlainText(tr("无法打开文件: %1").arg(path));
        return;
    }
    QString content = QString::fromUtf8(f.readAll());
    m_chapterContent->setHtml(simpleMarkdownToHtml(content));
}

// ========================================================================
// 扫描 + 分类
// ========================================================================

void MainWindow::onAutoBootScan() {
    // 启动时，如果 DB 里 skills 数量为 0 则自动扫一次；否则只提示就绪。
    QList<Skill> existing = DaoSkill::allSkills(m_db);
    if (existing.isEmpty()) {
        statusBar()->showMessage(tr("首次启动，开始扫描..."));
        onScanNew();
    }
}

void MainWindow::onScanNew() {
    statusBar()->showMessage(tr("正在扫描 doubao-skills..."));
    m_scanBtn->setEnabled(false);

    // 在后台线程里执行扫描
    QFutureWatcher<QList<Skill>>* watcher = new QFutureWatcher<QList<Skill>>(this);
    QList<Repo> repos = DaoSkill::allRepos(m_db);

    connect(watcher, &QFutureWatcher<QList<Skill>>::finished, this, [this, watcher]() {
        QList<Skill> newSkills = watcher->result();
        watcher->deleteLater();
        m_scanBtn->setEnabled(true);

        // upsert 到 DB
        int newCount = 0;
        for (const Skill& sc : newSkills) {
            Skill ex = DaoSkill::skillByDir(sc.directory, m_db);
            if (ex.id == 0) {
                Skill toInsert = sc;
                toInsert.isNew = true;
                int id = DaoSkill::insertSkill(toInsert, m_db);
                Q_UNUSED(id);
                ++newCount;
            } else {
                // 已存在：如果 mtime 不同则更新 body/description 等
                bool changed = false;
                if (ex.mtime != sc.mtime) {
                    ex.body        = sc.body;
                    ex.description = sc.description;
                    ex.fileSize    = sc.fileSize;
                    ex.fileCount   = sc.fileCount;
                    ex.hasChapters = sc.hasChapters;
                    ex.hasPatterns = sc.hasPatterns;
                    ex.hasGlossary = sc.hasGlossary;
                    ex.hasCheatsheet = sc.hasCheatsheet;
                    ex.hasPatterns = sc.hasPatterns;
                    changed = true;
                }
                if (changed) DaoSkill::updateSkill(ex, m_db);
            }
        }

        // 自动分类
        statusBar()->showMessage(tr("扫描完成 (新增 %1)，正在分类...").arg(newCount));
        QtConcurrent::run([this]() {
            m_classifier->reclassifyAll();
        }).then([this]() {
            refreshTable();
            refreshCategoryTree();
            statusBar()->showMessage(tr("扫描 + 分类完成"), 3000);
        });
    });

    watcher->setFuture(QtConcurrent::run([this, repos]() {
        return m_scanner->scanAll(kDoubaoSkillsRoot, repos);
    }));
}

// ========================================================================
// RepoManager 交互
// ========================================================================

void MainWindow::onCheckUpdates() {
    statusBar()->showMessage(tr("正在检查仓库更新..."));
    QList<Repo> repos = DaoSkill::allRepos(m_db);
    if (repos.isEmpty()) {
        statusBar()->showMessage(tr("没有仓库可检查"), 2000);
        return;
    }

    for (Repo r : repos) {
        if (!r.enabled) continue;
        QtConcurrent::run([this, r]() mutable {
            m_repoMgr->checkUpdate(r);
        });
    }
}

void MainWindow::onRepoUpdateChecked(const Repo& r, bool hasUpdate) {
    // 把 hasUpdate 写回 DB
    Repo copy = r;
    copy.hasUpdate = hasUpdate;
    copy.lastPoll = QDateTime::currentSecsSinceEpoch();
    DaoSkill::updateRepo(copy, m_db);

    if (hasUpdate) {
        statusBar()->showMessage(tr("仓库 %1 有更新").arg(r.name), 5000);
    } else {
        statusBar()->showMessage(tr("仓库 %1 已是最新").arg(r.name), 2000);
    }
}

void MainWindow::onRepoError(const QString& msg) {
    statusBar()->showMessage(tr("错误: %1").arg(msg), 5000);
    QMessageBox::warning(this, tr("错误"), msg);
}

void MainWindow::onRepoAdded(const Repo& r) {
    if (r.id != 0) {
        statusBar()->showMessage(tr("仓库已添加: %1").arg(r.name), 3000);
    }
}

void MainWindow::onAddRepo() {
    RepoAddDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    QString url = dlg.url();
    if (url.isEmpty()) return;

    statusBar()->showMessage(tr("正在克隆仓库: %1").arg(url));

    QtConcurrent::run([this, url]() {
        return m_repoMgr->addRepo(url, kDoubaoSkillsRoot);
    }).then([this](Repo r) {
        if (r.id != 0) {
            statusBar()->showMessage(tr("仓库 %1 克隆成功，重新扫描...").arg(r.name), 3000);
            onScanNew();
        } else {
            statusBar()->showMessage(tr("仓库克隆失败"), 3000);
            QMessageBox::critical(this, tr("失败"), tr("仓库克隆失败，详情看日志"));
        }
    });
}

// ========================================================================
// API 设置 / 翻译 / 启用禁用 / 删除
// ========================================================================

void MainWindow::onApiSettings() {
    ApiSettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_ai) {
            m_ai->setEndpoint(dlg.endpoint());
            m_ai->setApiKey(dlg.apiKey());
            m_ai->setModel(dlg.model());
        }
        statusBar()->showMessage(tr("API 设置已更新"), 2000);
    }
}

void MainWindow::onTranslateCurrent() {
    Skill s = currentSkill();
    if (s.id == 0) { QMessageBox::information(this, tr("提示"), tr("请先选中一个 Skill")); return; }
    if (s.description.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("该 skill 没有可翻译的 description")); return;
    }
    if (!m_ai || !m_ai->isConfigured()) {
        QMessageBox::information(this, tr("提示"), tr("请先在「API 设置」中配置 DeepSeek API Key"));
        return;
    }

    statusBar()->showMessage(tr("翻译中..."));
    m_translateBtn->setEnabled(false);

    m_ai->translate(s.description, [this, s](const QString& zh, const QString& err) {
        m_translateBtn->setEnabled(true);
        if (!err.isEmpty()) {
            QMessageBox::warning(this, tr("翻译失败"), err);
            statusBar()->showMessage(tr("翻译失败: %1").arg(err), 3000);
            return;
        }
        Skill updated = s;
        updated.descriptionZh = zh;
        DaoSkill::updateSkill(updated, m_db);
        showSkillDetail(updated);
        statusBar()->showMessage(tr("翻译完成"), 2000);
    });
}

void MainWindow::onEnableDisable() {
    Skill s = currentSkill();
    if (s.id == 0) return;
    bool newState = !s.enabled;
    if (!DaoSkill::setEnabled(s.id, newState, m_db)) {
        QMessageBox::warning(this, tr("错误"), tr("操作失败"));
        return;
    }
    refreshTable();
    refreshCategoryTree();
    showSkillDetail(DaoSkill::skillById(s.id, m_db));
}

void MainWindow::onDeleteSkill() {
    Skill s = currentSkill();
    if (s.id == 0) return;
    int ret = QMessageBox::question(this, tr("确认"),
        tr("确定删除 skill 「%1」？\n（仅从数据库移除记录，不删磁盘文件）").arg(s.name));
    if (ret != QMessageBox::Yes) return;

    if (DaoSkill::deleteSkill(s.id, m_db)) {
        refreshTable();
        refreshCategoryTree();
        clearDetail();
        statusBar()->showMessage(tr("已删除: %1").arg(s.name), 2000);
    } else {
        QMessageBox::warning(this, tr("错误"), tr("删除失败"));
    }
}

void MainWindow::onOpenDirectory() {
    Skill s = currentSkill();
    if (s.id == 0) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(s.directory));
}

// ========================================================================
// RAG
// ========================================================================

void MainWindow::onRagSend() {
    Skill s = currentSkill();
    if (s.id == 0) { QMessageBox::information(this, tr("提示"), tr("请先选中一个 Skill")); return; }
    QString question = m_ragInput->text().trimmed();
    if (question.isEmpty()) return;
    if (!m_ai || !m_ai->isConfigured()) {
        QMessageBox::information(this, tr("提示"), tr("请先在「API 设置」中配置 DeepSeek API Key"));
        return;
    }

    m_ragInput->clear();
    m_ragSendBtn->setEnabled(false);
    appendRagAnswer(question, tr("（思考中...）"));
    statusBar()->showMessage(tr("RAG 请求中..."));

    m_ai->rag(s.body, question, [this, question](const QString& answer, const QString& err) {
        m_ragSendBtn->setEnabled(true);
        if (!err.isEmpty()) {
            appendRagAnswer(question, tr("❌ 错误: %1").arg(err));
            statusBar()->showMessage(tr("RAG 失败: %1").arg(err), 5000);
            return;
        }
        // 把最后那个占位的 "思考中" 替换掉
        QString html = m_ragView->toHtml();
        html.remove(QRegularExpression("思考中[\\s\\S]*?</div>"));
        m_ragView->setHtml(html);

        QString bubble = QString(
            "<div style='margin:4px;padding:8px;background:#e3f2fd;border-radius:6px;'>"
            "<b>Q:</b> %1</div>"
            "<div style='margin:4px;padding:8px;background:#f5f5f5;border-radius:6px;white-space:pre-wrap;'>"
            "<b>A:</b> %2</div>")
            .arg(question.toHtmlEscaped(), answer.toHtmlEscaped());
        m_ragView->append(bubble);
        statusBar()->showMessage(tr("RAG 回答已返回"), 2000);
    });
}

void MainWindow::appendRagAnswer(const QString& question, const QString& answer) {
    QString bubble = QString(
        "<div style='margin:4px;padding:8px;background:#e3f2fd;border-radius:6px;'>"
        "<b>Q:</b> %1</div>"
        "<div style='margin:4px;padding:8px;background:#f5f5f5;border-radius:6px;white-space:pre-wrap;'>"
        "<b>A:</b> %2</div>")
        .arg(question.toHtmlEscaped(), answer.toHtmlEscaped());
    m_ragView->append(bubble);
}
