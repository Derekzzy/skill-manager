#include "dialogs.h"
#include "main_window.h"
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QSettings>
#include <QLabel>
#include <QComboBox>

// ============ ApiSettingsDialog ============

ApiSettingsDialog::ApiSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QObject::tr("API 设置"));
    setMinimumWidth(460);

    QSettings s("SkillManager", "SkillManager");
    QString savedEndpoint = s.value("deepseek/endpoint", "https://api.deepseek.com/chat/completions").toString();
    QString savedApiKey   = s.value("deepseek/apiKey", QString()).toString();
    QString savedModel    = s.value("deepseek/model", "deepseek-chat").toString();
    QString savedTheme    = s.value("ui/theme", "obsidian").toString();

    m_endpointEdit = new QLineEdit(savedEndpoint, this);
    m_apiKeyEdit   = new QLineEdit(savedApiKey, this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_modelEdit    = new QLineEdit(savedModel, this);

    // 主题下拉
    m_themeCombo = new QComboBox(this);
    QStringList ids = MainWindow::availableThemes();
    for (const QString& id : ids) {
        m_themeCombo->addItem(MainWindow::themeDisplayName(id), id);
    }
    int idx = m_themeCombo->findData(savedTheme);
    if (idx >= 0) m_themeCombo->setCurrentIndex(idx);

    auto* form = new QFormLayout();
    form->addRow(QObject::tr("Endpoint:"), m_endpointEdit);
    form->addRow(QObject::tr("API Key:"),  m_apiKeyEdit);
    form->addRow(QObject::tr("Model:"),    m_modelEdit);
    form->addRow(QObject::tr("界面主题:"), m_themeCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        QSettings s("SkillManager", "SkillManager");
        s.setValue("deepseek/endpoint", m_endpointEdit->text().trimmed());
        s.setValue("deepseek/apiKey",   m_apiKeyEdit->text());
        s.setValue("deepseek/model",    m_modelEdit->text().trimmed());
        QString themeId = m_themeCombo->currentData().toString();
        s.setValue("ui/theme", themeId);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
}

QString ApiSettingsDialog::endpoint() const { return m_endpointEdit->text().trimmed(); }
QString ApiSettingsDialog::apiKey()   const { return m_apiKeyEdit->text(); }
QString ApiSettingsDialog::model()    const { return m_modelEdit->text().trimmed(); }
QString ApiSettingsDialog::themeId()  const { return m_themeCombo->currentData().toString(); }

// ============ RepoAddDialog ============

RepoAddDialog::RepoAddDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QObject::tr("添加仓库"));
    setMinimumWidth(420);

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText("https://github.com/org/repo.git");

    auto* form = new QFormLayout();
    form->addRow(QObject::tr("仓库 URL:"), m_urlEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_urlEdit->text().trimmed().isEmpty()) return;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
}

QString RepoAddDialog::url() const { return m_urlEdit->text().trimmed(); }
