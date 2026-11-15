#include "SyncTask.h"
#include "Repository.h"
#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"

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
        auto* profile = m_instance->getPackProfile();
        profile->setComponentVersion("net.minecraft", value["minecraft"].toString(), true);
        profile->setComponentVersion("net.neoforged", value["neoforge"].toString());
        m_instance->setManagedPack("tatarin", value["id"].toString(), value["name"].toString(),
                                   value["release"].toString(), value["release"].toString());
        m_instance->saveNow();
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
