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
#include <QStatusBar>
#include <QFormLayout>
#include <QItemSelectionModel>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QSqlQuery>
#include <QSqlError>
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
#include <QCoreApplication>
#include <QSettings>
#include <QMap>
#include <QApplication>

static const char* kDoubaoSkillsRoot = "F:/doubao-skills";

// 简单 markdown → HTML 转码（目录卡片柜深色版）
static QString simpleMarkdownToHtml(const QString& md) {
    QString out = md.toHtmlEscaped();
    // 代码块 —— 纸面暗纹
    out.replace(QRegularExpression("```([\\s\\S]*?)```"),
                "<pre style='background:#121417;color:#4DA89C;padding:10px 14px;border-left:2px solid #4DA89C;"
                "white-space:pre-wrap;font-family:\"Cascadia Code\",Consolas,monospace;font-size:12px;line-height:1.6;'>\\1</pre>");
    // 标题 —— 打字机等宽
    out.replace(QRegularExpression("^### (.+)$", QRegularExpression::MultilineOption),
                "<h3 style='color:#E6E1D6;font-family:\"Cascadia Code\",monospace;font-size:14px;"
                "letter-spacing:1px;margin:14px 0 6px 0;font-weight:600;'>\\1</h3>");
    out.replace(QRegularExpression("^## (.+)$", QRegularExpression::MultilineOption),
                "<h2 style='color:#4DA89C;font-family:\"Cascadia Code\",monospace;font-size:15px;"
                "letter-spacing:2px;margin:18px 0 8px 0;font-weight:700;'>\\1</h2>");
    out.replace(QRegularExpression("^# (.+)$", QRegularExpression::MultilineOption),
                "<h1 style='color:#E6E1D6;font-family:\"Cascadia Code\",monospace;font-size:18px;"
                "letter-spacing:3px;margin:22px 0 10px 0;font-weight:700;'>\\1</h1>");
    // 无序列表 —— 铜绿圆点
    out.replace(QRegularExpression("^- (.+)$", QRegularExpression::MultilineOption),
                "<div style='padding:2px 0 2px 12px;position:relative;'>"
                "<span style='color:#4DA89C;position:absolute;left:0;'>·</span>\\1</div>");
    // 有序列表
    out.replace(QRegularExpression("^\\d+\\. (.+)$", QRegularExpression::MultilineOption),
                "<div style='padding:2px 0 2px 0;'>\\1</div>");
    // 粗体
    out.replace(QRegularExpression("\\*\\*(.+?)\\*\\*"), "<b style='color:#4DA89C;font-weight:600;'>\\1</b>");
    // 斜体
    out.replace(QRegularExpression("\\*(.+?)\\*"), "<i style='color:#8B929A;font-style:normal;'>\\1</i>");
    // 链接
    out.replace(QRegularExpression("\\[(.+?)\\]\\((.+?)\\)"),
                "<a href='\\2' style='color:#4DA89C;text-decoration:none;border-bottom:1px dashed #2A2E33;'>\\1</a>");
    // 行内代码
    out.replace(QRegularExpression("`([^`]+)`"),
                "<code style='background:#121417;color:#4DA89C;padding:1px 6px;border-radius:2px;"
                "font-family:\"Cascadia Code\",Consolas,monospace;font-size:12px;'>\\1</code>");
    // 换行
    out.replace("\n", "<br>");
    return "<div style='font-family:\"Cascadia Code\",\"JetBrains Mono\",\"Consolas\","
           "\"Microsoft YaHei UI\",monospace;line-height:1.7;color:#E6E1D6;font-size:13px;'>"
           + out + "</div>";
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
    setWindowTitle(tr("Skill Manager — 私人藏书目录"));
    resize(1280, 820);

    // ---- 主容器：TitleBar + 三栏 Splitter + 底部按钮栏 ----
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* mainV = new QVBoxLayout(central);
    mainV->setContentsMargins(0, 0, 0, 0);
    mainV->setSpacing(0);

    // ============================================================
    // Title Bar — 高存在感应用标题 + 搜索大框 + 右侧状态
    // ============================================================
    auto* titleBar = new QWidget(central);
    titleBar->setObjectName("TitleBar");
    titleBar->setFixedHeight(64);
    auto* tLayout = new QHBoxLayout(titleBar);
    tLayout->setContentsMargins(22, 12, 22, 10);
    tLayout->setSpacing(14);

    // App icon dot + 应用名
    auto* dot = new QLabel(tr("●"), titleBar);
    dot->setObjectName("AppDot");
    tLayout->addWidget(dot);

    auto* nameLbl = new QLabel(tr("Skill Manager"), titleBar);
    nameLbl->setObjectName("AppName");
    tLayout->addWidget(nameLbl);

    auto* subLbl = new QLabel(tr("5610 项 · 私人知识库"), titleBar);
    subLbl->setObjectName("AppSub");
    tLayout->addWidget(subLbl);

    tLayout->addSpacing(20);

    // 搜索大框
    m_searchEdit = new QLineEdit(titleBar);
    m_searchEdit->setObjectName("TitleSearch");
    m_searchEdit->setPlaceholderText(tr("  搜索 skill 名、书名、章节、关键字..."));
    m_searchEdit->setFixedWidth(380);
    tLayout->addWidget(m_searchEdit);

    tLayout->addStretch();

    // 右侧状态
    auto* stLbl = new QLabel(tr("在线"), titleBar);
    stLbl->setObjectName("StatusDot");
    tLayout->addWidget(stLbl);

    mainV->addWidget(titleBar);

    auto* goldRule = new QFrame(central);
    goldRule->setObjectName("GoldRule");
    goldRule->setFrameShape(QFrame::HLine);
    goldRule->setFixedHeight(1);
    mainV->addWidget(goldRule);

    // ---- 三栏 Splitter ----
    m_hSplitter = new QSplitter(Qt::Horizontal, central);
    mainV->addWidget(m_hSplitter, 1);

    // ============================================================
    // 左栏：Sidebar（分类树）
    // ============================================================
    auto* leftPanel = new QWidget(m_hSplitter);
    leftPanel->setObjectName("Sidebar");
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* catLabel = new QLabel(tr("分类"), leftPanel);
    catLabel->setObjectName("SidebarTitle");
    leftLayout->addWidget(catLabel);

    m_catTotalLabel = new QLabel(tr(""), leftPanel);
    m_catTotalLabel->setObjectName("SidebarCount");
    leftLayout->addWidget(m_catTotalLabel);

    m_catTree = new QTreeView(leftPanel);
    m_catTree->setObjectName("CategoryTree");
    m_catTree->setModel(m_catModel);
    m_catTree->setHeaderHidden(true);
    m_catTree->setAnimated(false);
    m_catTree->expandAll();
    m_catTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_catTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_catTree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    leftLayout->addWidget(m_catTree, 1);

    m_hSplitter->addWidget(leftPanel);

    // ============================================================
    // 中栏：ListPanel（Skill 列表）
    // ============================================================
    auto* midPanel = new QWidget(m_hSplitter);
    midPanel->setObjectName("ListPanel");
    auto* midLayout = new QVBoxLayout(midPanel);
    midLayout->setContentsMargins(0, 0, 0, 0);
    midLayout->setSpacing(0);

    auto* listHeader = new QLabel(tr("书目"), midPanel);
    listHeader->setObjectName("ListHeader");
    midLayout->addWidget(listHeader);

    m_skillTable = new QTableView(midPanel);
    m_skillTable->setObjectName("SkillTable");
    m_skillTable->setModel(m_tableModel);
    m_skillTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_skillTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_skillTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_skillTable->setAlternatingRowColors(false);
    m_skillTable->verticalHeader()->setVisible(false);
    m_skillTable->horizontalHeader()->setStretchLastSection(true);
    m_skillTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_skillTable->setShowGrid(false);
    m_skillTable->setSortingEnabled(true);
    m_skillTable->verticalHeader()->setDefaultSectionSize(36);
    m_skillTable->horizontalHeader()->resizeSection(0, 220);
    m_skillTable->horizontalHeader()->resizeSection(1, 120);
    m_skillTable->horizontalHeader()->resizeSection(2, 100);
    m_skillTable->horizontalHeader()->resizeSection(3, 60);
    m_skillTable->horizontalHeader()->resizeSection(4, 80);
    m_skillTable->horizontalHeader()->resizeSection(5, 120);
    midLayout->addWidget(m_skillTable, 1);

    m_countLabel = new QLabel(tr(""), midPanel);
    m_countLabel->setObjectName("ListFooter");
    midLayout->addWidget(m_countLabel);

    m_hSplitter->addWidget(midPanel);

    // ============================================================
    // 右栏：DetailCard（纸面卡片 + Tab）
    // ============================================================
    auto* detailWrap = new QWidget(m_hSplitter);
    auto* detailOuter = new QVBoxLayout(detailWrap);
    detailOuter->setContentsMargins(10, 10, 10, 10);

    auto* detailCard = new QFrame(detailWrap);
    detailCard->setObjectName("DetailCard");
    auto* cardLayout = new QVBoxLayout(detailCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    m_detailTabs = new QTabWidget(detailCard);
    m_detailTabs->setObjectName("DetailTabs");

    // Tab 1: 概览
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        lay->setContentsMargins(22, 18, 22, 14);
        lay->setSpacing(0);

        m_overviewName = new QLabel(tab);
        m_overviewName->setObjectName("OverviewTitle");
        lay->addWidget(m_overviewName);

        m_overviewMeta = new QLabel(tab);
        m_overviewMeta->setObjectName("OverviewMeta");
        lay->addWidget(m_overviewMeta);

        m_overviewSource = new QLabel(tab);
        m_overviewSource->setObjectName("OverviewSource");
        lay->addWidget(m_overviewSource);

        lay->addSpacing(10);

        auto* descLabel = new QLabel(tr("DESCRIPTION"), tab);
        descLabel->setObjectName("SectionLabel");
        lay->addWidget(descLabel);

        m_overviewDesc = new QTextBrowser(tab);
        m_overviewDesc->setObjectName("OverviewDesc");
        m_overviewDesc->setOpenExternalLinks(true);
        lay->addWidget(m_overviewDesc, 1);

        m_detailTabs->addTab(tab, tr("概览"));
    }

    // Tab 2: SKILL.md
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        lay->setContentsMargins(22, 18, 22, 14);
        m_skillMdView = new QTextBrowser(tab);
        m_skillMdView->setObjectName("SkillMdView");
        m_skillMdView->setOpenExternalLinks(true);
        lay->addWidget(m_skillMdView);
        m_detailTabs->addTab(tab, tr("SKILL.md"));
    }

    // Tab 3: 章节/术语
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        lay->setContentsMargins(22, 18, 22, 14);
        m_chapterList = new QListWidget(tab);
        m_chapterList->setObjectName("ChapterList");
        lay->addWidget(m_chapterList, 1);
        m_chapterContent = new QTextBrowser(tab);
        m_chapterContent->setOpenExternalLinks(true);
        m_chapterContent->setObjectName("ChapterView");
        m_chapterContent->setMaximumHeight(260);
        lay->addWidget(m_chapterContent);
        m_detailTabs->addTab(tab, tr("章节 / 术语"));
    }

    // Tab 4: RAG 问答
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        lay->setContentsMargins(22, 18, 22, 14);
        lay->setSpacing(10);
        m_ragView = new QTextBrowser(tab);
        m_ragView->setObjectName("RagView");
        m_ragView->setOpenExternalLinks(true);
        lay->addWidget(m_ragView, 1);

        auto* inputRow = new QHBoxLayout();
        inputRow->setSpacing(10);
        m_ragInput = new QTextEdit(tab);
        m_ragInput->setObjectName("RagInput");
        m_ragInput->setPlaceholderText(tr("问点什么 — 回车键发送"));
        m_ragInput->setAcceptRichText(false);
        inputRow->addWidget(m_ragInput, 1);
        m_ragSendBtn = new QPushButton(tr("发送"), tab);
        m_ragSendBtn->setFixedWidth(80);
        inputRow->addWidget(m_ragSendBtn);
        lay->addLayout(inputRow);

        m_detailTabs->addTab(tab, tr("RAG 问答"));
    }

    // Tab 5: 元数据 / 操作
    {
        auto* tab = new QWidget();
        auto* lay = new QVBoxLayout(tab);
        lay->setContentsMargins(22, 18, 22, 14);
        lay->setSpacing(10);

        auto* btnRow = new QHBoxLayout();
        btnRow->setSpacing(6);

        m_translateBtn = new QPushButton(tr("翻译描述"), tab);
        m_enableBtn    = new QPushButton(tr("启用 / 禁用"), tab);
        m_openDirBtn   = new QPushButton(tr("打开目录"), tab);
        m_translateBtn->setObjectName("SecondaryBtn");
        m_enableBtn->setObjectName("SecondaryBtn");
        m_openDirBtn->setObjectName("SecondaryBtn");
        m_deleteBtn = new QPushButton(tr("删除"), tab);
        m_deleteBtn->setObjectName("DangerBtn");

        btnRow->addWidget(m_translateBtn);
        btnRow->addWidget(m_enableBtn);
        btnRow->addWidget(m_openDirBtn);
        btnRow->addStretch();
        btnRow->addWidget(m_deleteBtn);
        lay->addLayout(btnRow);

        auto* metaTitle = new QLabel(tr("FILES & METADATA"), tab);
        metaTitle->setObjectName("SectionLabel");
        lay->addWidget(metaTitle);

        m_metaDirectory = new QLabel(tab);
        m_metaDirectory->setWordWrap(true);
        m_metaSkillMd   = new QLabel(tab);
        m_metaSkillMd->setWordWrap(true);
        m_metaSourceSub = new QLabel(tab);
        m_metaSourceSub->setWordWrap(true);

        m_metaFiles = new QTextBrowser(tab);
        m_metaFiles->setOpenExternalLinks(false);
        m_metaFiles->setObjectName("OverviewDesc");
        m_metaFiles->setMaximumHeight(200);

        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(6);
        form->addRow(tr("目录:"), m_metaDirectory);
        form->addRow(tr("SKILL.md:"), m_metaSkillMd);
        form->addRow(tr("来源:"), m_metaSourceSub);
        lay->addLayout(form);
        lay->addWidget(m_metaFiles);
        lay->addStretch();

        m_detailTabs->addTab(tab, tr("元数据 / 操作"));
    }

    cardLayout->addWidget(m_detailTabs);
    detailOuter->addWidget(detailCard);

    m_hSplitter->addWidget(detailWrap);

    // Splitter 比例
    m_hSplitter->setStretchFactor(0, 0);
    m_hSplitter->setStretchFactor(1, 1);
    m_hSplitter->setStretchFactor(2, 2);
    m_hSplitter->setSizes(QList<int>() << 200 << 440 << 640);

    // ============================================================
    // 底部按钮栏
    // ============================================================
    auto* cmdBar = new QWidget(central);
    cmdBar->setObjectName("CommandBar");
    auto* cmdLayout = new QHBoxLayout(cmdBar);
    cmdLayout->setContentsMargins(24, 10, 24, 10);
    cmdLayout->setSpacing(6);
    mainV->addWidget(cmdBar);

    cmdLayout->addStretch();

    m_scanBtn    = new QPushButton(tr("扫描"), cmdBar);
    m_refreshBtn = new QPushButton(tr("刷新"), cmdBar);
    m_translateBatchBtn = new QPushButton(tr("批量翻译"), cmdBar);
    m_checkBtn   = new QPushButton(tr("检查更新"), cmdBar);
    m_addRepoBtn = new QPushButton(tr("添加仓库"), cmdBar);
    m_apiBtn     = new QPushButton(tr("API 设置"), cmdBar);

    m_scanBtn->setObjectName("SecondaryBtn");
    m_refreshBtn->setObjectName("SecondaryBtn");
    m_translateBatchBtn->setObjectName("SecondaryBtn");
    m_checkBtn->setObjectName("SecondaryBtn");
    m_addRepoBtn->setObjectName("SecondaryBtn");
    m_apiBtn->setObjectName("SecondaryBtn");

    cmdLayout->addWidget(m_scanBtn);
    cmdLayout->addWidget(m_refreshBtn);
    cmdLayout->addWidget(m_translateBatchBtn);
    cmdLayout->addWidget(m_checkBtn);
    cmdLayout->addWidget(m_addRepoBtn);
    cmdLayout->addWidget(m_apiBtn);
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
    connect(m_translateBatchBtn, &QPushButton::clicked, this, &MainWindow::onBatchTranslate);

    connect(m_ragSendBtn, &QPushButton::clicked, this, &MainWindow::onRagSend);

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
    // 侧栏计数
    QSqlQuery q(m_db);
    int total = 0;
    if (q.exec("SELECT COUNT(*) FROM skills")) {
        if (q.next()) total = q.value(0).toInt();
    }
    m_catTotalLabel->setText(tr("TOTAL %1").arg(total));
}

void MainWindow::refreshTable() {
    m_tableModel->setFilter(m_catModel->categoryName(m_catTree->currentIndex()), m_searchEdit->text());
    int n = m_tableModel->rowCount();
    m_countLabel->setText(tr("共 %1 条").arg(n));
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
    m_overviewMeta->clear();
    m_overviewSource->clear();
    m_overviewDesc->clear();
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
    m_overviewName->setText(s.name);

    QString catTag = s.category.isEmpty() ? tr("未分类") : s.category;
    QString srcTag = s.source.isEmpty() ? "local" : s.source;
    if (s.repoId > 0) {
        Repo repo = DaoSkill::repoById(s.repoId, m_db);
        if (repo.id != 0) srcTag += QString(" · %1").arg(repo.name);
    }
    if (!s.tags.isEmpty()) catTag += QString(" · %1").arg(s.tags.split(',')[0]);
    m_overviewMeta->setText(QString("◆ %1   %2").arg(catTag, srcTag));

    QString srcText = s.source.isEmpty() ? "local" : s.source;
    if (s.repoId > 0) {
        Repo repo = DaoSkill::repoById(s.repoId, m_db);
        if (repo.id != 0) srcText += QString("  (repo: %1)").arg(repo.name);
    }
    m_overviewSource->setText(tr("DIR  %1").arg(s.directory));

    QString descHtml;
    if (!s.descriptionZh.isEmpty()) {
        descHtml = simpleMarkdownToHtml(s.descriptionZh);
    } else if (!s.description.isEmpty()) {
        descHtml = simpleMarkdownToHtml(s.description);
    } else {
        descHtml = QString("<p style='color:#6B7278;font-style:normal;'>— no description —</p>");
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
        // 立即切换主题（热重载 QSS）
        applyTheme(dlg.themeId());
        statusBar()->showMessage(tr("设置已更新，主题已切换"), 2000);
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

// ============================================================
// 批量翻译
// ============================================================

// 检测字符串里是否有 CJK 统一表意文字（汉字）
static bool hasChineseText(const QString& s) {
    for (auto it = s.constBegin(); it != s.constEnd(); ++it) {
        QChar c = *it;
        uint cp = c.unicode();
        // CJK 统一表意文字: 0x4E00-0x9FFF, 扩展 A 0x3400-0x4DBF, 兼容表意文字 0xF900-0xFAFF
        if ((cp >= 0x4E00 && cp <= 0x9FFF) ||
            (cp >= 0x3400 && cp <= 0x4DBF) ||
            (cp >= 0xF900 && cp <= 0xFAFF)) {
            return true;
        }
    }
    return false;
}

void MainWindow::onBatchTranslate() {
    if (!m_ai || !m_ai->isConfigured()) {
        QMessageBox::information(this, tr("提示"),
            tr("请先在底部「API」按钮里配置 DeepSeek API Key"));
        return;
    }
    if (!m_todoTranslateIds.isEmpty() || m_activeTranslate > 0) {
        QMessageBox::information(this, tr("提示"), tr("翻译正在进行中，请稍候"));
        return;
    }

    // 收集：description 非空 + descriptionZh 空 + 原文无中文 + 有实质内容（≥6字）
    QList<Skill> all = DaoSkill::allSkills(m_db);
    QList<int> ids;
    for (const Skill& s : all) {
        if (s.description.isEmpty()) continue;
        if (!s.descriptionZh.isEmpty()) continue;   // 已有译文跳过
        if (hasChineseText(s.description)) continue;  // 本身是中文跳过
        if (s.description.trimmed().length() < 6) continue;  // 垃圾数据 (>- 等)
        ids.append(s.id);
    }

    if (ids.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("没有需要翻译的 skill"));
        return;
    }

    m_todoTranslateIds = ids;
    m_translateBatchBtn->setEnabled(false);
    statusBar()->showMessage(tr("开始批量翻译，共 %1 条").arg(ids.size()));
    trySendOneTranslate();
}

void MainWindow::trySendOneTranslate() {
    while (m_activeTranslate < kMaxConcurrentTranslate && !m_todoTranslateIds.isEmpty()) {
        int id = m_todoTranslateIds.takeFirst();
        Skill s = DaoSkill::skillById(id, m_db);
        if (s.id == 0 || s.description.isEmpty() || !s.descriptionZh.isEmpty()) continue;

        m_activeTranslate++;
        m_ai->translate(s.description,
            [this, id](const QString& zh, const QString& err) {
                m_activeTranslate--;
                if (!err.isEmpty()) {
                    qWarning() << "批量翻译失败 id=" << id << err;
                } else if (!zh.isEmpty()) {
                    Skill updated = DaoSkill::skillById(id, m_db);
                    if (updated.id != 0) {
                        updated.descriptionZh = zh;
                        DaoSkill::updateSkill(updated, m_db);
                    }
                }

                trySendOneTranslate();

                // 全部完成
                if (m_todoTranslateIds.isEmpty() && m_activeTranslate == 0) {
                    m_translateBatchBtn->setEnabled(true);
                    refreshTable();
                    refreshCategoryTree();
                    statusBar()->showMessage(tr("批量翻译完成"), 3000);
                } else {
                    int totalDone = 0;
                    // 扫 DB 里有多少已经翻好的做粗略进度
                    QSqlQuery q(m_db);
                    if (q.exec("SELECT COUNT(*) FROM skills WHERE description_zh<>'' AND description<>''")) {
                        if (q.next()) totalDone = q.value(0).toInt();
                    }
                    statusBar()->showMessage(
                        tr("翻译中... 已完成 %1").arg(totalDone));
                }
            });
    }
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
    QString question = m_ragInput->toPlainText().trimmed();
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

// ============================================================
// 主题切换
// ============================================================

QStringList MainWindow::availableThemes() {
    return { "obsidian", "linear", "paper", "forest" };
}

QString MainWindow::themeDisplayName(const QString& id) {
    static const QMap<QString, QString> map = {
        { "obsidian", "Obsidian · 深石板" },
        { "linear",   "Linear · 极深蓝"  },
        { "paper",    "Notion · 浅石墨"  },
        { "forest",   "森林 · 墨绿"      }
    };
    return map.value(id, id);
}

void MainWindow::applyTheme(const QString& themeName) {
    QString base = QCoreApplication::applicationDirPath() + "/resources/themes/";
    QString alt  = QCoreApplication::applicationDirPath() + "/../resources/themes/";
    QString qssPath;
    if (QFile::exists(base + themeName + ".qss"))
        qssPath = base + themeName + ".qss";
    else
        qssPath = alt + themeName + ".qss";

    QFile f(qssPath);
    if (f.open(QIODevice::ReadOnly)) {
        // 用 QApplication 覆盖全局样式，立即生效
        qobject_cast<QApplication*>(qApp)->setStyleSheet(f.readAll());
    }

    QSettings s("SkillManager", "SkillManager");
    s.setValue("ui/theme", themeName);
}
