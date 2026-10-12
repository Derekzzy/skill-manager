#pragma once
#include <QSqlDatabase>
#include <QList>
#include "../core/skill.h"
#include "../core/repo.h"

class DaoSkill {
public:
    // Skill CRUD
    static int insertSkill(const Skill& s, QSqlDatabase db);
    static bool updateSkill(const Skill& s, QSqlDatabase db);
    static Skill skillById(int id, QSqlDatabase db);
    static Skill skillByDir(const QString& dir, QSqlDatabase db);
    static QList<Skill> allSkills(QSqlDatabase db,
                                  bool enabledOnly = false,
                                  const QString& category = {},
                                  const QString& search = {});
    static bool deleteSkill(int id, QSqlDatabase db);
    static bool setEnabled(int id, bool enabled, QSqlDatabase db);

    // Repo CRUD
    static int insertRepo(const Repo& r, QSqlDatabase db);
    static bool updateRepo(const Repo& r, QSqlDatabase db);
    static Repo repoById(int id, QSqlDatabase db);
    static Repo repoByName(const QString& name, QSqlDatabase db);
    static QList<Repo> allRepos(QSqlDatabase db);
    static bool deleteRepo(int id, QSqlDatabase db);
};
