#pragma once
#include <QSqlDatabase>

class Seed {
public:
    static void run(QSqlDatabase& db);
};
