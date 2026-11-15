#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>
#include <functional>
#include <stdexcept>

namespace Tatarin {
using Cancel = std::function<bool()>;
using Progress = std::function<void(qint64, qint64)>;
class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

QString defaultSource();
// HTTPS в публичной раздаче; HTTP разрешён только для локальных тестов.
QUrl sourceUrl(const QString& text);
QJsonObject catalog(QNetworkAccessManager* network, const QUrl& source, Cancel cancel = {});
QJsonObject manifest(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& pack, Cancel cancel = {});
void validateManifest(const QJsonObject& value);
QString checkedPath(const QString& root, const QString& relative);
QString fileHash(const QString& path);
QJsonObject readJson(const QString& path);
void writeJson(const QString& path, const QJsonObject& value);
void downloadObject(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& entry,
                    const QString& destination, Cancel cancel);
void install(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& value,
             const QString& instanceRoot, const QString& gameRoot, Cancel cancel, Progress progress);
QJsonObject synchronize(QNetworkAccessManager* network, const QUrl& source, const QString& packId,
                        const QString& instanceRoot, const QString& gameRoot, Cancel cancel, Progress progress,
                        std::function<void(bool)> applying = {});
QJsonObject verifiedInstalled(const QString& instanceRoot, const QString& gameRoot, const QString& packId);
QStringList additionalMods(const QString& gameRoot, const QJsonObject& value);
}  // namespace Tatarin
