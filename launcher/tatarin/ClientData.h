#pragma once

#include "Repository.h"
#include <QVector>

namespace Tatarin {
struct ClientDataFile {
    QString source;
    QString target;
    qint64 size;
    qint64 modifiedMs;
};
struct ClientDataPlan {
    QString sourceRoot;
    QVector<ClientDataFile> files;
    QStringList newWorlds;
    QStringList sourceWorlds;
    qint64 bytes = 0;
    int protectedFiles = 0;
    int omitted = 0;
    int renamedWorlds = 0;
};
ClientDataPlan planClientData(const QString& source, const QString& instanceRoot, const QString& gameRoot);
void importClientData(const ClientDataPlan& plan, const QString& instanceRoot, const QString& gameRoot,
                      Cancel cancel = {}, Progress progress = {}, std::function<void(bool)> applying = {});
// lockHeld используется только задачами, уже владеющими tatarin/sync.lock.
void recoverClientData(const QString& instanceRoot, const QString& gameRoot, bool lockHeld = false);
}
