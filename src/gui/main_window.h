#pragma once
#include <QMainWindow>
#include <QSqlDatabase>

class SkillScanner;
class Classifier;
class RepoManager;
class DeepSeekClient;

class QTreeView;
class QTableView;
class QTabWidget;
class QLineEdit;
class QTextBrowser;
class QTextEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QSplitter;
class QTimer;

class CategoryModel;
class SkillTableModel;
class Skill;
class Repo;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(SkillScanner* scanner,
                        Classifier* classifier,
                        RepoManager* repoMgr,
                        DeepSeekClient* ai,
                        QSqlDatabase db,
                        QWidget* parent = nullptr);

    // 启动时自动扫描一次（main.cpp 里 QTimer::singleShot(0,...) 调用）
    Q_INVOKABLE void onAutoBootScan();

private slots:
    // 搜索 / 分类 / 刷新
    void onCategoryClicked(const QModelIndex& idx);
    void onSkillSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onSearchChanged(const QString& text);

    // 工具栏
    void onScanNew();
    void onRefresh();
    void onCheckUpdates();
    void onAddRepo();
    void onApiSettings();

    // 详情操作
    void onTranslateCurrent();
    void onEnableDisable();
    void onDeleteSkill();
    void onOpenDirectory();
    void onChapterClicked(int row);
    void onRagSend();

    // RepoManager 回调
    void onRepoAdded(const Repo& r);
    void onRepoUpdateChecked(const Repo& r, bool hasUpdate);
    void onRepoError(const QString& msg);

private:
    Skill currentSkill() const;
    void refreshTable();
    void refreshCategoryTree();
    void showSkillDetail(const Skill& s);
    void clearDetail();
    void appendRagAnswer(const QString& question, const QString& answer);
    void buildUi();
    void connectSignals();
    void setupRepoPolling();

    SkillScanner*   m_scanner;
    Classifier*     m_classifier;
    RepoManager*    m_repoMgr;
    DeepSeekClient* m_ai;
    QSqlDatabase    m_db;

    // 中心三栏
    QSplitter*    m_hSplitter = nullptr;
    QSplitter*    m_vSplitter = nullptr;
    QTreeView*    m_catTree   = nullptr;
    QTableView*   m_skillTable = nullptr;
    QTabWidget*   m_detailTabs = nullptr;

    CategoryModel*    m_catModel   = nullptr;
    SkillTableModel*  m_tableModel = nullptr;

    // 顶部工具栏
    QLineEdit*    m_searchEdit  = nullptr;
    QPushButton*  m_scanBtn     = nullptr;
    QPushButton*  m_refreshBtn  = nullptr;
    QPushButton*  m_checkBtn    = nullptr;
    QPushButton*  m_addRepoBtn  = nullptr;
    QPushButton*  m_apiBtn      = nullptr;

    // Tab 1: 概览
    QLabel*       m_overviewName     = nullptr;
    QLabel*       m_overviewCategory = nullptr;
    QLabel*       m_overviewSource   = nullptr;
    QTextBrowser* m_overviewDesc     = nullptr;
    QLabel*       m_overviewStatus   = nullptr;

    // Tab 2: SKILL.md
    QTextBrowser* m_skillMdView      = nullptr;

    // Tab 3: 章节/术语
    QListWidget*  m_chapterList      = nullptr;
    QTextBrowser* m_chapterContent   = nullptr;

    // Tab 4: RAG 问答
    QTextBrowser* m_ragView          = nullptr;
    QTextEdit*    m_ragInput         = nullptr;
    QPushButton*  m_ragSendBtn       = nullptr;

    // Tab 5: 元数据/操作
    QLabel*       m_metaDirectory    = nullptr;
    QLabel*       m_metaSkillMd      = nullptr;
    QLabel*       m_metaSourceSub    = nullptr;
    QTextBrowser* m_metaFiles        = nullptr;
    QPushButton*  m_translateBtn     = nullptr;
    QPushButton*  m_enableBtn        = nullptr;
    QPushButton*  m_deleteBtn        = nullptr;
    QPushButton*  m_openDirBtn       = nullptr;

    // Debounce
    QTimer* m_searchDebounce = nullptr;
    QString m_pendingSearch;
};
