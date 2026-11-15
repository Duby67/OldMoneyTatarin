#include "MemoryPolicy.h"
#include "BaseInstance.h"
#include "settings/SettingsObject.h"
#include <QJsonDocument>

namespace Tatarin {
void applyMemorySettings(BaseInstance* instance, const QJsonObject& manifest)
{
    auto* settings = instance->settings();
    const auto memory = memoryRequirements(manifest);
    if (!memory) {
        if (settings->get("TatarinMemoryManaged").toBool()) {
            const auto previous = QJsonDocument::fromJson(settings->get("TatarinMemoryBackup").toString().toUtf8()).object();
            settings->set("OverrideMemory", previous["override"].toBool());
            if (!previous.isEmpty()) {
                settings->set("MinMemAlloc", previous["min"].toInt());
                settings->set("MaxMemAlloc", previous["max"].toInt());
                settings->set("LowMemWarning", previous["warning"].toBool(true));
            }
            settings->set("TatarinMemoryManaged", false);
        }
        return;
    }
    if (!settings->get("TatarinMemoryManaged").toBool()) {
        QJsonObject previous{{"override", settings->get("OverrideMemory").toBool()},
                              {"min", settings->get("MinMemAlloc").toInt()}, {"max", settings->get("MaxMemAlloc").toInt()},
                              {"warning", settings->get("LowMemWarning").toBool()}};
        settings->set("TatarinMemoryBackup", QString::fromUtf8(QJsonDocument(previous).toJson(QJsonDocument::Compact)));
    }
    settings->set("TatarinMemoryManaged", true);
    settings->set("TatarinMinMemory", memory->minimumMiB);
    settings->set("TatarinMaxMemory", memory->maximumMiB);
    settings->set("TatarinSystemReserve", memory->systemReserveMiB);
    settings->set("OverrideMemory", true);
    settings->set("MinMemAlloc", memory->minimumMiB);
    settings->set("MaxMemAlloc", memory->maximumMiB);
    settings->set("LowMemWarning", true);
}
}
