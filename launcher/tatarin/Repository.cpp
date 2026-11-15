#include "Repository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace Tatarin {
namespace {
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }
bool identifier(const QString& text)
{
    return QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$").match(text).hasMatch();
}
bool hashValid(const QString& text) { return QRegularExpression("^[0-9a-f]{64}$").match(text).hasMatch(); }
bool reparse(const QString& path)
{
#ifdef Q_OS_WIN
    auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return QFileInfo(path).isSymLink();
#endif
}

QByteArray request(QNetworkAccessManager* network, const QUrl& url, qint64 limit, Cancel cancel,
                   QSaveFile* output = nullptr)
{
    if (cancel && cancel())
        fail(QStringLiteral("Операция отменена"));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = network->get(request);
    reply->setReadBufferSize(256 * 1024);
    QEventLoop loop;
    QTimer timer;
    timer.setInterval(50);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (cancel && cancel())
            reply->abort();
    });
    QByteArray bytes;
    qint64 received = 0;
    bool writeError = false;
    auto consume = [&] {
        auto chunk = reply->readAll();
        received += chunk.size();
        if (received > limit || (output && output->write(chunk) != chunk.size())) {
            writeError = true;
            reply->abort();
        } else if (!output) {
            bytes += chunk;
        }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start();
    loop.exec();
    consume();
    const bool okay = !writeError && reply->error() == QNetworkReply::NoError &&
                      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
    reply->deleteLater();
    if (cancel && cancel())
        fail(QStringLiteral("Операция отменена"));
    if (writeError)
        fail(QStringLiteral("Превышен размер ответа TATARIN или не удалось записать файл"));
    if (!okay)
        throw NetworkError("Не удалось получить файлы TATARIN: источник недоступен или ответ некорректен");
    return bytes;
}

QJsonObject parse(const QByteArray& bytes)
{
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        fail(QStringLiteral("Некорректный JSON TATARIN"));
    return doc.object();
}

void download(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& entry,
              const QString& destination, Cancel cancel)
{
    if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
        fail(QStringLiteral("Не удалось создать папку загрузки"));
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Не удалось открыть файл загрузки"));
    request(network, source.resolved(QUrl("objects/" + entry["sha256"].toString())),
            entry["size"].toInteger(), cancel, &output);
    if (!output.commit() || QFileInfo(destination).size() != entry["size"].toInteger() ||
        fileHash(destination) != entry["sha256"].toString())
        fail(QStringLiteral("Размер или SHA-256 загруженного файла не совпадает с манифестом"));
}
}  // namespace

QUrl sourceUrl(const QString& text)
{
    QUrl url(text.trimmed());
    if (!url.isValid() || url.scheme() != "http" || url.host() != "127.0.0.1" ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || url.path() != "/")
        fail(QStringLiteral("Локальный источник должен иметь вид http://127.0.0.1:18765/. Публичные источники пока отключены"));
    return url;
}

QJsonObject catalog(QNetworkAccessManager* network, const QUrl& source, Cancel cancel)
{
    sourceUrl(source.toString());
    auto value = parse(request(network, source.resolved(QUrl("v1/catalog.json")), 4 * 1024 * 1024, cancel));
    if (value["schema"].toInt() != 1 || !value["packs"].isArray() || value["packs"].toArray().size() > 1000)
        fail(QStringLiteral("Неподдерживаемый каталог TATARIN"));
    QSet<QString> ids;
    for (const auto& item : value["packs"].toArray()) {
        auto pack = item.toObject();
        auto id = pack["id"].toString();
        auto release = pack["release"].toString();
        if (!identifier(id) || !identifier(release) || ids.contains(id) || pack["name"].toString().isEmpty() ||
            !hashValid(pack["sha256"].toString()) ||
            pack["manifest"].toString() != QString("v1/packs/%1/releases/%2.json").arg(id, release))
            fail(QStringLiteral("Некорректная запись каталога TATARIN"));
        ids.insert(id);
    }
    return value;
}

QJsonObject manifest(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& pack, Cancel cancel)
{
    auto bytes = request(network, source.resolved(QUrl(pack["manifest"].toString())), 16 * 1024 * 1024, cancel);
    if (QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != pack["sha256"].toString())
        fail(QStringLiteral("SHA-256 манифеста не совпадает с каталогом"));
    auto value = parse(bytes);
    validateManifest(value);
    if (value["id"] != pack["id"] || value["release"] != pack["release"])
        fail(QStringLiteral("Манифест относится к другой сборке или выпуску"));
    return value;
}

QString checkedPath(const QString& root, const QString& relative)
{
    if (relative.isEmpty() || relative.size() > 220 || relative.contains('\\') || relative.startsWith('/') ||
        relative.contains(QRegularExpression("[<>:\"|?*\\x00-\\x1f]")))
        fail(QStringLiteral("Недопустимый путь TATARIN"));
    for (const auto& part : relative.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ') ||
            QRegularExpression("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption).match(part).hasMatch())
            fail(QStringLiteral("Недопустимый компонент пути TATARIN"));
    }
    const auto absolute = QDir(root).absoluteFilePath(relative);
    auto cursor = absolute;
    while (true) {
        if (reparse(cursor))
            fail(QStringLiteral("Ссылки и junction в папке TATARIN не поддерживаются"));
        const auto parent = QFileInfo(cursor).absolutePath();
        if (parent == cursor)
            break;
        cursor = parent;
    }
    if (QFileInfo(absolute).exists() && !QFileInfo(absolute).isFile())
        fail(QStringLiteral("Вместо файла TATARIN обнаружен каталог"));
    return absolute;
}

void validateManifest(const QJsonObject& value)
{
    const QRegularExpression version("^[0-9]+(?:\\.[0-9]+){1,3}$");
    if (value["schema"].toInt() != 1 || !identifier(value["id"].toString()) || !identifier(value["release"].toString()) ||
        value["name"].toString().isEmpty() || !version.match(value["minecraft"].toString()).hasMatch() ||
        !version.match(value["neoforge"].toString()).hasMatch() || !value["files"].isArray() ||
        value["files"].toArray().size() > 50000)
        fail(QStringLiteral("Неподдерживаемый манифест TATARIN"));
    const QSet<QString> roots = {"mods", "config", "defaultconfigs", "configureddefaults", "kubejs", "resourcepacks",
                                 "shaderpacks", "datapacks", "moonlight-global-datapacks", "patchouli_books"};
    QSet<QString> paths;
    qint64 total = 0;
    for (const auto& item : value["files"].toArray()) {
        auto entry = item.toObject();
        auto path = entry["path"].toString();
        checkedPath(QDir::tempPath() + "/tatarin-validation", path);
        auto size = entry["size"].toInteger(-1);
        auto policy = entry["policy"].toString();
        if (!roots.contains(path.section('/', 0, 0)) || !path.contains('/') || paths.contains(path.toCaseFolded()) ||
            !hashValid(entry["sha256"].toString()) || size < 0 || size > 1024LL * 1024 * 1024 ||
            entry["size"].toDouble() != static_cast<double>(size) || (policy != "managed" && policy != "seed"))
            fail(QStringLiteral("Некорректный файл в манифесте TATARIN"));
        total += size;
        if (total > 32LL * 1024 * 1024 * 1024)
            fail(QStringLiteral("Слишком большой выпуск TATARIN"));
        paths.insert(path.toCaseFolded());
    }
    if (value.contains("initializedSeeds")) {
        if (!value["initializedSeeds"].isArray() || value["initializedSeeds"].toArray().size() > 50000)
            fail(QStringLiteral("Повреждён реестр стартовых файлов TATARIN"));
        for (const auto& item : value["initializedSeeds"].toArray()) {
            const auto path = item.toString();
            checkedPath(QDir::tempPath() + "/tatarin-validation", path);
            if (!roots.contains(path.section('/', 0, 0)) || !path.contains('/'))
                fail(QStringLiteral("Повреждён реестр стартовых файлов TATARIN"));
        }
    }
    for (const auto& path : paths) {
        auto prefix = path.section('/', 0, -2);
        while (!prefix.isEmpty()) {
            if (paths.contains(prefix))
                fail(QStringLiteral("Конфликт файла и каталога TATARIN"));
            if (!prefix.contains('/'))
                break;
            prefix = prefix.section('/', 0, -2);
        }
    }
}

QString fileHash(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Не удалось прочитать файл TATARIN"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        fail(QStringLiteral("Не удалось вычислить SHA-256"));
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024)
        fail(QStringLiteral("Не удалось прочитать состояние TATARIN"));
    return parse(file.readAll());
}

void writeJson(const QString& path, const QJsonObject& value)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail(QStringLiteral("Не удалось создать папку состояния TATARIN"));
    QSaveFile file(path);
    auto bytes = QJsonDocument(value).toJson();
    if (bytes.size() > 64 * 1024 * 1024)
        fail(QStringLiteral("Слишком большой документ состояния TATARIN"));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        fail(QStringLiteral("Не удалось сохранить состояние TATARIN"));
}

void install(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& value,
             const QString& instanceRoot, const QString& gameRoot, Cancel cancel, Progress progress)
{
    validateManifest(value);
    const auto files = value["files"].toArray();
    qint64 done = 0;
    for (const auto& item : files) {
        const auto entry = item.toObject();
        download(network, source, entry, checkedPath(gameRoot, entry["path"].toString()), cancel);
        if (progress)
            progress(++done, files.size());
    }
    writeJson(checkedPath(instanceRoot, "tatarin/installed.json"), value);
}

void downloadObject(QNetworkAccessManager* network, const QUrl& source, const QJsonObject& entry,
                    const QString& destination, Cancel cancel)
{
    download(network, source, entry, destination, cancel);
}
}  // namespace Tatarin
