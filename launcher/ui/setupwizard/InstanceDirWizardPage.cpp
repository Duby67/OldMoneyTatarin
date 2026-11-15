#include "InstanceDirWizardPage.h"

#include "Application.h"
#include "InstanceList.h"
#include "settings/SettingsObject.h"

#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryFile>
#include <QVBoxLayout>

InstanceDirWizardPage::InstanceDirWizardPage(QWidget* parent) : BaseWizardPage(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(QStringLiteral("Выберите общий каталог сборок:"), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_directory = new QLineEdit(QDir::toNativeSeparators(QDir(APPLICATION->settings()->get("InstanceDir").toString()).absolutePath()), this);
    layout->addWidget(m_directory);
    auto* browse = new QPushButton(QStringLiteral("Выбрать папку…"), this);
    layout->addWidget(browse);
    layout->addStretch();
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка сборок"), m_directory->text());
        if (!path.isEmpty())
            m_directory->setText(QDir::toNativeSeparators(path));
    });
    retranslate();
}

void InstanceDirWizardPage::retranslate()
{
    setTitle(QStringLiteral("Папка сборок"));
    setSubTitle(QStringLiteral("Сборки могут храниться отдельно от программы"));
}

bool InstanceDirWizardPage::validatePage()
{
    auto path = QDir::fromNativeSeparators(m_directory->text().trimmed());
    if (!QDir::isAbsolutePath(path) || QDir(path).isRoot() ||
        QDir(path).absolutePath().compare(QDir::currentPath(), Qt::CaseInsensitive) == 0 || path.contains('!') ||
        !QDir().mkpath(path)) {
        QMessageBox::warning(this, QStringLiteral("Папка сборок"),
                             QStringLiteral("Укажите доступный полный путь к отдельной папке сборок без символа !."));
        return false;
    }
    QTemporaryFile probe(QDir(path).filePath(".oldmoney-write-XXXXXX"));
    if (!probe.open()) {
        QMessageBox::warning(this, QStringLiteral("Папка сборок"), QStringLiteral("В выбранную папку нельзя записывать файлы."));
        return false;
    }
    probe.close();
    auto* settings = APPLICATION->settings();
    settings->set("InstanceDir", QDir(path).absolutePath());
    settings->set("InstanceDirConfigured", true);
    APPLICATION->instances()->loadList();
    return true;
}
