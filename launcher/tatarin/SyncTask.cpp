#include "SyncTask.h"
#include "Repository.h"
#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/Component.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QMessageBox>

namespace Tatarin {
bool SyncTask::abort()
{
    if (!canAbort())
        return false;
    m_cancelled = true;
    return true;
}

void SyncTask::executeTask()
{
    setAbortable(true);
    setStatus(QStringLiteral("Проверка сборки TATARIN"));
    try {
        QJsonObject value;
        try {
            const auto source = sourceUrl(m_instance->settings()->get("ManagedPackURL").toString());
            value = synchronize(APPLICATION->network(), source, m_instance->getManagedPackID(),
                                m_instance->instanceRoot(), m_instance->gameRoot(), [this] { return m_cancelled; },
                                [this](qint64 current, qint64 total) { setProgress(current, total); },
                                [this](bool applying) {
                                    setAbortable(!applying);
                                    setStatus(applying ? QStringLiteral("Применение или восстановление TATARIN") :
                                                        QStringLiteral("Проверка и загрузка файлов TATARIN"));
                                });
        } catch (const NetworkError&) {
            if (m_cancelled)
                throw;
            setAbortable(false);
            value = verifiedInstalled(m_instance->instanceRoot(), m_instance->gameRoot(), m_instance->getManagedPackID());
            if (QMessageBox::question(nullptr, QStringLiteral("Источник TATARIN недоступен"),
                                      QStringLiteral("Актуальность сборки не проверена. Запустить проверенную локальную версию %1?")
                                          .arg(value["release"].toString()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
                emitAborted();
                return;
            }
        }
        setAbortable(false);
        const auto extras = additionalMods(m_instance->gameRoot(), value);
        if (!extras.isEmpty())
            logWarning(QStringLiteral("TATARIN: сохранено дополнительных модов: %1. Они могут влиять на совместимость; список доступен на странице TATARIN экземпляра.")
                           .arg(extras.size()));
        auto* profile = m_instance->getPackProfile();
        // Загружаем список компонентов до изменения. Пересчёт здесь только локальный:
        // setComponentVersion(..., true) немедленно запустил бы Online-resolve
        // между изменениями Minecraft и NeoForge, в том числе при запуске без источника.
        if (const auto loaded = profile->reload(Net::Mode::Offline); !loaded)
            throw std::runtime_error(loaded.error.toUtf8().constData());
        if (auto task = profile->getCurrentTask(); task && !task->isFinished()) {
            QEventLoop loop;
            connect(task.get(), &Task::finished, &loop, &QEventLoop::quit);
            if (!task->isFinished())
                loop.exec();
        }
        const QMap<QString, QString> versions = {{"net.minecraft", value["minecraft"].toString()},
                                                 {"net.neoforged", value["neoforge"].toString()}};
        for (auto it = versions.cbegin(); it != versions.cend(); ++it) {
            if (profile->getComponentVersion(it.key()) != it.value() && !profile->setComponentVersion(it.key(), it.value()))
                throw std::runtime_error("Не удалось изменить версию компонента TATARIN");
            const auto component = profile->getComponent(it.key());
            if (!component)
                throw std::runtime_error("Не найден компонент TATARIN");
            component->setImportant(true);
            component->setEnabled(true);
        }
        m_instance->setManagedPack("tatarin", value["id"].toString(), value["name"].toString(),
                                   value["release"].toString(), value["release"].toString());
        m_instance->saveNow();
        // saveNow() не возвращает ошибку записи; не запускаем игру с прежними версиями.
        const auto saved = readJson(checkedPath(m_instance->instanceRoot(), "mmc-pack.json"));
        for (auto it = versions.cbegin(); it != versions.cend(); ++it) {
            bool found = false;
            for (const auto& item : saved["components"].toArray()) {
                const auto component = item.toObject();
                if (component["uid"].toString() == it.key() && component["version"].toString() == it.value() &&
                    component["important"].toBool() && !component["disabled"].toBool())
                    found = true;
            }
            if (!found)
                throw std::runtime_error("Не удалось сохранить версии компонентов TATARIN");
        }
        m_instance->updateRuntimeContext();
        emitSucceeded();
    } catch (const std::exception& error) {
        if (m_cancelled)
            emitAborted();
        else
            emitFailed(QString::fromUtf8(error.what()));
    }
}
}  // namespace Tatarin
