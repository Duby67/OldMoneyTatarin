#pragma once

#include "tasks/Task.h"
class BaseInstance;

class ClientDataRecoveryTask : public Task {
    Q_OBJECT
public:
    explicit ClientDataRecoveryTask(BaseInstance* instance) : m_instance(instance) {}
protected:
    void executeTask() override;
private:
    BaseInstance* m_instance;
};
