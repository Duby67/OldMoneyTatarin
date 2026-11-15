#include "CatalogPage.h"
#include "CreationTask.h"
#include "Repository.h"
#include "Application.h"
#include "ui/dialogs/NewInstanceDialog.h"

#include <QLabel>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace Tatarin {
CatalogPage::CatalogPage(NewInstanceDialog* dialog) : m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Локальный каталог сборок TATARIN"), this));
    m_directory = new QLabel(this);
    m_directory->setTextFormat(Qt::PlainText);
    m_directory->setWordWrap(true);
    m_directory->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_directory);
    auto* directoryButton = new QPushButton(QStringLiteral("Настроить папку сборок…"), this);
    layout->addWidget(directoryButton);
    connect(directoryButton, &QPushButton::clicked, this, [this] {
        APPLICATION->ShowGlobalSettings(m_dialog, "launcher-settings");
        m_directory->setText(QStringLiteral("Папка сборок: %1\nДля каждой сборки здесь создаётся отдельный каталог.")
                                 .arg(QDir::toNativeSeparators(QDir(APPLICATION->settings()->get("InstanceDir").toString()).absolutePath())));
    });
    m_source = new QLineEdit(APPLICATION->settings()->get("TatarinDevSource").toString(), this);
    layout->addWidget(m_source);
    auto* refreshButton = new QPushButton(QStringLiteral("Обновить каталог"), this);
    layout->addWidget(refreshButton);
    m_list = new QListWidget(this);
    layout->addWidget(m_list);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    connect(refreshButton, &QPushButton::clicked, this, &CatalogPage::refresh);
    connect(m_list, &QListWidget::currentRowChanged, this, &CatalogPage::select);
    connect(m_source, &QLineEdit::textChanged, this, [this] {
        m_packs = {};
        m_list->clear();
        if (isOpened)
            m_dialog->setSuggestedPack();
    });
}

void CatalogPage::openedImpl()
{
    m_directory->setText(QStringLiteral("Папка сборок: %1\nДля каждой сборки здесь создаётся отдельный каталог.")
                             .arg(QDir::toNativeSeparators(QDir(APPLICATION->settings()->get("InstanceDir").toString()).absolutePath())));
    if (m_packs.isEmpty())
        refresh();
    else
        select();
}

void CatalogPage::refresh()
{
    m_dialog->setSuggestedPack();
    m_packs = {};
    m_list->clear();
    // Запрос обрабатывает события Qt; блокируем смену страницы и закрытие окна до ответа.
    m_dialog->setEnabled(false);
    try {
        auto source = sourceUrl(m_source->text());
        m_packs = catalog(APPLICATION->network(), source)["packs"].toArray();
        APPLICATION->settings()->set("TatarinDevSource", source.toString());
        for (const auto& item : m_packs) {
            auto pack = item.toObject();
            m_list->addItem(pack["name"].toString() + " — " + pack["release"].toString());
        }
        m_status->setText(m_packs.isEmpty() ? QStringLiteral("В каталоге нет сборок") : QStringLiteral("Выберите сборку для установки"));
    } catch (const std::exception& error) {
        m_status->setText(QString::fromUtf8(error.what()));
    }
    m_dialog->setEnabled(true);
}

void CatalogPage::select()
{
    const auto row = m_list->currentRow();
    if (!isOpened)
        return;
    if (row < 0 || row >= m_packs.size()) {
        m_dialog->setSuggestedPack();
        return;
    }
    auto pack = m_packs[row].toObject();
    m_status->setText(pack["description"].toString());
    m_dialog->setSuggestedPack(pack["name"].toString(), new CreationTask(sourceUrl(m_source->text()), pack));
}
}  // namespace Tatarin
