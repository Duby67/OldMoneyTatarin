#include "ReleaseInfo.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <stdexcept>

namespace LauncherUpdate {
namespace {
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }
bool allowed(const QUrl& url)
{
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
           (url.scheme() == "https" || (url.scheme() == "http" && url.host() == "127.0.0.1"));
}
}

QVersionNumber version(const QString& text)
{
    if (!QRegularExpression("^v?(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})$").match(text).hasMatch())
        fail(QStringLiteral("Некорректная версия выпуска; ожидается vX.Y.Z"));
    auto result = QVersionNumber::fromString(text.startsWith('v') ? text.mid(1) : text);
    if (result.segmentCount() != 3)
        fail(QStringLiteral("Некорректная версия выпуска"));
    return result;
}

QUrl source(const QString& overrideUrl, const QString& repository)
{
    if (!overrideUrl.trimmed().isEmpty()) {
        const QUrl url(overrideUrl.trimmed());
        if (!allowed(url))
            fail(QStringLiteral("Источник обновлений должен использовать HTTPS; для локального теста разрешён HTTP на 127.0.0.1"));
        return url;
    }
    const QUrl repo(repository);
    if (repo.scheme() != "https" || repo.host() != "github.com" || !repo.userInfo().isEmpty() ||
        repo.hasQuery() || repo.hasFragment() ||
        !QRegularExpression("^/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+/?$").match(repo.path()).hasMatch())
        fail(QStringLiteral("Не настроен репозиторий GitHub Releases"));
    auto path = repo.path();
    if (path.endsWith('/'))
        path.chop(1);
    return QUrl("https://api.github.com/repos" + path + "/releases/latest");
}

ReleaseInfo parse(const QByteArray& bytes)
{
    if (bytes.size() > 2 * 1024 * 1024)
        fail(QStringLiteral("Слишком большой ответ источника обновлений"));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        fail(QStringLiteral("Источник обновлений вернул некорректный JSON"));
    const auto value = document.object();
    if (!value["draft"].isBool() || !value["prerelease"].isBool() || value["draft"].toBool() || value["prerelease"].toBool())
        fail(QStringLiteral("Источник не содержит опубликованный стабильный выпуск"));
    ReleaseInfo info;
    info.tag = value["tag_name"].toString();
    info.version = version(info.tag);
    info.notes = value["body"].toString();
    info.page = QUrl(value["html_url"].toString());
    if (!allowed(info.page))
        fail(QStringLiteral("Источник вернул недопустимый адрес страницы выпуска"));
    return info;
}
}  // namespace LauncherUpdate
