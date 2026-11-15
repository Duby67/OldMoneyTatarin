#include "ClientData.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace Tatarin {
namespace {
[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toUtf8().constData()); }

bool personalPath(const QString& path)
{
    const auto parts = path.toLower().split('/');
    const QSet<QString> excludedRoots = {"mods", "kubejs", "scripts", "defaultconfigs", "configureddefaults", "datapacks",
        "moonlight-global-datapacks", "downloads", "logs", "debug", "crash-reports", "server-resource-packs", "bin", "natives", "patches",
        "dynamic-data-pack-cache", "dynamic-resource-pack-cache", "backups", "accounts", "libraries", "versions", "assets"};
    if (excludedRoots.contains(parts.first()))
        return false;
    const QSet<QString> privateParts = {"cache", "caches", "sessions", "accounts", "credentials", "e4mc", "crash_assistant", "spark"};
    for (const auto& part : parts) {
        if (part.startsWith('.') || privateParts.contains(part))
            return false;
    }
    const auto name = parts.last();
    if (name == "session.lock" || name == "usercache.json" || name == "usernamecache.json" || name == "command_history.txt" ||
        name == "instance.cfg" || name == "mmc-pack.json" || name == "aoc-client-manifest.json" ||
        QRegularExpression("(?:token|password|cookie|credentials|account|session)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
        return false;
    for (const auto& suffix : {".bak", ".old", ".dat_old", ".log", ".tmp", ".lock", ".exe", ".dll", ".jar", ".bat", ".cmd", ".ps1", ".lnk", ".url"}) {
        if (name.endsWith(suffix))
            return false;
    }
    // В корне игровой папки копируем только известные клиентские файлы.
    if (parts.size() == 1)
        return name == "servers.dat" || name == "patchouli_data.json" ||
               (name.startsWith("options") && name.endsWith(".txt")) ||
               (name.startsWith("ponders_watched") && name.endsWith(".json"));
    return true;
}

QSet<QString> protectedPaths(const QString& instanceRoot)
{
    QSet<QString> result;
    const auto state = checkedPath(instanceRoot, "tatarin/installed.json");
    if (!QFileInfo::exists(state))
        return result;
    const auto manifest = readJson(state);
    validateManifest(manifest);
    for (const auto& item : manifest["files"].toArray()) {
        const auto file = item.toObject();
        if (file["policy"].toString() == "managed")
            result.insert(file["path"].toString().toCaseFolded());
    }
    return result;
}

bool protectedPath(const QSet<QString>& paths, const QString& path)
{
    const auto lower = path.toCaseFolded();
    for (const auto& protectedFile : paths) {
        if (lower == protectedFile || lower.startsWith(protectedFile + '/') || protectedFile.startsWith(lower + '/'))
            return true;
    }
    return false;
}

bool containsCredentials(const QString& path)
{
    const QSet<QString> extensions = {"json", "json5", "toml", "cfg", "properties", "txt", "yml", "yaml"};
    if (!extensions.contains(QFileInfo(path).suffix().toLower()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(QStringLiteral("Не удалось прочитать клиентский файл"));
    if (file.size() > 16 * 1024 * 1024)
        return true;
    const auto text = QString::fromUtf8(file.readAll());
    const QRegularExpression pattern(R"REGEX(["']?(?:access[_-]?token|refresh[_-]?token|password|passwd|secret|api[_-]?key|authorization|cookie)["']?\s*[:=]\s*["']?([^\s"',}\]]+))REGEX",
                                     QRegularExpression::CaseInsensitiveOption);
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const auto value = matches.next().captured(1).toLower();
        if (value != "false" && value != "true" && value != "null" && value != "none" && value != "changeme")
            return true;
    }
    return false;
}

void copy(const QString& source, const QString& target, Cancel cancel = {})
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly) || !QDir().mkpath(QFileInfo(target).absolutePath()))
        fail(QStringLiteral("Не удалось подготовить копирование клиентского файла"));
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly))
        fail(QStringLiteral("Не удалось записать клиентский файл"));
    while (!input.atEnd()) {
        if (cancel && cancel())
            fail(QStringLiteral("Перенос отменён"));
        const auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size())
            fail(QStringLiteral("Ошибка копирования клиентского файла"));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    if (!output.commit())
        fail(QStringLiteral("Не удалось завершить запись клиентского файла"));
}

class WorldLocks {
public:
    WorldLocks(const QString& source, const QStringList& worlds)
    {
#ifdef Q_OS_WIN
        try {
            for (const auto& world : worlds) {
                const auto path = checkedPath(source, world + "/session.lock");
                if (!QFileInfo::exists(path))
                    continue;
                const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                    fail(QStringLiteral("Мир открыт в другой игре или недоступен. Закройте Minecraft перед переносом."));
                m_handles.append(handle);
            }
        } catch (...) {
            close();
            throw;
        }
#else
        Q_UNUSED(source)
        Q_UNUSED(worlds)
#endif
    }
    ~WorldLocks() { close(); }
private:
    void close()
    {
#ifdef Q_OS_WIN
        for (const auto handle : m_handles)
            CloseHandle(handle);
        m_handles.clear();
#endif
    }
#ifdef Q_OS_WIN
    QVector<HANDLE> m_handles;
#endif
};

std::unique_ptr<QLockFile> lock(const QString& instanceRoot)
{
    const auto path = checkedPath(instanceRoot, "tatarin/sync.lock");
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail(QStringLiteral("Не удалось создать папку блокировки"));
    auto guard = std::make_unique<QLockFile>(path);
    guard->setStaleLockTime(0);
    if (!guard->tryLock(0))
        fail(QStringLiteral("Этот экземпляр уже обновляется или переносит данные"));
    return guard;
}

void recover(const QString& instanceRoot, const QString& gameRoot)
{
    const auto path = checkedPath(instanceRoot, "client-data-import/pending.json");
    if (!QFileInfo::exists(path))
        return;
    const auto journal = readJson(path);
    const auto transaction = journal["transaction"].toString();
    const auto operations = journal["operations"].toArray();
    if (!QRegularExpression("^[0-9a-f-]{36}$").match(transaction).hasMatch() || !journal["operations"].isArray() || operations.size() > 200000)
        fail(QStringLiteral("Повреждён журнал переноса клиентских данных"));
    const auto protectedFiles = protectedPaths(instanceRoot);
    QSet<QString> seen;
    for (const auto& item : operations) {
        const auto op = item.toObject();
        const auto relative = op["path"].toString();
        if (!personalPath(relative) || protectedPath(protectedFiles, relative) || seen.contains(relative.toCaseFolded()) || !op["existed"].isBool())
            fail(QStringLiteral("Недопустимый путь в журнале переноса"));
        seen.insert(relative.toCaseFolded());
        checkedPath(gameRoot, relative);
        const auto target = checkedPath(gameRoot, relative);
        if (!QRegularExpression("^[0-9a-f]{64}$").match(op["afterHash"].toString()).hasMatch())
            fail(QStringLiteral("Повреждён хеш в журнале переноса"));
        if (QFileInfo::exists(target)) {
            const auto current = fileHash(target);
            if (current != op["afterHash"].toString() && (!op["existed"].toBool() || current != op["beforeHash"].toString()))
                fail(QStringLiteral("После сбоя клиентский файл %1 был изменён. Сохраните его отдельно перед восстановлением.").arg(relative));
        }
        if (op["existed"].toBool()) {
            const auto backup = checkedPath(instanceRoot, "client-data-import/backups/" + transaction + "/" + relative);
            if (fileHash(backup) != op["beforeHash"].toString())
                fail(QStringLiteral("Резервная копия клиентского файла повреждена"));
        }
    }
    for (const auto& item : operations) {
        const auto op = item.toObject();
        const auto relative = op["path"].toString();
        const auto target = checkedPath(gameRoot, relative);
        if (op["existed"].toBool())
            copy(checkedPath(instanceRoot, "client-data-import/backups/" + transaction + "/" + relative), target);
        else if (QFileInfo::exists(target) && !QFile::remove(target))
            fail(QStringLiteral("Не удалось отменить перенос клиентского файла"));
    }
    if (!QFile::remove(path))
        fail(QStringLiteral("Не удалось завершить восстановление клиентских данных"));
}
}

ClientDataPlan planClientData(const QString& source, const QString& instanceRoot, const QString& gameRoot)
{
    const auto sourcePath = QDir(source).absolutePath();
    const auto targetPath = QDir(gameRoot).absolutePath();
    if (!QDir(sourcePath).exists() || sourcePath.compare(targetPath, Qt::CaseInsensitive) == 0 ||
        sourcePath.startsWith(targetPath + '/', Qt::CaseInsensitive) || targetPath.startsWith(sourcePath + '/', Qt::CaseInsensitive))
        fail(QStringLiteral("Источник и назначение должны быть независимыми игровыми папками"));
    checkedPath(sourcePath, "options.txt");
    checkedPath(targetPath, "options.txt");
    if (!QFileInfo::exists(QDir(sourcePath).filePath("config")) && !QFileInfo::exists(QDir(sourcePath).filePath("options.txt")))
        fail(QStringLiteral("Выберите игровую папку, где находятся config или options.txt"));
    ClientDataPlan plan;
    plan.sourceRoot = sourcePath;
    const auto protectedFiles = protectedPaths(instanceRoot);
    QVector<QPair<QString, QString>> pending = {{"", ""}};
    QSet<QString> reservedWorlds;
    QSet<QString> targetFiles;
    while (!pending.isEmpty()) {
        const auto [from, to] = pending.takeLast();
        for (const auto& info : QDir(QDir(sourcePath).filePath(from)).entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
            const auto relative = from.isEmpty() ? info.fileName() : from + '/' + info.fileName();
            auto target = to.isEmpty() ? info.fileName() : to + '/' + info.fileName();
            if (!personalPath(relative + (info.isDir() ? "/probe" : ""))) {
                ++plan.omitted;
                continue;
            }
            try {
                checkedPath(sourcePath, relative + (info.isDir() ? "/probe" : ""));
            } catch (const std::exception&) {
                ++plan.omitted;
                continue;
            }
            if (info.isDir()) {
                if (from == "saves") {
                    const WorldLocks sourceWorld(sourcePath, {relative});
                    int suffix = 1;
                    while (QFileInfo::exists(QDir(gameRoot).filePath(target)) || reservedWorlds.contains(target.toCaseFolded()))
                        target = to + '/' + info.fileName() + QString("_import_%1").arg(suffix++);
                    reservedWorlds.insert(target.toCaseFolded());
                    if (target != relative)
                        ++plan.renamedWorlds;
                    plan.newWorlds.append(target);
                    plan.sourceWorlds.append(relative);
                }
                checkedPath(targetPath, target + "/probe");
                pending.append({relative, target});
            } else if (protectedPath(protectedFiles, target)) {
                ++plan.protectedFiles;
            } else if (containsCredentials(info.filePath())) {
                ++plan.omitted;
            } else {
                checkedPath(targetPath, target);
                if (targetFiles.contains(target.toCaseFolded()))
                    fail(QStringLiteral("Обнаружен конфликт имён клиентских файлов"));
                targetFiles.insert(target.toCaseFolded());
                plan.files.append({relative, target, info.size(), info.lastModified().toMSecsSinceEpoch()});
                plan.bytes += info.size();
                if (plan.files.size() > 200000)
                    fail(QStringLiteral("Слишком много файлов для одного переноса"));
            }
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    return plan;
}

void recoverClientData(const QString& instanceRoot, const QString& gameRoot, bool lockHeld)
{
    if (!QFileInfo::exists(checkedPath(instanceRoot, "client-data-import/pending.json")))
        return;
    const auto guard = lockHeld ? std::unique_ptr<QLockFile>() : lock(instanceRoot);
    recover(instanceRoot, gameRoot);
}

void importClientData(const ClientDataPlan& plan, const QString& instanceRoot, const QString& gameRoot,
                      Cancel cancel, Progress progress, std::function<void(bool)> applying)
{
    const auto guard = lock(instanceRoot);
    recover(instanceRoot, gameRoot);
    const WorldLocks worldLocks(plan.sourceRoot, plan.sourceWorlds);
    const auto protectedFiles = protectedPaths(instanceRoot);
    const auto transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto work = "client-data-import/work/" + transaction + '/';
    QJsonArray operations;
    qint64 done = 0;
    if (applying)
        applying(false);
    for (const auto& entry : plan.files) {
        if (cancel && cancel())
            fail(QStringLiteral("Перенос отменён"));
        if (!personalPath(entry.source) || !personalPath(entry.target) || protectedPath(protectedFiles, entry.target))
            fail(QStringLiteral("Состав сборки изменился; повторите предварительную проверку переноса"));
        const auto source = checkedPath(plan.sourceRoot, entry.source);
        if (QFileInfo(source).size() != entry.size || QFileInfo(source).lastModified().toMSecsSinceEpoch() != entry.modifiedMs)
            fail(QStringLiteral("Исходные файлы изменились; закройте игру и повторите проверку"));
        if (containsCredentials(source))
            fail(QStringLiteral("В клиентском файле появились учётные данные; повторите проверку"));
        const auto target = checkedPath(gameRoot, entry.target);
        const auto staged = checkedPath(instanceRoot, work + QString::number(done));
        copy(source, staged, cancel);
        if (QFileInfo(source).size() != entry.size || QFileInfo(source).lastModified().toMSecsSinceEpoch() != entry.modifiedMs)
            fail(QStringLiteral("Исходный файл изменился во время копирования; повторите перенос"));
        const auto newHash = fileHash(staged);
        const bool exists = QFileInfo::exists(target);
        QString oldHash;
        if (exists) {
            oldHash = fileHash(target);
            if (oldHash == newHash) {
                QFile::remove(staged);
                ++done;
                if (progress)
                    progress(done, plan.files.size());
                continue;
            }
            const auto backup = checkedPath(instanceRoot, "client-data-import/backups/" + transaction + '/' + entry.target);
            copy(target, backup, cancel);
            if (fileHash(backup) != oldHash)
                fail(QStringLiteral("Не удалось проверить резервную копию клиентского файла"));
        }
        operations.append(QJsonObject{{"path", entry.target}, {"existed", exists}, {"beforeHash", oldHash},
                                      {"stage", QString::number(done)}, {"afterHash", newHash}});
        ++done;
        if (progress)
            progress(done, plan.files.size());
    }
    for (const auto& world : plan.newWorlds) {
        checkedPath(gameRoot, world + "/probe");
        if (QFileInfo::exists(QDir(gameRoot).filePath(world)))
            fail(QStringLiteral("Папка мира появилась во время подготовки; повторите перенос"));
    }
    for (const auto& item : operations) {
        const auto op = item.toObject();
        const auto target = checkedPath(gameRoot, op["path"].toString());
        if (QFileInfo::exists(target) != op["existed"].toBool() ||
            (op["existed"].toBool() && fileHash(target) != op["beforeHash"].toString()))
            fail(QStringLiteral("Клиентские файлы изменились во время подготовки; повторите перенос"));
    }
    if (cancel && cancel())
        fail(QStringLiteral("Перенос отменён"));
    if (operations.isEmpty())
        return;
    if (applying)
        applying(true);
    const auto journal = checkedPath(instanceRoot, "client-data-import/pending.json");
    writeJson(journal, {{"transaction", transaction}, {"operations", operations}});
    try {
        for (const auto& item : operations) {
            const auto op = item.toObject();
            const auto target = checkedPath(gameRoot, op["path"].toString());
            copy(checkedPath(instanceRoot, work + op["stage"].toString()), target);
            if (fileHash(target) != op["afterHash"].toString())
                fail(QStringLiteral("Клиентский файл изменился при переносе"));
        }
        if (!QFile::remove(journal))
            fail(QStringLiteral("Не удалось завершить перенос клиентских данных"));
    } catch (...) {
        recover(instanceRoot, gameRoot);
        throw;
    }
    for (const auto& item : operations)
        QFile::remove(checkedPath(instanceRoot, work + item.toObject()["stage"].toString()));
    QDir().rmdir(QDir(instanceRoot).filePath(work));
}
}
