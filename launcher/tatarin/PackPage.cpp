#include "PackPage.h"
#include "Repository.h"
#include "MemoryPolicy.h"
#include "BaseInstance.h"

#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace Tatarin {
PackPage::PackPage(BaseInstance* instance) : m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    layout->addWidget(m_text);
}

void PackPage::openedImpl()
{
    try {
        const auto value = readJson(checkedPath(m_instance->instanceRoot(), "tatarin/installed.json"));
        validateManifest(value);
        const auto extras = additionalMods(m_instance->gameRoot(), value);
        QString text = QStringLiteral("%1\nУстановленный выпуск: %2\nMinecraft: %3\nNeoForge: %4\n\n")
                           .arg(value["name"].toString(), value["release"].toString(), value["minecraft"].toString(), value["neoforge"].toString());
        if (const auto memory = memoryRequirements(value))
            text += QStringLiteral("Память Java задаётся сборкой: минимум %1 МиБ, максимум %2 МиБ.\n"
                                   "Для системы дополнительно предусмотрено %3 МиБ.\n\n")
                        .arg(memory->minimumMiB).arg(memory->maximumMiB).arg(memory->systemReserveMiB);
        text += QStringLiteral("Перед запуском проверяются обновления и целостность управляемых файлов.\n"
                               "Обязательные конфиги заменяются с резервной копией. Стартовые файлы выдаются один раз.\n"
                               "Миры, настройки управления и личные файлы сохраняются. Дополнительные моды не удаляются.\n"
                               "Копии заменённых файлов: tatarin/backups/ внутри папки экземпляра.\n\n");
        text += QStringLiteral("Патчноут установленного выпуска:\n%1\n\n").arg(value["notes"].toString());
        text += extras.isEmpty() ? QStringLiteral("Дополнительных модов не обнаружено.") :
                                  QStringLiteral("Внимание: дополнительные моды могут влиять на совместимость:\n%1").arg(extras.join('\n'));
        m_text->setPlainText(text);
    } catch (const std::exception& error) {
        m_text->setPlainText(QString::fromUtf8(error.what()));
    }
}
}  // namespace Tatarin
