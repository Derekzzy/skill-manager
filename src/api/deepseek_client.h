#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QJsonArray>
#include <functional>

class DeepSeekClient : public QObject {
    Q_OBJECT
public:
    explicit DeepSeekClient(QObject* parent = nullptr);

    void setApiKey(const QString& key);
    void setModel(const QString& model);
    void setEndpoint(const QString& url);

    QString model() const { return m_model; }
    QString endpoint() const { return m_endpoint; }
    bool isConfigured() const { return !m_apiKey.isEmpty(); }

    void chat(const QString& systemPrompt,
              const QString& userPrompt,
              std::function<void(const QString& answer, const QString& err)> cb);

    void chatMessages(const QJsonArray& messages,
                      std::function<void(const QString& answer, const QString& err)> cb);

    void rag(const QString& skillContent,
             const QString& question,
             std::function<void(const QString& answer, const QString& err)> cb);

    void translate(const QString& english,
                   std::function<void(const QString& zh, const QString& err)> cb);

signals:
    void replyReady(const QString& text);
    void errorOccurred(const QString& error);

private:
    QNetworkAccessManager* m_nam;
    QString m_apiKey;
    QString m_model = "deepseek-chat";
    QString m_endpoint = "https://api.deepseek.com/chat/completions";

    void doRequest(const QJsonArray& messages,
                   std::function<void(const QString&, const QString&)> cb);

    void loadFromSettings();
    void saveToSettings();
};
