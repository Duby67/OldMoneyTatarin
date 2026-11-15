#include "tatarin/ClientData.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

void require(bool value)
{
    if (!value)
        throw std::runtime_error("Client-data assertion failed");
}
void write(const QString& path, const QByteArray& data)
{
    require(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly));
    require(file.write(data) == data.size());
}
QByteArray read(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid());
        const auto root = directory.path();
        const auto source = root + "/legacy";
        const auto instance = root + "/instance";
        const auto game = instance + "/minecraft";
        write(source + "/options.txt", "legacy-controls");
        write(source + "/servers.dat", "server-list");
        write(source + "/config/ui.json", "legacy-ui");
        write(source + "/config/server.toml", "do-not-import-gameplay");
        write(source + "/config/private.toml", "access_token=private-test-value");
        write(source + "/mods/extra.jar", "do-not-import-mods");
        write(source + "/kubejs/client_scripts/code.js", "do-not-import-code");
        write(source + "/logs/latest.log", "do-not-import-logs");
        write(source + "/accounts.json", "do-not-import-accounts");
        write(source + "/xaero/waypoints.dat", "legacy-waypoints");
        write(source + "/shaderpacks/personal.zip", "legacy-shader");
        write(source + "/screenshots/image.png", "legacy-image");
        write(source + "/saves/World/level.dat", "legacy-world");
        write(source + "/saves/World/session.lock", "");
        write(source + "/saves/World_import_1/level.dat", "another-legacy-world");
        write(game + "/saves/World/level.dat", "existing-world");
        write(game + "/options.txt", "existing-controls");
        write(game + "/config/server.toml", "required-gameplay");
        write(game + "/config/ui.json", "existing-ui");
        QJsonArray files;
        for (const auto& path : {"config/server.toml", "config/ui.json"})
            files.append(QJsonObject{{"path", path}, {"sha256", Tatarin::fileHash(game + '/' + path)},
                                     {"size", QFileInfo(game + '/' + path).size()}, {"policy", QString(path).contains("server") ? "managed" : "seed"}});
        Tatarin::writeJson(instance + "/tatarin/installed.json", {{"schema", 1}, {"id", "regionalworlds"}, {"name", "Test"},
            {"release", "test-1"}, {"minecraft", "1.21.1"}, {"neoforge", "21.1.264"}, {"files", files}});
        const auto plan = Tatarin::planClientData(source, instance, game);
        require(plan.protectedFiles == 1 && plan.renamedWorlds == 2);
        Tatarin::importClientData(plan, instance, game);
        require(read(game + "/options.txt") == "legacy-controls");
        require(read(source + "/options.txt") == "legacy-controls");
        require(read(game + "/config/server.toml") == "required-gameplay");
        require(read(game + "/config/ui.json") == "legacy-ui");
        require(read(game + "/xaero/waypoints.dat") == "legacy-waypoints");
        require(read(game + "/screenshots/image.png") == "legacy-image");
        require(read(game + "/saves/World/level.dat") == "existing-world");
        require(read(game + "/saves/World_import_1/level.dat") == "legacy-world");
        require(read(game + "/saves/World_import_1_import_1/level.dat") == "another-legacy-world");
        require(!QFile::exists(game + "/saves/World_import_1/session.lock"));
        require(!QFile::exists(game + "/mods/extra.jar") && !QFile::exists(game + "/config/private.toml"));
        require(!QFile::exists(game + "/accounts.json") && !QFile::exists(game + "/kubejs/client_scripts/code.js"));
        const auto transactions = QDir(instance + "/client-data-import/backups").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        require(transactions.size() == 1);
        require(read(instance + "/client-data-import/backups/" + transactions[0] + "/options.txt") == "existing-controls");
        write(source + "/options.txt", "changed-again-controls");
        const auto cancelledPlan = Tatarin::planClientData(source, instance, game);
        bool cancelled = false;
        bool failed = false;
        try {
            Tatarin::importClientData(cancelledPlan, instance, game, [&] { return cancelled; }, [&](qint64, qint64) { cancelled = true; });
        } catch (const std::exception&) { failed = true; }
        require(failed && read(game + "/options.txt") == "legacy-controls");
        write(source + "/zzz/payload.json", "new-file");
        const auto failurePlan = Tatarin::planClientData(source, instance, game);
        failed = false;
        try {
            Tatarin::importClientData(failurePlan, instance, game, {}, {}, [&](bool applying) {
                if (applying)
                    QDir().mkpath(game + "/zzz/payload.json");
            });
        } catch (const std::exception&) { failed = true; }
        require(failed && QFile::exists(instance + "/client-data-import/pending.json"));
        require(QDir().rmdir(game + "/zzz/payload.json"));
        write(game + "/options.txt", "manual-after-failure");
        failed = false;
        try { Tatarin::recoverClientData(instance, game); } catch (const std::exception&) { failed = true; }
        require(failed && read(game + "/options.txt") == "manual-after-failure");
        write(game + "/options.txt", "changed-again-controls");
        Tatarin::recoverClientData(instance, game);
        require(read(game + "/options.txt") == "legacy-controls");
        require(!QFile::exists(instance + "/client-data-import/pending.json"));
        failed = false;
        try { Tatarin::planClientData(game, instance, game); } catch (const std::exception&) { failed = true; }
        require(failed);
#ifdef Q_OS_WIN
        const auto outside = root + "/outside";
        write(outside + "/outside.json", "outside-data");
        const auto junction = source + "/linked";
        require(QProcess::execute("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(junction), QDir::toNativeSeparators(outside)}) == 0);
        const auto guarded = Tatarin::planClientData(source, instance, game);
        for (const auto& entry : guarded.files)
            require(!entry.source.startsWith("linked/"));
        require(QDir().rmdir(junction));
#endif
        QTextStream(stdout) << "PASS: client files, protected configs, secrets exclusion, world collisions, backups, cancel, crash recovery, containment\n";
        return 0;
    } catch (const std::exception& error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
