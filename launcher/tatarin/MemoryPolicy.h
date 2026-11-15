#pragma once

#include <QJsonObject>
#include <QString>
#include <optional>
class BaseInstance;

namespace Tatarin {
struct MemoryRequirements {
    int minimumMiB;
    int maximumMiB;
    int systemReserveMiB;
};
std::optional<MemoryRequirements> memoryRequirements(const QJsonObject& manifest);
QString memoryWarning(const MemoryRequirements& memory, quint64 totalMiB, quint64 availableMiB);
void applyMemorySettings(BaseInstance* instance, const QJsonObject& manifest);
}
