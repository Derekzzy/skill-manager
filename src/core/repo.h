#pragma once
#include <QString>

struct Repo {
    int id = 0;
    QString name;
    QString url;
    QString provider;
    QString localPath;
    QString branch;
    QString currentCommit;
    QString latestCommit;
    qint64 lastPoll = 0;
    bool hasUpdate = false;
    bool enabled = true;
    QString notes;
};
Q_DECLARE_METATYPE(Repo)
