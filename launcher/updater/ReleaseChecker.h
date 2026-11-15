#pragma once

#include "ExternalUpdater.h"
#include <QTimer>

class QWidget;
class QNetworkReply;

class ReleaseChecker : public ExternalUpdater {
    Q_OBJECT
public:
    explicit ReleaseChecker(QWidget* parent);
    void checkForUpdates() override;
    bool getAutomaticallyChecksForUpdates() override;
    double getUpdateCheckInterval() override;
    bool getBetaAllowed() override { return false; }
    void setAutomaticallyChecksForUpdates(bool check) override;
    void setUpdateCheckInterval(double seconds) override;
    void setBetaAllowed(bool) override {}
signals:
    void checkResult(const QString& message);
private:
    void check(bool manual);
    void schedule();
    void result(const QString& message, bool showDialog);
    QWidget* m_parent;
    QTimer m_timer;
    bool m_busy = false;
};
