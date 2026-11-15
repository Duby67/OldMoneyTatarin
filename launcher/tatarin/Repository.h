#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <functional>

namespace Tatarin {
using Cancel = std::function<bool()>;
using Progress = std::function<void(qint64, qint64)>;

// На этапе локальной разработки доверяем только явному IPv4 loopback.
QUrl sourceUrl(const QString& text);
QJsonObject catalog(QNetworkAccessManager* network, const QUrl& source, Cancel cancel = {});
QJsonObject manifest(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& pack, Cancel cancel = {});
void validateManifest(const QJsonObject& value);
QString checkedPath(const QString& root, const QString& relative);
QString fileHash(const QString& path);
QJsonObject readJson(const QString& path);
void writeJson(const QString& path, const QJsonObject& value);
void install(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& value,
             const QString& instanceRoot, const QString& gameRoot, Cancel cancel, Progress progress);
}  // namespace Tatarin
