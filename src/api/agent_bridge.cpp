#include "agent_bridge.h"
#include "deepseek_client.h"
#include "../core/skill.h"
#include "../core/repo.h"
#include "../db/dao_skill.h"

#include <QCoreApplication>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QDebug>

AgentBridge::AgentBridge(QSqlDatabase db, DeepSeekClient* ai, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_ai(ai)
{
}

AgentBridge::~AgentBridge() {
    stopHttp();
}

// -----------------------------------------------------------------------------
// HTTP server
// -----------------------------------------------------------------------------

void AgentBridge::onNewConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        if (!sock) continue;

        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            if (sock->state() != QAbstractSocket::ConnectedState) return;

            // 先读到 headers 结束
            QByteArray buf;
            const int maxHeaders = 65536;
            while (buf.indexOf("\r\n\r\n") < 0) {
                if (sock->bytesAvailable() == 0) {
                    if (!sock->waitForReadyRead(500)) return;
                }
                QByteArray chunk = sock->read(maxHeaders - buf.size());
                if (chunk.isEmpty()) break;
                buf.append(chunk);
                if (buf.size() >= maxHeaders) break;
            }
            if (!buf.contains("\r\n\r\n")) {
                sendText(sock, 400, "Bad Request");
                sock->disconnectFromHost();
                return;
            }

            int splitIdx = buf.indexOf("\r\n\r\n");
            QByteArray headRaw = buf.left(splitIdx);
            QByteArray bodyPart = buf.mid(splitIdx + 4);

            QStringList lines = QString::fromUtf8(headRaw).split("\r\n");
            if (lines.isEmpty()) {
                sendText(sock, 400, "Bad Request");
                sock->disconnectFromHost();
                return;
            }

            QStringList head1 = lines.first().split(' ');
            if (head1.size() < 2) {
                sendText(sock, 400, "Bad Request");
                sock->disconnectFromHost();
                return;
            }
            QString method = head1.at(0);
            QString rawPath = head1.at(1);

            // 解析 headers，找 Content-Length
            int contentLength = bodyPart.size();
            for (int i = 1; i < lines.size(); ++i) {
                const QString& line = lines.at(i);
                int colon = line.indexOf(':');
                if (colon <= 0) continue;
                QString key = line.left(colon).trimmed().toLower();
                QString val = line.mid(colon + 1).trimmed();
                if (key == "content-length") {
                    contentLength = val.toInt();
                }
            }

            // 继续读取剩余 body
            while (bodyPart.size() < contentLength) {
                if (!sock->waitForReadyRead(1500)) break;
                QByteArray more = sock->read(contentLength - bodyPart.size());
                if (more.isEmpty()) break;
                bodyPart.append(more);
            }
            QByteArray body = bodyPart.left(contentLength);

            Q_UNUSED(method);
            handleRequest(sock, method, rawPath, body);
        });

        // socket 断开后清理
        connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
    }
}

bool AgentBridge::startHttp(quint16 port) {
    if (m_server) {
        m_lastError = "HTTP server already running";
        return false;
    }
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &AgentBridge::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        m_lastError = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        return false;
    }
    emit httpStarted(port);
    return true;
}

void AgentBridge::stopHttp() {
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
        emit httpStopped();
    }
}

void AgentBridge::handleRequest(QTcpSocket* sock, const QString& method,
                                const QString& rawPath, const QByteArray& body)
{
    // 去掉 query 串
    QString path = rawPath;
    int qIdx = path.indexOf('?');
    QString query;
    if (qIdx >= 0) {
        query = path.mid(qIdx + 1);
        path = path.left(qIdx);
    }

    if (path == "/health") {
        sendJson(sock, 200, QJsonObject{{"ok", true}, {"version", "1.0"}});
        sock->disconnectFromHost();
        return;
    }

    // GET /v1/skills
    if (method == "GET" && path == "/v1/skills") {
        handleGetSkills(sock, query);
        sock->disconnectFromHost();
        return;
    }

    // GET /v1/skills/<id> 或 /v1/skills/<id>/content
    QRegularExpression reSkill("^/v1/skills/([^/]+)(/content)?$");
    auto match = reSkill.match(path);
    if (match.hasMatch()) {
        QString idStr = match.captured(1);
        bool isContent = match.captured(2) == "/content";
        if (isContent) {
            handleGetSkillContent(sock, idStr);
        } else {
            handleGetSkillDetail(sock, idStr);
        }
        sock->disconnectFromHost();
        return;
    }

    // POST /v1/query
    if (method == "POST" && path == "/v1/query") {
        handleRagQuery(sock, body);
        sock->disconnectFromHost();
        return;
    }

    // POST /v1/translate
    if (method == "POST" && path == "/v1/translate") {
        handleTranslate(sock, body);
        sock->disconnectFromHost();
        return;
    }

    // Repos
    if (path == "/v1/repos") {
        if (method == "GET") { handleReposList(sock); }
        else if (method == "POST") { handleRepoAdd(sock, body); }
        else { sendText(sock, 405, "Method Not Allowed"); }
        sock->disconnectFromHost();
        return;
    }

    QRegularExpression reRepo("^/v1/repos/(\\d+)(/(check|pull))?$");
    auto m2 = reRepo.match(path);
    if (m2.hasMatch()) {
        int id = m2.captured(1).toInt();
        QString sub = m2.captured(3);
        if (sub == "check" && method == "POST") {
            handleRepoCheck(sock, id);
        } else if (sub == "pull" && method == "POST") {
            handleRepoPull(sock, id);
        } else if (sub.isEmpty() && method == "DELETE") {
            handleRepoDelete(sock, id);
        } else {
            sendText(sock, 405, "Method Not Allowed");
        }
        sock->disconnectFromHost();
        return;
    }

    sendText(sock, 404, "Not Found");
    sock->disconnectFromHost();
}

// ---- HTTP handlers ----

void AgentBridge::handleGetSkills(QTcpSocket* sock, const QString& queryStr) {
    QUrlQuery q(queryStr);
    QString category = q.queryItemValue("category");
    QString search   = q.queryItemValue("q");
    bool enabledOnly = q.queryItemValue("enabled") == "1";

    QList<Skill> list = DaoSkill::allSkills(m_db, enabledOnly, category, search);

    QJsonArray arr;
    for (const auto& s : list) {
        QJsonObject o;
        o["id"]              = s.id;
        o["name"]            = s.name;
        o["canonical_name"]  = s.canonicalName;
        o["description"]     = s.description;
        o["description_zh"]  = s.descriptionZh;
        o["category"]        = s.category;
        o["enabled"]         = s.enabled;
        o["tags"]            = s.tags;
        o["directory"]       = s.directory;
        o["has_chapters"]    = s.hasChapters;
        o["has_patterns"]    = s.hasPatterns;
        o["has_glossary"]    = s.hasGlossary;
        o["has_cheatsheet"]  = s.hasCheatsheet;
        arr.append(o);
    }
    QJsonObject root{{"count", arr.size()}, {"skills", arr}};
    sendJson(sock, 200, root);
}

void AgentBridge::handleGetSkillDetail(QTcpSocket* sock, const QString& idOrName) {
    Skill s = resolveSkill(idOrName);
    if (s.id == 0) {
        sendJson(sock, 404, QJsonObject{{"error", "skill not found"}});
        return;
    }

    QJsonObject o;
    o["id"]              = s.id;
    o["name"]            = s.name;
    o["canonical_name"]  = s.canonicalName;
    o["description"]     = s.description;
    o["description_zh"]  = s.descriptionZh;
    o["body"]            = s.body;
    o["category"]        = s.category;
    o["enabled"]         = s.enabled;
    o["tags"]            = s.tags;
    o["directory"]       = s.directory;
    o["skill_md_path"]   = s.skillMdPath;
    o["has_chapters"]    = s.hasChapters;
    o["has_patterns"]    = s.hasPatterns;
    o["has_glossary"]    = s.hasGlossary;
    o["has_cheatsheet"]  = s.hasCheatsheet;

    // chapters 文件列表
    QJsonArray chapters;
    QDir dir(s.directory);
    if (QDir().exists(dir.filePath("chapters"))) {
        QDir cdir(dir.filePath("chapters"));
        for (const auto& f : cdir.entryInfoList({"*.md"}, QDir::Files | QDir::NoDotAndDotDot)) {
            chapters.append(f.fileName());
        }
    }
    o["chapters"] = chapters;

    sendJson(sock, 200, o);
}

void AgentBridge::handleGetSkillContent(QTcpSocket* sock, const QString& idOrName) {
    Skill s = resolveSkill(idOrName);
    if (s.id == 0) {
        sendJson(sock, 404, QJsonObject{{"error", "skill not found"}});
        return;
    }
    QJsonObject o;
    o["id"]      = s.id;
    o["content"] = readSkillContent(s);
    sendJson(sock, 200, o);
}

void AgentBridge::handleRagQuery(QTcpSocket* sock, const QByteArray& body) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        sendJson(sock, 400, QJsonObject{{"error", "invalid JSON"}});
        return;
    }
    QJsonObject obj = doc.object();
    QString question = obj.value("question").toString();
    QJsonArray ids   = obj.value("skill_ids").toArray();

    if (question.isEmpty()) {
        sendJson(sock, 400, QJsonObject{{"error", "question required"}});
        return;
    }

    QStringList skillContents;
    for (const auto& v : ids) {
        int sid = v.toInt();
        Skill s = DaoSkill::skillById(sid, m_db);
        if (s.id != 0) {
            skillContents.append(readSkillContent(s));
        }
    }
    if (skillContents.isEmpty()) {
        sendJson(sock, 400, QJsonObject{{"error", "no valid skills"}});
        return;
    }
    QString merged = skillContents.join("\n\n===== 下一个技能 =====\n\n");

    if (!m_ai || !m_ai->isConfigured()) {
        sendJson(sock, 503, QJsonObject{{"error", "AI client not configured"}});
        return;
    }

    // 异步等待
    QEventLoop loop;
    QString answer, errMsg;
    m_ai->rag(merged, question, [&](const QString& a, const QString& e) {
        answer = a; errMsg = e;
        loop.quit();
    });
    if (loop.exec(QEventLoop::ExcludeUserInputEvents) == -1) {}

    if (!errMsg.isEmpty()) {
        sendJson(sock, 502, QJsonObject{{"error", errMsg}});
    } else {
        sendJson(sock, 200, QJsonObject{{"answer", answer}});
    }
}

void AgentBridge::handleTranslate(QTcpSocket* sock, const QByteArray& body) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        sendJson(sock, 400, QJsonObject{{"error", "invalid JSON"}});
        return;
    }
    QString text = doc.object().value("text").toString();
    if (text.isEmpty()) {
        sendJson(sock, 400, QJsonObject{{"error", "text required"}});
        return;
    }
    if (!m_ai || !m_ai->isConfigured()) {
        sendJson(sock, 503, QJsonObject{{"error", "AI client not configured"}});
        return;
    }

    QEventLoop loop;
    QString zh, errMsg;
    m_ai->translate(text, [&](const QString& a, const QString& e) {
        zh = a; errMsg = e;
        loop.quit();
    });
    loop.exec(QEventLoop::ExcludeUserInputEvents);

    if (!errMsg.isEmpty()) {
        sendJson(sock, 502, QJsonObject{{"error", errMsg}});
    } else {
        sendJson(sock, 200, QJsonObject{{"zh", zh}});
    }
}

void AgentBridge::handleReposList(QTcpSocket* sock) {
    QList<Repo> list = DaoSkill::allRepos(m_db);
    QJsonArray arr;
    for (const auto& r : list) {
        QJsonObject o;
        o["id"]              = r.id;
        o["name"]            = r.name;
        o["url"]             = r.url;
        o["provider"]        = r.provider;
        o["local_path"]      = r.localPath;
        o["branch"]          = r.branch;
        o["current_commit"]  = r.currentCommit;
        o["latest_commit"]   = r.latestCommit;
        o["has_update"]      = r.hasUpdate;
        o["enabled"]         = r.enabled;
        arr.append(o);
    }
    sendJson(sock, 200, QJsonObject{{"count", arr.size()}, {"repos", arr}});
}

void AgentBridge::handleRepoAdd(QTcpSocket* sock, const QByteArray& body) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        sendJson(sock, 400, QJsonObject{{"error", "invalid JSON"}});
        return;
    }
    QString url = doc.object().value("url").toString();
    if (url.isEmpty()) {
        sendJson(sock, 400, QJsonObject{{"error", "url required"}});
        return;
    }
    Repo r;
    r.url = url;
    r.name = QFileInfo(url).completeBaseName();
    if (r.name.isEmpty()) r.name = url;
    int id = DaoSkill::insertRepo(r, m_db);
    if (id <= 0) {
        sendJson(sock, 500, QJsonObject{{"error", "insert failed"}});
    } else {
        sendJson(sock, 200, QJsonObject{{"id", id}, {"name", r.name}, {"url", url}});
    }
}

void AgentBridge::handleRepoCheck(QTcpSocket* sock, int repoId) {
    Repo r = DaoSkill::repoById(repoId, m_db);
    if (r.id == 0) {
        sendJson(sock, 404, QJsonObject{{"error", "repo not found"}});
        return;
    }
    // 占位：实际 check 需要 git 命令
    sendJson(sock, 200, QJsonObject{{"id", repoId}, {"has_update", r.hasUpdate}});
}

void AgentBridge::handleRepoPull(QTcpSocket* sock, int repoId) {
    Repo r = DaoSkill::repoById(repoId, m_db);
    if (r.id == 0) {
        sendJson(sock, 404, QJsonObject{{"error", "repo not found"}});
        return;
    }
    sendJson(sock, 200, QJsonObject{{"id", repoId}, {"message", "pull placeholder"}});
}

void AgentBridge::handleRepoDelete(QTcpSocket* sock, int repoId) {
    if (!DaoSkill::deleteRepo(repoId, m_db)) {
        sendJson(sock, 404, QJsonObject{{"error", "repo not found"}});
    } else {
        sendJson(sock, 200, QJsonObject{{"id", repoId}});
    }
}

// ---- sendJson ----

void AgentBridge::sendJson(QTcpSocket* sock, int code, const QJsonObject& obj) {
    QJsonDocument doc(obj);
    QByteArray body = doc.toJson(QJsonDocument::Compact);
    sendText(sock, code, body, "application/json");
}

void AgentBridge::sendText(QTcpSocket* sock, int code, const QByteArray& text,
                           const QByteArray& contentType)
{
    QByteArray reason;
    switch (code) {
        case 200: reason = "OK"; break;
        case 400: reason = "Bad Request"; break;
        case 404: reason = "Not Found"; break;
        case 405: reason = "Method Not Allowed"; break;
        case 500: reason = "Internal Server Error"; break;
        case 502: reason = "Bad Gateway"; break;
        case 503: reason = "Service Unavailable"; break;
        default:  reason = "Unknown"; break;
    }

    QByteArray resp;
    resp.append(QByteArray("HTTP/1.1 ") + QByteArray::number(code) + " " + reason + "\r\n");
    resp.append("Content-Type: " + contentType + "\r\n");
    resp.append("Content-Length: " + QByteArray::number(text.size()) + "\r\n");
    resp.append("Access-Control-Allow-Origin: *\r\n");
    resp.append("Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n");
    resp.append("Access-Control-Allow-Headers: Content-Type, Authorization\r\n");
    resp.append("Connection: close\r\n");
    resp.append("\r\n");
    resp.append(text);

    if (sock) {
        sock->write(resp);
        sock->flush();
    }
}

// ---- 工具 ----

Skill AgentBridge::resolveSkill(const QString& idOrName) {
    bool ok;
    int id = idOrName.toInt(&ok);
    if (ok) {
        Skill s = DaoSkill::skillById(id, m_db);
        if (s.id != 0) return s;
    }
    // 按 canonical_name 或 name 搜
    auto list = DaoSkill::allSkills(m_db);
    for (const auto& s : list) {
        if (s.canonicalName.compare(idOrName, Qt::CaseInsensitive) == 0 ||
            s.name.compare(idOrName, Qt::CaseInsensitive) == 0) {
            return s;
        }
    }
    return Skill{};
}

QString AgentBridge::readSkillContent(const Skill& s) {
    QString content = s.body;

    auto appendFile = [&](const QString& filePath, const QString& label) {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString text = QString::fromUtf8(f.readAll());
            if (!text.trimmed().isEmpty()) {
                if (!content.isEmpty()) content.append("\n\n");
                if (!label.isEmpty()) content.append("## ").append(label).append("\n\n");
                content.append(text);
            }
        }
    };

    QDir dir(s.directory);
    if (dir.exists("chapters")) {
        QDir cdir(dir.filePath("chapters"));
        QStringList names;
        for (const auto& fi : cdir.entryInfoList({"*.md"}, QDir::Files)) names << fi.fileName();
        names.sort();
        for (const auto& n : names) {
            appendFile(cdir.filePath(n), QString("Chapter: %1").arg(n));
        }
    }
    if (dir.exists("glossary.md"))   appendFile(dir.filePath("glossary.md"),   "Glossary");
    if (dir.exists("patterns.md"))   appendFile(dir.filePath("patterns.md"),   "Patterns");
    if (dir.exists("cheatsheet.md")) appendFile(dir.filePath("cheatsheet.md"), "Cheatsheet");

    return content;
}

// -----------------------------------------------------------------------------
// CLI
// -----------------------------------------------------------------------------

int AgentBridge::runCli(const QStringList& args) {
    if (args.size() < 2) {
        qInfo() << "Usage:";
        qInfo() << "  SkillManager list [--category xxx] [--q keyword]";
        qInfo() << "  SkillManager show <id|name>";
        qInfo() << "  SkillManager content <id>";
        qInfo() << "  SkillManager query <skill-id|name> <question...>";
        qInfo() << "  SkillManager translate <english...>";
        qInfo() << "  SkillManager repos";
        qInfo() << "  SkillManager repo-add <url>";
        qInfo() << "  SkillManager repo-check <id>";
        qInfo() << "  SkillManager repo-pull <id>";
        qInfo() << "  SkillManager serve [port=17234]";
        return 1;
    }

    QString cmd = args.at(1);

    if (cmd == "list")       return cliList(args.mid(2));
    if (cmd == "show") {
        if (args.size() < 3) { qWarning() << "Missing id/name"; return 1; }
        return cliShow(args.at(2));
    }
    if (cmd == "content") {
        if (args.size() < 3) { qWarning() << "Missing id"; return 1; }
        return cliContent(args.at(2));
    }
    if (cmd == "query") {
        if (args.size() < 4) { qWarning() << "Usage: query <skill-id|name> <question...>"; return 1; }
        return cliQuery(args.at(2), args.mid(3).join(" "));
    }
    if (cmd == "translate") {
        if (args.size() < 3) { qWarning() << "Usage: translate <english...>"; return 1; }
        return cliTranslate(args.mid(2).join(" "));
    }
    if (cmd == "repos")      return cliRepos();
    if (cmd == "repo-add") {
        if (args.size() < 3) { qWarning() << "Missing url"; return 1; }
        return cliRepoAdd(args.at(2));
    }
    if (cmd == "repo-check") {
        if (args.size() < 3) { qWarning() << "Missing id"; return 1; }
        return cliRepoCheck(args.at(2).toInt());
    }
    if (cmd == "repo-pull") {
        if (args.size() < 3) { qWarning() << "Missing id"; return 1; }
        return cliRepoPull(args.at(2).toInt());
    }

    if (cmd == "serve") {
        quint16 port = 17234;
        if (args.size() >= 3) port = args.at(2).toUShort();
        if (!startHttp(port)) {
            qCritical() << "Failed to start HTTP:" << m_lastError;
            return 1;
        }
        qInfo() << "HTTP listening on 127.0.0.1:" << port;
        // 阻塞直到 Ctrl+C
        return 0;
    }

    qWarning() << "Unknown command:" << cmd;
    return 1;
}

int AgentBridge::cliList(const QStringList& args) {
    QString category, search;
    bool enabledOnly = false;
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--category" && i + 1 < args.size()) category = args[++i];
        else if (args[i] == "--q" && i + 1 < args.size())  search   = args[++i];
        else if (args[i] == "--enabled") enabledOnly = true;
    }
    auto list = DaoSkill::allSkills(m_db, enabledOnly, category, search);
    for (const auto& s : list) {
        QTextStream out(stdout);
        out << s.id << "\t" << s.canonicalName << "\t" << s.category
            << "\t" << (s.enabled ? "on" : "off") << Qt::endl;
    }
    return 0;
}

int AgentBridge::cliShow(const QString& idOrName) {
    Skill s = resolveSkill(idOrName);
    if (s.id == 0) { qWarning() << "Skill not found:" << idOrName; return 1; }
    QTextStream out(stdout);
    out << "id:         " << s.id << Qt::endl;
    out << "name:       " << s.name << Qt::endl;
    out << "canonical:  " << s.canonicalName << Qt::endl;
    out << "category:   " << s.category << Qt::endl;
    out << "directory:  " << s.directory << Qt::endl;
    out << "enabled:    " << (s.enabled ? "yes" : "no") << Qt::endl;
    out << "--- description ---" << Qt::endl;
    out << s.description << Qt::endl;
    out << "--- body (first 500 chars) ---" << Qt::endl;
    out << s.body.left(500) << Qt::endl;
    return 0;
}

int AgentBridge::cliContent(const QString& idOrName) {
    Skill s = resolveSkill(idOrName);
    if (s.id == 0) { qWarning() << "Skill not found:" << idOrName; return 1; }
    QTextStream out(stdout);
    out << readSkillContent(s) << Qt::endl;
    return 0;
}

int AgentBridge::cliQuery(const QString& idOrName, const QString& question) {
    Skill s = resolveSkill(idOrName);
    if (s.id == 0) { qWarning() << "Skill not found:" << idOrName; return 1; }
    if (!m_ai || !m_ai->isConfigured()) {
        qWarning() << "AI client not configured"; return 1;
    }
    QEventLoop loop;
    QString answer, errMsg;
    m_ai->rag(readSkillContent(s), question, [&](const QString& a, const QString& e) {
        answer = a; errMsg = e;
        loop.quit();
    });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (!errMsg.isEmpty()) { qWarning() << "Error:" << errMsg; return 2; }
    QTextStream out(stdout);
    out << answer << Qt::endl;
    return 0;
}

int AgentBridge::cliTranslate(const QString& text) {
    if (!m_ai || !m_ai->isConfigured()) {
        qWarning() << "AI client not configured"; return 1;
    }
    QEventLoop loop;
    QString zh, errMsg;
    m_ai->translate(text, [&](const QString& a, const QString& e) {
        zh = a; errMsg = e;
        loop.quit();
    });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (!errMsg.isEmpty()) { qWarning() << "Error:" << errMsg; return 2; }
    QTextStream out(stdout);
    out << zh << Qt::endl;
    return 0;
}

int AgentBridge::cliRepos() {
    auto list = DaoSkill::allRepos(m_db);
    QTextStream out(stdout);
    for (const auto& r : list) {
        out << r.id << "\t" << r.name << "\t" << r.url
            << "\t" << (r.hasUpdate ? "update" : "ok") << Qt::endl;
    }
    return 0;
}

int AgentBridge::cliRepoAdd(const QString& url) {
    Repo r;
    r.url = url;
    r.name = QFileInfo(url).completeBaseName();
    if (r.name.isEmpty()) r.name = url;
    int id = DaoSkill::insertRepo(r, m_db);
    if (id <= 0) { qWarning() << "insert failed"; return 1; }
    qInfo() << "Added repo id=" << id << "name=" << r.name;
    return 0;
}

int AgentBridge::cliRepoCheck(int id) {
    Repo r = DaoSkill::repoById(id, m_db);
    if (r.id == 0) { qWarning() << "Repo not found:" << id; return 1; }
    qInfo() << "Repo" << r.name << "- has_update:" << r.hasUpdate;
    return 0;
}

int AgentBridge::cliRepoPull(int id) {
    Repo r = DaoSkill::repoById(id, m_db);
    if (r.id == 0) { qWarning() << "Repo not found:" << id; return 1; }
    qInfo() << "Pull placeholder for repo" << r.name;
    return 0;
}
