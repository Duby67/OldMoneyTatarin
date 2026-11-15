#include "MemoryPolicy.h"
#include <stdexcept>

namespace Tatarin {
std::optional<MemoryRequirements> memoryRequirements(const QJsonObject& manifest)
{
    if (!manifest.contains("memory"))
        return std::nullopt;
    if (!manifest["memory"].isObject())
        throw std::runtime_error("Требования к памяти должны быть объектом");
    const auto object = manifest["memory"].toObject();
    auto number = [&](const QString& key, int fallback = -1) {
        if (!object.contains(key))
            return fallback;
        const auto value = object[key].toInteger(-1);
        if (value < 0 || value > 262144 || object[key].toDouble(-1) != static_cast<double>(value))
            throw std::runtime_error("Некорректное требование к памяти TATARIN");
        return static_cast<int>(value);
    };
    MemoryRequirements memory{number("minMiB"), number("maxMiB"), number("systemReserveMiB", 2048)};
    if (memory.minimumMiB < 256 || memory.maximumMiB < memory.minimumMiB || memory.systemReserveMiB < 1024)
        throw std::runtime_error("Некорректный диапазон памяти TATARIN");
    return memory;
}

QString memoryWarning(const MemoryRequirements& memory, quint64 totalMiB, quint64 availableMiB)
{
    auto gib = [](quint64 mib) { return QString::number(static_cast<double>(mib) / 1024.0, 'g', 4); };
    const auto required = static_cast<quint64>(memory.minimumMiB);
    const auto reserve = static_cast<quint64>(memory.systemReserveMiB);
    if (totalMiB && availableMiB >= static_cast<quint64>(memory.maximumMiB) && totalMiB >= static_cast<quint64>(memory.maximumMiB) + reserve)
        return {};
    QString message = QStringLiteral("Сборке требуется минимум %1 ГБ для Java (%2 МиБ). Максимум Java: %3 ГБ.\n")
                          .arg(gib(required)).arg(memory.minimumMiB).arg(gib(static_cast<quint64>(memory.maximumMiB)));
    if (!totalMiB || !availableMiB)
        message += QStringLiteral("Не удалось достоверно определить доступную оперативную память.\n");
    else
        message += QStringLiteral("В системе всего %1 ГБ; сейчас доступно %2 ГБ.\n").arg(gib(totalMiB), gib(availableMiB));
    message += QStringLiteral("Кроме Java, для Windows и других программ предусмотрен резерв %1 ГБ.\n\n"
                              "Закройте лишние программы. Продолжить запуск с параметрами сборки? Это может вызвать зависание или сбой.")
                   .arg(gib(reserve));
    return message;
}
}
