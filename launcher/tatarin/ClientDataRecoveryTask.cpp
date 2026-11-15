#include "ClientDataRecoveryTask.h"
#include "ClientData.h"
#include "BaseInstance.h"

void ClientDataRecoveryTask::executeTask()
{
    setStatus(QStringLiteral("Проверка завершения переноса клиентских данных"));
    try {
        Tatarin::recoverClientData(m_instance->instanceRoot(), m_instance->gameRoot());
        emitSucceeded();
    } catch (const std::exception& error) {
        emitFailed(QString::fromUtf8(error.what()));
    }
}
