#pragma once

#include "ReleaseInfo.h"
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

class ReleaseQuery : public QObject {
    Q_OBJECT
public:
    explicit ReleaseQuery(QNetworkAccessManager* network, QObject* parent = nullptr);
    ~ReleaseQuery() override;
    void start(const QUrl& url, int timeoutMs = 20000);
signals:
    void releaseReady(const LauncherUpdate::ReleaseInfo& release);
    void failed(const QString& message);
private:
    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_bytes;
    bool m_oversized = false;
};
