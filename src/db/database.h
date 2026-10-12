#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QString>

// 单例管理 SQLite 连接
class Database : public QObject {
    Q_OBJECT
public:
    static Database& instance();
    static QSqlDatabase db();

    bool init();

private:
    explicit Database(QObject* parent = nullptr);
    ~Database() override;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    QString m_dbPath;
    QString m_connName;
    bool m_inited = false;
};

// 初始化 schema（执行一次 CREATE TABLE IF NOT EXISTS）
bool initSchema(QSqlDatabase& db);
