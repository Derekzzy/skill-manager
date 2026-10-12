#include "deepseek_client.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

DeepSeekClient::DeepSeekClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    loadFromSettings();
}

void DeepSeekClient::setApiKey(const QString& key) {
    m_apiKey = key.trimmed();
    saveToSettings();
}

void DeepSeekClient::setModel(const QString& model) {
    m_model = model.trimmed();
    if (m_model.isEmpty()) m_model = "deepseek-chat";
    saveToSettings();
}

void DeepSeekClient::setEndpoint(const QString& url) {
    m_endpoint = url.trimmed();
    if (m_endpoint.isEmpty()) m_endpoint = "https://api.deepseek.com/chat/completions";
    saveToSettings();
}

void DeepSeekClient::loadFromSettings() {
    QSettings s;
    m_apiKey    = s.value("api/deepseek_key", QString()).toString();
    m_model     = s.value("api/deepseek_model", "deepseek-chat").toString();
    m_endpoint  = s.value("api/deepseek_endpoint",
                          "https://api.deepseek.com/chat/completions").toString();
}

void DeepSeekClient::saveToSettings() {
    QSettings s;
    s.setValue("api/deepseek_key",      m_apiKey);
    s.setValue("api/deepseek_model",     m_model);
    s.setValue("api/deepseek_endpoint",  m_endpoint);
}

void DeepSeekClient::chat(const QString& systemPrompt,
                          const QString& userPrompt,
                          std::function<void(const QString&, const QString&)> cb)
{
    QJsonArray msgs;
    msgs.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
    msgs.append(QJsonObject{{"role", "user"},   {"content", userPrompt}});
    chatMessages(msgs, cb);
}

void DeepSeekClient::chatMessages(const QJsonArray& messages,
                                  std::function<void(const QString&, const QString&)> cb)
{
    doRequest(messages, cb);
}

void DeepSeekClient::rag(const QString& skillContent,
                         const QString& question,
                         std::function<void(const QString&, const QString&)> cb)
{
    QString systemPrompt =
        "你是一个知识问答助手。请严格基于以下提供的知识内容回答用户问题。"
        "如果知识里没有答案，就明确说不知道。\n\n"
        "---知识内容---\n" + skillContent + "\n---知识内容结束---";
    chat(systemPrompt, question, cb);
}

void DeepSeekClient::translate(const QString& english,
                               std::function<void(const QString&, const QString&)> cb)
{
    QString systemPrompt = "你是一名专业翻译。把用户的英文翻译成简体中文。只输出译文，不要额外解释。";
    chat(systemPrompt, english, cb);
}

void DeepSeekClient::doRequest(const QJsonArray& messages,
                               std::function<void(const QString&, const QString&)> cb)
{
    if (m_apiKey.isEmpty()) {
        QString err = "API Key 未配置，请先在设置中填写 DeepSeek API Key";
        emit errorOccurred(err);
        if (cb) cb(QString(), err);
        return;
    }

    QJsonObject bodyObj;
    bodyObj["model"]       = m_model;
    bodyObj["messages"]    = messages;
    bodyObj["temperature"] = 0.3;

    QJsonDocument doc(bodyObj);
    QByteArray body = doc.toJson(QJsonDocument::Compact);

    QUrl url(m_endpoint);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());

    QNetworkReply* reply = m_nam->post(req, body);
    reply->setParent(this);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        reply->deleteLater();

        // 网络层错误
        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            emit errorOccurred(err);
            if (cb) cb(QString(), err);
            return;
        }

        // HTTP 状态码
        int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray raw = reply->readAll();

        QJsonParseError jerr;
        QJsonDocument resp = QJsonDocument::fromJson(raw, &jerr);
        if (jerr.error != QJsonParseError::NoError) {
            QString err = QString("JSON 解析失败: %1").arg(jerr.errorString());
            emit errorOccurred(err);
            if (cb) cb(QString(), err);
            return;
        }

        QJsonObject root = resp.object();

        // API 返回 error 字段
        if (root.contains("error")) {
            QJsonObject e = root.value("error").toObject();
            QString errMsg = e.value("message").toString();
            if (errMsg.isEmpty()) errMsg = QString("API HTTP %1").arg(code);
            emit errorOccurred(errMsg);
            if (cb) cb(QString(), errMsg);
            return;
        }

        // 提取 choices[0].message.content
        QString content;
        if (root.contains("choices")) {
            QJsonArray choices = root.value("choices").toArray();
            if (!choices.isEmpty()) {
                QJsonObject msg = choices.at(0).toObject().value("message").toObject();
                content = msg.value("content").toString();
            }
        }

        emit replyReady(content);
        if (cb) cb(content, QString());
    });
}
