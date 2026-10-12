#include "database.h"
#include <QStandardPaths>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

static const char* kConnName = "skill-manager-main";

Database::Database(QObject* parent)
    : QObject(parent)
    , m_connName(kConnName)
{
    QString appDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDir.isEmpty()) {
        appDir = QDir::homePath() + "/.skill-manager";
    }
    QDir().mkpath(appDir);
    m_dbPath = appDir + "/skill.db";
}

Database::~Database() {
    QSqlDatabase::removeDatabase(m_connName);
}

Database& Database::instance() {
    static Database inst;
    return inst;
}

bool Database::init() {
    if (m_inited) return true;

    QSqlDatabase conn;
    if (QSqlDatabase::contains(m_connName)) {
        conn = QSqlDatabase::database(m_connName);
    } else {
        conn = QSqlDatabase::addDatabase("QSQLITE", m_connName);
        conn.setDatabaseName(m_dbPath);
    }

    if (!conn.open()) {
        qCritical() << "Database::init open failed:" << conn.lastError().text();
        return false;
    }

    // 首次打开时自动跑 schema
    if (!initSchema(conn)) {
        qCritical() << "Database::init schema failed";
        return false;
    }

    m_inited = true;
    qInfo() << "Database opened:" << m_dbPath;
    return true;
}

QSqlDatabase Database::db() {
    QSqlDatabase conn = QSqlDatabase::database(kConnName);
    if (!conn.isOpen()) {
        conn.open();
    }
    return conn;
}

bool initSchema(QSqlDatabase& db) {
    static const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS skills (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  clean_name TEXT,
  canonical_name TEXT,
  directory TEXT NOT NULL UNIQUE,
  skill_md_path TEXT NOT NULL,
  description TEXT,
  description_zh TEXT,
  body TEXT,
  source TEXT DEFAULT 'local',
  repo_id INTEGER,
  source_subpath TEXT,
  standard TEXT,
  has_chapters INTEGER DEFAULT 0,
  has_patterns  INTEGER DEFAULT 0,
  has_glossary  INTEGER DEFAULT 0,
  has_cheatsheet INTEGER DEFAULT 0,
  total_files INTEGER DEFAULT 0,
  file_size  INTEGER DEFAULT 0,
  file_count INTEGER DEFAULT 0,
  mtime INTEGER DEFAULT 0,
  is_new INTEGER DEFAULT 0,
  enabled INTEGER DEFAULT 1,
  note TEXT,
  category TEXT,
  tags TEXT,
  created_at INTEGER,
  updated_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_skills_category ON skills(category);
CREATE INDEX IF NOT EXISTS idx_skills_name ON skills(name);
CREATE INDEX IF NOT EXISTS idx_skills_enabled ON skills(enabled);
CREATE INDEX IF NOT EXISTS idx_skills_repo ON skills(repo_id);

CREATE TABLE IF NOT EXISTS remote_repos (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT UNIQUE NOT NULL,
  url TEXT NOT NULL,
  provider TEXT DEFAULT 'github',
  local_path TEXT NOT NULL UNIQUE,
  branch TEXT DEFAULT 'main',
  current_commit TEXT,
  latest_commit TEXT,
  last_poll INTEGER,
  has_update INTEGER DEFAULT 0,
  enabled INTEGER DEFAULT 1,
  notes TEXT,
  created_at INTEGER,
  updated_at INTEGER
);

CREATE TABLE IF NOT EXISTS categories (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT UNIQUE NOT NULL,
  keywords TEXT NOT NULL,
  parent_id INTEGER DEFAULT 0,
  auto_assign INTEGER DEFAULT 1,
  sort_order INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS query_history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  skill_id INTEGER,
  question TEXT NOT NULL,
  answer TEXT,
  source_files TEXT,
  model TEXT,
  created_at INTEGER
);

CREATE TABLE IF NOT EXISTS settings (
  key TEXT PRIMARY KEY,
  value TEXT,
  updated_at INTEGER
);
)SQL";

    QStringList stmts = QString::fromUtf8(kSchema).split(';', Qt::SkipEmptyParts);
    for (const QString& s : stmts) {
        QString trimmed = s.trimmed();
        if (trimmed.isEmpty()) continue;
        QSqlQuery q(db);
        if (!q.exec(trimmed)) {
            qWarning() << "initSchema exec failed:" << q.lastError().text() << "\nSQL:" << trimmed;
            return false;
        }
    }
    return true;
}
