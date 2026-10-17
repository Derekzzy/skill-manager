#pragma once
#include <QDialog>

class QLineEdit;
class QPushButton;
class QComboBox;

// API 设置对话框：deepseek endpoint / api key / model / 主题
class ApiSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ApiSettingsDialog(QWidget* parent = nullptr);

    QString endpoint() const;
    QString apiKey() const;
    QString model() const;
    QString themeId() const;

private:
    QLineEdit* m_endpointEdit;
    QLineEdit* m_apiKeyEdit;
    QLineEdit* m_modelEdit;
    QComboBox* m_themeCombo;
};

// 添加仓库对话框：输入 git 仓库 URL
class RepoAddDialog : public QDialog {
    Q_OBJECT
public:
    explicit RepoAddDialog(QWidget* parent = nullptr);

    QString url() const;

private:
    QLineEdit* m_urlEdit;
};
