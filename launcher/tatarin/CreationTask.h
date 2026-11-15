#pragma once

#include "InstanceCreationTask.h"
#include <QJsonObject>
#include <QUrl>

namespace Tatarin {
class CreationTask : public InstanceCreationTask {
    Q_OBJECT
public:
    CreationTask(QUrl source, QJsonObject pack) : m_source(std::move(source)), m_pack(std::move(pack)) {}
    std::unique_ptr<MinecraftInstance> createInstance() override;
    bool abort() override;
private:
    QUrl m_source;
    QJsonObject m_pack;
    bool m_creating = false;
};
}
