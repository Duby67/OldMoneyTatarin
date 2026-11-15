#include "Repository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLockFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <memory>

namespace Tatarin {
namespace {
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }

void copyFile(const QString& source, const QString& target)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || !QDir().mkpath(QFileInfo(target).absolutePath()))
        fail(QStringLiteral("Не удалось подготовить копирование TATARIN"));
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Не удалось открыть файл для замены TATARIN"));
    while (!input.atEnd()) {
        auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            fail(QStringLiteral("Ошибка копирования TATARIN"));
    }
    if (!output.commit())
        fail(QStringLiteral("Не удалось заменить файл TATARIN"));
}

QMap<QString, QJsonObject> fileMap(const QJsonObject& value)
{
    QMap<QString, QJsonObject> result;
    for (const auto& item : value["files"].toArray()) {
        auto entry = item.toObject();
        result.insert(entry["path"].toString().toCaseFolded(), entry);
    }
    return result;
}

QJsonObject installed(const QString& instanceRoot, const QString& packId)
{
    auto value = readJson(checkedPath(instanceRoot, "tatarin/installed.json"));
    validateManifest(value);
    if (value["id"].toString() != packId)
        fail(QStringLiteral("Локальный манифест относится к другой сборке"));
    return value;
}

void recover(const QString& instanceRoot, const QString& gameRoot, const QString& packId)
{
    const auto journalPath = checkedPath(instanceRoot, "tatarin/pending.json");
    if (!QFileInfo::exists(journalPath))
        return;
    const auto journal = readJson(journalPath);
    const auto before = journal["before"].toObject();
    const auto after = journal["after"].toObject();
    validateManifest(before);
    validateManifest(after);
    const auto transaction = journal["transaction"].toString();
    if (before["id"].toString() != packId || after["id"].toString() != packId ||
        !QRegularExpression("^[0-9a-f-]{36}$").match(transaction).hasMatch() || !journal["operations"].isArray())
        fail(QStringLiteral("Повреждён журнал восстановления TATARIN"));
    const auto oldFiles = fileMap(before);
    const auto newFiles = fileMap(after);
    QSet<QString> seen;
    // Проверяем весь журнал и резервные копии до восстановления первого файла.
    for (const auto& item : journal["operations"].toArray()) {
        const auto op = item.toObject();
        const auto path = op["path"].toString();
        const auto key = path.toCaseFolded();
        if (seen.contains(key) || (!oldFiles.contains(key) && !newFiles.contains(key)) || !op["existed"].isBool())
            fail(QStringLiteral("Некорректная операция восстановления TATARIN"));
        seen.insert(key);
        checkedPath(gameRoot, path);
        if (op["existed"].toBool()) {
            auto backup = checkedPath(instanceRoot, "tatarin/backups/" + transaction + "/" + path);
            if (fileHash(backup) != op["beforeHash"].toString())
                fail(QStringLiteral("Повреждена резервная копия TATARIN; запуск остановлен"));
        }
    }
    for (const auto& item : journal["operations"].toArray()) {
        const auto op = item.toObject();
        const auto path = op["path"].toString();
        const auto target = checkedPath(gameRoot, path);
        if (op["existed"].toBool()) {
            copyFile(checkedPath(instanceRoot, "tatarin/backups/" + transaction + "/" + path), target);
        } else if (QFileInfo::exists(target) && !QFile::remove(target)) {
            fail(QStringLiteral("Не удалось отменить создание файла TATARIN"));
        }
    }
    writeJson(checkedPath(instanceRoot, "tatarin/installed.json"), before);
    if (!QFile::remove(journalPath))
        fail(QStringLiteral("Не удалось завершить восстановление TATARIN"));
}

std::unique_ptr<QLockFile> lock(const QString& instanceRoot)
{
    const auto path = checkedPath(instanceRoot, "tatarin/sync.lock");
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail(QStringLiteral("Не удалось создать папку TATARIN"));
    auto result = std::make_unique<QLockFile>(path);
    // Не отбираем блокировку у долгой загрузки другого процесса.
    result->setStaleLockTime(0);
    if (!result->tryLock(0))
        fail(QStringLiteral("Этот экземпляр TATARIN уже обновляется другим процессом"));
    return result;
}
}  // namespace

QJsonObject verifiedInstalled(const QString& instanceRoot, const QString& gameRoot, const QString& packId)
{
    const auto guard = lock(instanceRoot);
    recover(instanceRoot, gameRoot, packId);
    const auto value = installed(instanceRoot, packId);
    for (const auto& item : value["files"].toArray()) {
        const auto entry = item.toObject();
        if (entry["policy"].toString() == "seed")
            continue;
        auto path = checkedPath(gameRoot, entry["path"].toString());
        if (!QFileInfo::exists(path) || fileHash(path) != entry["sha256"].toString())
            fail(QStringLiteral("Установленная сборка повреждена. Для восстановления нужен источник TATARIN"));
    }
    return value;
}

QJsonObject synchronize(QNetworkAccessManager* network, const QUrl& source, const QString& packId,
                        const QString& instanceRoot, const QString& gameRoot, Cancel cancel, Progress progress,
                        std::function<void(bool)> applying)
{
    const auto guard = lock(instanceRoot);
    if (applying)
        applying(true);
    recover(instanceRoot, gameRoot, packId);
    if (applying)
        applying(false);
    const auto before = installed(instanceRoot, packId);
    QJsonObject pack;
    for (const auto& item : catalog(network, source, cancel)["packs"].toArray()) {
        if (item.toObject()["id"].toString() == packId)
            pack = item.toObject();
    }
    if (pack.isEmpty())
        fail(QStringLiteral("Сборка отсутствует в каталоге TATARIN"));
    const auto after = manifest(network, source, pack, cancel);
    const auto oldFiles = fileMap(before);
    const auto newFiles = fileMap(after);
    const auto transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonArray operations;
    qint64 done = 0;
    auto prepare = [&](const QString& path, const QJsonObject& entry) {
        if (cancel && cancel())
            fail(QStringLiteral("Операция отменена"));
        const auto target = checkedPath(gameRoot, path);
        const bool exists = QFileInfo::exists(target);
        const auto currentHash = exists ? fileHash(target) : QString();
        if (!entry.isEmpty()) {
            if (exists && currentHash == entry["sha256"].toString())
                return;
            const auto stage = checkedPath(instanceRoot, "tatarin/downloads/" + entry["sha256"].toString());
            if (!QFileInfo::exists(stage) || fileHash(stage) != entry["sha256"].toString())
                downloadObject(network, source, entry, stage, cancel);
        } else if (!exists) {
            return;
        }
        if (exists) {
            const auto backup = checkedPath(instanceRoot, "tatarin/backups/" + transaction + "/" + path);
            copyFile(target, backup);
            if (fileHash(backup) != currentHash)
                fail(QStringLiteral("Не удалось проверить резервную копию TATARIN"));
        }
        operations.append(QJsonObject{{"path", path}, {"existed", exists}, {"beforeHash", currentHash},
                                      {"download", entry["sha256"].toString()}});
    };
    for (auto it = newFiles.cbegin(); it != newFiles.cend(); ++it) {
        const auto path = it.value()["path"].toString();
        if (oldFiles.contains(it.key()) && oldFiles[it.key()]["path"].toString() != path)
            fail(QStringLiteral("Изменение только регистра пути не поддерживается"));
        prepare(path, it.value());
        if (progress)
            progress(++done, newFiles.size() + oldFiles.size());
        QCoreApplication::processEvents();
    }
    for (auto it = oldFiles.cbegin(); it != oldFiles.cend(); ++it) {
        if (!newFiles.contains(it.key()) && it.value()["policy"].toString() == "managed")
            prepare(it.value()["path"].toString(), {});
        if (progress)
            progress(++done, newFiles.size() + oldFiles.size());
    }
    if (cancel && cancel())
        fail(QStringLiteral("Операция отменена"));
    if (applying)
        applying(true);
    if (operations.isEmpty()) {
        writeJson(checkedPath(instanceRoot, "tatarin/installed.json"), after);
        return after;
    }
    const auto journalPath = checkedPath(instanceRoot, "tatarin/pending.json");
    writeJson(journalPath, {{"transaction", transaction}, {"before", before}, {"after", after}, {"operations", operations}});
    try {
        for (const auto& item : operations) {
            const auto op = item.toObject();
            const auto target = checkedPath(gameRoot, op["path"].toString());
            const auto hash = op["download"].toString();
            if (!hash.isEmpty()) {
                copyFile(checkedPath(instanceRoot, "tatarin/downloads/" + hash), target);
                if (fileHash(target) != hash)
                    fail(QStringLiteral("Файл изменился при применении обновления TATARIN"));
            } else if (QFileInfo::exists(target) && !QFile::remove(target)) {
                fail(QStringLiteral("Не удалось удалить устаревший файл TATARIN"));
            }
        }
        writeJson(checkedPath(instanceRoot, "tatarin/installed.json"), after);
        if (!QFile::remove(journalPath))
            fail(QStringLiteral("Не удалось завершить транзакцию TATARIN"));
    } catch (...) {
        recover(instanceRoot, gameRoot, packId);
        throw;
    }
    return after;
}
}  // namespace Tatarin
