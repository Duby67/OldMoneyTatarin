#include "ReleaseQuery.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <algorithm>
#include <exception>

ReleaseQuery::ReleaseQuery(QNetworkAccessManager* network, QObject* parent) : QObject(parent), m_network(network) {}
ReleaseQuery::~ReleaseQuery()
{
    if (m_reply) {
        disconnect(m_reply.data(), nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void ReleaseQuery::start(const QUrl& url, int timeoutMs)
{
    if (m_reply)
        return;
    try {
        LauncherUpdate::source(url.toString(), {});
    } catch (const std::exception& error) {
        emit failed(QString::fromUtf8(error.what()));
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OldMoneyTatarin-update-check"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(std::max(1, timeoutMs));
    auto* reply = m_network->get(request);
    m_reply = reply;
    m_bytes.clear();
    m_oversized = false;
    reply->setReadBufferSize(256 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        m_bytes.append(reply->readAll());
        if (m_bytes.size() > 2 * 1024 * 1024) {
            m_oversized = true;
            reply->abort();
        }
    });
    QTimer::singleShot(std::max(1, timeoutMs), reply, [reply] {
        if (!reply->isFinished())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_bytes.append(reply->readAll());
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->error();
        m_reply = nullptr;
        reply->deleteLater();
        if (m_oversized || m_bytes.size() > 2 * 1024 * 1024) {
            emit failed(QStringLiteral("Слишком большой ответ источника обновлений."));
            return;
        }
        if (status == 404) {
            emit failed(QStringLiteral("Опубликованный стабильный выпуск не найден. Репозиторий может быть недоступен; актуальность версии не подтверждена."));
            return;
        }
        if (networkError != QNetworkReply::NoError || status != 200) {
            emit failed(QStringLiteral("Источник недоступен или вернул HTTP %1. Актуальность версии не подтверждена.").arg(status));
            return;
        }
        try {
            const auto info = LauncherUpdate::parse(m_bytes);
            emit releaseReady(info);
        } catch (const std::exception& error) {
            emit failed(QString::fromUtf8(error.what()));
        }
    });
}
