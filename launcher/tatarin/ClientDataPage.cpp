#include "ClientDataPage.h"
#include "ClientData.h"
#include "BaseInstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopeGuard>
#include <QVBoxLayout>

ClientDataPage::ClientDataPage(BaseInstance* instance) : m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(QStringLiteral("Перенос настроек Minecraft, клиентских данных модов, карт и точек, "
        "списка серверов, ресурспаков, шейдеров, миров и скриншотов из игровой папки другой сборки.\n\n"
        "Закройте Minecraft в обоих лаунчерах. Исходная папка не изменяется. "
        "Обязательные файлы сборки не заменяются, личные файлы назначения сохраняются в резервной копии.\n\n"
        "Резервные копии находятся в client-data-import/backups внутри папки экземпляра."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* destination = new QLabel(QStringLiteral("Назначение: %1").arg(QDir::toNativeSeparators(instance->gameRoot())), this);
    destination->setTextFormat(Qt::PlainText);
    destination->setWordWrap(true);
    destination->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(destination);
    auto* button = new QPushButton(QStringLiteral("Перенести данные из другой сборки…"), this);
    layout->addWidget(button);
    layout->addStretch();
    connect(button, &QPushButton::clicked, this, &ClientDataPage::importData);
}

void ClientDataPage::importData()
{
    if (m_instance->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("Перенос данных"), QStringLiteral("Сначала остановите сборку и дождитесь завершения её задач."));
        return;
    }
    const auto source = QFileDialog::getExistingDirectory(this, QStringLiteral("Игровая папка другой сборки (config и options.txt)"), QDir::homePath());
    if (source.isEmpty())
        return;
    m_instance->setRunning(true);
    const auto runningGuard = qScopeGuard([this] { m_instance->setRunning(false); });
    try {
        Tatarin::recoverClientData(m_instance->instanceRoot(), m_instance->gameRoot());
        const auto plan = Tatarin::planClientData(source, m_instance->instanceRoot(), m_instance->gameRoot());
        if (plan.files.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("Перенос данных"),
                QStringLiteral("Нет подходящих клиентских файлов. Обязательных файлов пропущено: %1; других файлов/папок: %2.")
                    .arg(plan.protectedFiles).arg(plan.omitted));
            return;
        }
        const auto text = QStringLiteral("Будет перенесено файлов: %1 (%2 МиБ).\n"
            "Обязательных файлов сборки пропущено: %3. Других файлов/папок пропущено: %4.\n"
            "Миров с новым именем: %5.\n\nСовпадающие личные файлы будут заменены с резервной копией. "
            "Аккаунты, токены, моды, скрипты и кеши не переносятся.\n\nПродолжить?")
            .arg(plan.files.size()).arg(QString::number(static_cast<double>(plan.bytes) / (1024 * 1024), 'f', 1))
            .arg(plan.protectedFiles).arg(plan.omitted).arg(plan.renamedWorlds);
        if (QMessageBox::question(this, QStringLiteral("Проверка переноса"), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QProgressDialog progress(QStringLiteral("Подготовка клиентских данных…"), QStringLiteral("Отмена"), 0, static_cast<int>(plan.files.size()), this);
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        bool applying = false;
        Tatarin::importClientData(plan, m_instance->instanceRoot(), m_instance->gameRoot(),
            [&] {
                QCoreApplication::processEvents();
                return !applying && progress.wasCanceled();
            },
            [&](qint64 done, qint64) { progress.setValue(static_cast<int>(done)); },
            [&](bool value) {
                applying = value;
                if (value) {
                    progress.setCancelButton(nullptr);
                    progress.setLabelText(QStringLiteral("Применение клиентских данных…"));
                }
            });
        progress.close();
        QMessageBox::information(this, QStringLiteral("Перенос данных"), QStringLiteral("Клиентские данные перенесены. Исходная папка сохранена."));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, QStringLiteral("Перенос не завершён"), QString::fromUtf8(error.what()));
    }
}
