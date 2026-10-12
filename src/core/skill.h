#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QMetaType>

struct Skill {
    int id = 0;
    QString name;
    QString cleanName;
    QString canonicalName;
    QString directory;
    QString skillMdPath;
    QString description;
    QString descriptionZh;
    QString body;
    QString source;       // local / git-root / git-sub
    int repoId = 0;
    QString sourceSubpath;
    QString standard;     // doubao / agent-skills / github-copilot
    bool hasChapters = false;
    bool hasPatterns = false;
    bool hasGlossary = false;
    bool hasCheatsheet = false;
    int totalFiles = 0;
    qint64 fileSize = 0;
    int fileCount = 0;
    qint64 mtime = 0;
    bool isNew = false;
    bool enabled = true;
    QString note;
    QString category;
    QString tags;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};
Q_DECLARE_METATYPE(Skill)
