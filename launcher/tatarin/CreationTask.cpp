#include "CreationTask.h"
#include "Repository.h"
#include "Application.h"
#include "minecraft/PackProfile.h"
#include "settings/INISettingsObject.h"

namespace Tatarin {
bool CreationTask::abort()
{
    if (!canAbort())
        return false;
    m_abort = true;
    if (m_creating)
        return true;
    return InstanceCreationTask::abort();
}

std::unique_ptr<MinecraftInstance> CreationTask::createInstance()
{
    try {
        m_creating = true;
        setStatus(QStringLiteral("Установка TATARIN"));
        auto value = manifest(APPLICATION->network(), m_source, m_pack, [this] { return m_abort; });
        auto settings = std::make_unique<INISettingsObject>(m_stagingPath + "/instance.cfg");
        auto instance = std::make_unique<MinecraftInstance>(m_globalSettings, std::move(settings), m_stagingPath);
        auto* profile = instance->getPackProfile();
        profile->buildingFromScratch();
        profile->setComponentVersion("net.minecraft", value["minecraft"].toString(), true);
        profile->setComponentVersion("net.neoforged", value["neoforge"].toString());
        install(APPLICATION->network(), m_source, value, m_stagingPath, instance->gameRoot(),
                [this] { return m_abort; }, [this](qint64 current, qint64 total) { setProgress(current, total); });
        instance->setManagedPack("tatarin", value["id"].toString(), value["name"].toString(),
                                 value["release"].toString(), value["release"].toString());
        instance->settings()->set("ManagedPackURL", m_source.toString());
        instance->setName(name());
        instance->setIconKey(m_instIcon);
        instance->saveNow();
        m_creating = false;
        return instance;
    } catch (const std::exception& error) {
        m_creating = false;
        if (m_abort)
            emitAborted();
        else
            setError(QString::fromUtf8(error.what()));
        return nullptr;
    }
}
}  // namespace Tatarin
