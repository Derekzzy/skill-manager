#include "dao_skill.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QStringConverter>

// ============ helpers ============
static QString safe(const QString& s) {
    return s;
}

static Skill buildSkillFromQuery(const QSqlQuery& q) {
    Skill s;
    s.id            = q.value("id").toInt();
    s.name          = q.value("name").toString();
    s.cleanName     = q.value("clean_name").toString();
    s.canonicalName = q.value("canonical_name").toString();
    s.directory     = q.value("directory").toString();
    s.skillMdPath   = q.value("skill_md_path").toString();
    s.description   = q.value("description").toString();
    s.descriptionZh = q.value("description_zh").toString();
    s.body          = q.value("body").toString();
    s.source        = q.value("source").toString();
    s.repoId        = q.value("repo_id").toInt();
    s.sourceSubpath = q.value("source_subpath").toString();
    s.standard      = q.value("standard").toString();
    s.hasChapters   = q.value("has_chapters").toInt() != 0;
    s.hasPatterns   = q.value("has_patterns").toInt() != 0;
    s.hasGlossary   = q.value("has_glossary").toInt() != 0;
    s.hasCheatsheet = q.value("has_cheatsheet").toInt() != 0;
    s.totalFiles    = q.value("total_files").toInt();
    s.fileSize      = q.value("file_size").toLongLong();
    s.fileCount     = q.value("file_count").toInt();
    s.mtime         = q.value("mtime").toLongLong();
    s.isNew         = q.value("is_new").toInt() != 0;
    s.enabled       = q.value("enabled").toInt() != 0;
    s.note          = q.value("note").toString();
    s.category      = q.value("category").toString();
    s.tags          = q.value("tags").toString();
    s.createdAt     = q.value("created_at").toLongLong();
    s.updatedAt     = q.value("updated_at").toLongLong();
    return s;
}

static Repo buildRepoFromQuery(const QSqlQuery& q) {
    Repo r;
    r.id            = q.value("id").toInt();
    r.name          = q.value("name").toString();
    r.url           = q.value("url").toString();
    r.provider      = q.value("provider").toString();
    r.localPath     = q.value("local_path").toString();
    r.branch        = q.value("branch").toString();
    r.currentCommit = q.value("current_commit").toString();
    r.latestCommit  = q.value("latest_commit").toString();
    r.lastPoll      = q.value("last_poll").toLongLong();
    r.hasUpdate     = q.value("has_update").toInt() != 0;
    r.enabled       = q.value("enabled").toInt() != 0;
    r.notes         = q.value("notes").toString();
    return r;
}

// ============ Skill ============
int DaoSkill::insertSkill(const Skill& s, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare(R"SQL(INSERT INTO skills
    (name, clean_name, canonical_name, directory, skill_md_path,
     description, description_zh, body, source, repo_id, source_subpath, standard,
     has_chapters, has_patterns, has_glossary, has_cheatsheet,
     total_files, file_size, file_count, mtime, is_new, enabled,
     note, category, tags, created_at, updated_at)
    VALUES
    (:name, :clean_name, :canonical_name, :directory, :skill_md_path,
     :description, :description_zh, :body, :source, :repo_id, :source_subpath, :standard,
     :has_chapters, :has_patterns, :has_glossary, :has_cheatsheet,
     :total_files, :file_size, :file_count, :mtime, :is_new, :enabled,
     :note, :category, :tags, :created_at, :updated_at))SQL");

    qint64 now = QDateTime::currentSecsSinceEpoch();
    q.bindValue(":name", s.name);
    q.bindValue(":clean_name", s.cleanName);
    q.bindValue(":canonical_name", s.canonicalName);
    q.bindValue(":directory", s.directory);
    q.bindValue(":skill_md_path", s.skillMdPath);
    q.bindValue(":description", s.description);
    q.bindValue(":description_zh", s.descriptionZh);
    q.bindValue(":body", s.body);
    q.bindValue(":source", s.source.isEmpty() ? "local" : s.source);
    q.bindValue(":repo_id", s.repoId == 0 ? QVariant() : s.repoId);
    q.bindValue(":source_subpath", s.sourceSubpath);
    q.bindValue(":standard", s.standard);
    q.bindValue(":has_chapters", s.hasChapters ? 1 : 0);
    q.bindValue(":has_patterns", s.hasPatterns ? 1 : 0);
    q.bindValue(":has_glossary", s.hasGlossary ? 1 : 0);
    q.bindValue(":has_cheatsheet", s.hasCheatsheet ? 1 : 0);
    q.bindValue(":total_files", s.totalFiles);
    q.bindValue(":file_size", static_cast<qint64>(s.fileSize));
    q.bindValue(":file_count", s.fileCount);
    q.bindValue(":mtime", static_cast<qint64>(s.mtime));
    q.bindValue(":is_new", s.isNew ? 1 : 0);
    q.bindValue(":enabled", s.enabled ? 1 : 0);
    q.bindValue(":note", s.note);
    q.bindValue(":category", s.category);
    q.bindValue(":tags", s.tags);
    q.bindValue(":created_at", now);
    q.bindValue(":updated_at", now);

    if (!q.exec()) {
        qWarning() << "insertSkill failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool DaoSkill::updateSkill(const Skill& s, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare(R"SQL(UPDATE skills SET
        name=:name, clean_name=:clean_name, canonical_name=:canonical_name,
        description=:description, description_zh=:description_zh, body=:body,
        source=:source, repo_id=:repo_id, source_subpath=:source_subpath, standard=:standard,
        has_chapters=:has_chapters, has_patterns=:has_patterns,
        has_glossary=:has_glossary, has_cheatsheet=:has_cheatsheet,
        total_files=:total_files, file_size=:file_size, file_count=:file_count,
        mtime=:mtime, is_new=:is_new, enabled=:enabled,
        note=:note, category=:category, tags=:tags, updated_at=:updated_at
    WHERE id=:id)SQL");

    qint64 now = QDateTime::currentSecsSinceEpoch();
    q.bindValue(":id", s.id);
    q.bindValue(":name", s.name);
    q.bindValue(":clean_name", s.cleanName);
    q.bindValue(":canonical_name", s.canonicalName);
    q.bindValue(":description", s.description);
    q.bindValue(":description_zh", s.descriptionZh);
    q.bindValue(":body", s.body);
    q.bindValue(":source", s.source.isEmpty() ? "local" : s.source);
    q.bindValue(":repo_id", s.repoId == 0 ? QVariant() : s.repoId);
    q.bindValue(":source_subpath", s.sourceSubpath);
    q.bindValue(":standard", s.standard);
    q.bindValue(":has_chapters", s.hasChapters ? 1 : 0);
    q.bindValue(":has_patterns", s.hasPatterns ? 1 : 0);
    q.bindValue(":has_glossary", s.hasGlossary ? 1 : 0);
    q.bindValue(":has_cheatsheet", s.hasCheatsheet ? 1 : 0);
    q.bindValue(":total_files", s.totalFiles);
    q.bindValue(":file_size", static_cast<qint64>(s.fileSize));
    q.bindValue(":file_count", s.fileCount);
    q.bindValue(":mtime", static_cast<qint64>(s.mtime));
    q.bindValue(":is_new", s.isNew ? 1 : 0);
    q.bindValue(":enabled", s.enabled ? 1 : 0);
    q.bindValue(":note", s.note);
    q.bindValue(":category", s.category);
    q.bindValue(":tags", s.tags);
    q.bindValue(":updated_at", now);

    if (!q.exec()) {
        qWarning() << "updateSkill failed:" << q.lastError().text();
        return false;
    }
    return true;
}

Skill DaoSkill::skillById(int id, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("SELECT * FROM skills WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec() || !q.next()) {
        return Skill{};
    }
    return buildSkillFromQuery(q);
}

Skill DaoSkill::skillByDir(const QString& dir, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("SELECT * FROM skills WHERE directory = :dir");
    q.bindValue(":dir", dir);
    if (!q.exec() || !q.next()) {
        return Skill{};
    }
    return buildSkillFromQuery(q);
}

QList<Skill> DaoSkill::allSkills(QSqlDatabase db,
                                 bool enabledOnly,
                                 const QString& category,
                                 const QString& search) {
    QString sql = "SELECT * FROM skills WHERE 1=1";
    QList<QPair<QString, QVariant>> binds;

    if (enabledOnly) {
        sql += " AND enabled = 1";
    }
    if (!category.isEmpty()) {
        sql += " AND category = :cat";
        binds.append({":cat", category});
    }
    if (!search.isEmpty()) {
        sql += " AND (name LIKE :kw OR description LIKE :kw OR canonical_name LIKE :kw)";
        binds.append({":kw", QString("%%1%").arg(search)});
    }
    sql += " ORDER BY name ASC";

    QSqlQuery q(db);
    q.prepare(sql);
    for (const auto& b : binds) {
        q.bindValue(b.first, b.second);
    }
    if (!q.exec()) {
        qWarning() << "allSkills failed:" << q.lastError().text();
        return {};
    }

    QList<Skill> result;
    while (q.next()) {
        result.append(buildSkillFromQuery(q));
    }
    return result;
}

bool DaoSkill::deleteSkill(int id, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("DELETE FROM skills WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}

bool DaoSkill::setEnabled(int id, bool enabled, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("UPDATE skills SET enabled = :e, updated_at = :ua WHERE id = :id");
    q.bindValue(":e", enabled ? 1 : 0);
    q.bindValue(":ua", QDateTime::currentSecsSinceEpoch());
    q.bindValue(":id", id);
    return q.exec();
}

// ============ Repo ============
int DaoSkill::insertRepo(const Repo& r, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare(R"SQL(INSERT INTO remote_repos
        (name, url, provider, local_path, branch,
         current_commit, latest_commit, last_poll, has_update, enabled, notes,
         created_at, updated_at)
        VALUES
        (:name, :url, :provider, :local_path, :branch,
         :current_commit, :latest_commit, :last_poll, :has_update, :enabled, :notes,
         :created_at, :updated_at))SQL");

    qint64 now = QDateTime::currentSecsSinceEpoch();
    q.bindValue(":name", r.name);
    q.bindValue(":url", r.url);
    q.bindValue(":provider", r.provider.isEmpty() ? "github" : r.provider);
    q.bindValue(":local_path", r.localPath);
    q.bindValue(":branch", r.branch.isEmpty() ? "main" : r.branch);
    q.bindValue(":current_commit", r.currentCommit);
    q.bindValue(":latest_commit", r.latestCommit);
    q.bindValue(":last_poll", static_cast<qint64>(r.lastPoll));
    q.bindValue(":has_update", r.hasUpdate ? 1 : 0);
    q.bindValue(":enabled", r.enabled ? 1 : 0);
    q.bindValue(":notes", r.notes);
    q.bindValue(":created_at", now);
    q.bindValue(":updated_at", now);

    if (!q.exec()) {
        qWarning() << "insertRepo failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool DaoSkill::updateRepo(const Repo& r, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare(R"SQL(UPDATE remote_repos SET
        name=:name, url=:url, provider=:provider, local_path=:local_path, branch=:branch,
        current_commit=:current_commit, latest_commit=:latest_commit,
        last_poll=:last_poll, has_update=:has_update, enabled=:enabled, notes=:notes,
        updated_at=:updated_at
    WHERE id=:id)SQL");

    q.bindValue(":id", r.id);
    q.bindValue(":name", r.name);
    q.bindValue(":url", r.url);
    q.bindValue(":provider", r.provider.isEmpty() ? "github" : r.provider);
    q.bindValue(":local_path", r.localPath);
    q.bindValue(":branch", r.branch.isEmpty() ? "main" : r.branch);
    q.bindValue(":current_commit", r.currentCommit);
    q.bindValue(":latest_commit", r.latestCommit);
    q.bindValue(":last_poll", static_cast<qint64>(r.lastPoll));
    q.bindValue(":has_update", r.hasUpdate ? 1 : 0);
    q.bindValue(":enabled", r.enabled ? 1 : 0);
    q.bindValue(":notes", r.notes);
    q.bindValue(":updated_at", QDateTime::currentSecsSinceEpoch());

    if (!q.exec()) {
        qWarning() << "updateRepo failed:" << q.lastError().text();
        return false;
    }
    return true;
}

Repo DaoSkill::repoById(int id, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("SELECT * FROM remote_repos WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec() || !q.next()) return Repo{};
    return buildRepoFromQuery(q);
}

Repo DaoSkill::repoByName(const QString& name, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("SELECT * FROM remote_repos WHERE name = :name");
    q.bindValue(":name", name);
    if (!q.exec() || !q.next()) return Repo{};
    return buildRepoFromQuery(q);
}

QList<Repo> DaoSkill::allRepos(QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("SELECT * FROM remote_repos ORDER BY name ASC");
    if (!q.exec()) {
        qWarning() << "allRepos failed:" << q.lastError().text();
        return {};
    }
    QList<Repo> result;
    while (q.next()) result.append(buildRepoFromQuery(q));
    return result;
}

bool DaoSkill::deleteRepo(int id, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("DELETE FROM remote_repos WHERE id = :id");
    q.bindValue(":id", id);
    return q.exec();
}
