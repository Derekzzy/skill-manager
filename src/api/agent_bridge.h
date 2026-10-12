#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <QByteArray>
#include "../core/skill.h"

class DeepSeekClient;

class AgentBridge : public QObject {
    Q_OBJECT
public:
    explicit AgentBridge(QSqlDatabase db,
                        DeepSeekClient* ai,
                        QObject* parent = nullptr);

    ~AgentBridge() override;

    int runCli(const QStringList& args);

    bool startHttp(quint16 port = 17234);
    void stopHttp();

    QString lastError() const { return m_lastError; }

signals:
    void httpStarted(quint16 port);
    void httpStopped();

private slots:
    void onNewConnection();

private:
    QSqlDatabase m_db;
    DeepSeekClient* m_ai;
    QTcpServer* m_server = nullptr;
    QString m_lastError;

    void handleRequest(QTcpSocket* sock,
                       const QString& method,
                       const QString& path,
                       const QByteArray& body);

    void handleGetSkills(QTcpSocket* sock, const QString& path);
    void handleGetSkillDetail(QTcpSocket* sock, const QString& path);
    void handleGetSkillContent(QTcpSocket* sock, const QString& path);
    void handleRagQuery(QTcpSocket* sock, const QByteArray& body);
    void handleTranslate(QTcpSocket* sock, const QByteArray& body);
    void handleReposList(QTcpSocket* sock);
    void handleRepoAdd(QTcpSocket* sock, const QByteArray& body);
    void handleRepoCheck(QTcpSocket* sock, int repoId);
    void handleRepoPull(QTcpSocket* sock, int repoId);
    void handleRepoDelete(QTcpSocket* sock, int repoId);

    // CLI 子命令
    int cliList(const QStringList& args);
    int cliShow(const QString& idOrName);
    int cliContent(const QString& idOrName);
    int cliQuery(const QString& idOrName, const QString& question);
    int cliTranslate(const QString& text);
    int cliRepos();
    int cliRepoAdd(const QString& url);
    int cliRepoCheck(int id);
    int cliRepoPull(int id);

    // 工具
    QString readSkillContent(const Skill& s);
    Skill   resolveSkill(const QString& idOrName);

    void sendJson(QTcpSocket* sock, int code, const QJsonObject& obj);
    void sendText(QTcpSocket* sock, int code, const QByteArray& text,
                  const QByteArray& contentType = "text/plain");
};
