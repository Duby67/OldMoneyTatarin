#pragma once

#include "tasks/Task.h"
class MinecraftInstance;

namespace Tatarin {
class SyncTask : public Task {
    Q_OBJECT
public:
    explicit SyncTask(MinecraftInstance* instance) : m_instance(instance) {}
    bool abort() override;
protected:
    void executeTask() override;
private:
    MinecraftInstance* m_instance;
    bool m_cancelled = false;
};
}
