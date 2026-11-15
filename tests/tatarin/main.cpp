#include "tatarin/Repository.h"
#include "tatarin/MemoryPolicy.h"
#include "tatarin/ClientData.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 2 && args[1] == "--memory") {
            if (Tatarin::memoryRequirements({}).has_value())
                throw std::runtime_error("Legacy manifest memory must remain optional");
            const QJsonObject memory{{"minMiB", 8192}, {"maxMiB", 8192}, {"systemReserveMiB", 2048}};
            const auto required = *Tatarin::memoryRequirements({{"memory", memory}});
            if (Tatarin::memoryWarning(required, 6144, 4096).isEmpty() ||
                Tatarin::memoryWarning(required, 8192, 8192).isEmpty() ||
                Tatarin::memoryWarning(required, 16384, 6144).isEmpty() ||
                Tatarin::memoryWarning(required, 0, 0).isEmpty() ||
                !Tatarin::memoryWarning(required, 16384, 10240).isEmpty())
                throw std::runtime_error("Memory warning scenarios failed");
            const Tatarin::MemoryRequirements range{8192, 12288, 2048};
            if (Tatarin::memoryWarning(range, 12288, 12288).isEmpty() ||
                !Tatarin::memoryWarning(range, 16384, 12288).isEmpty())
                throw std::runtime_error("System reserve must also cover the maximum heap");
            for (const auto& invalid : {QJsonObject{{"minMiB", 8192}, {"maxMiB", 4096}},
                                       QJsonObject{{"minMiB", "8192"}, {"maxMiB", 8192}},
                                       QJsonObject{{"minMiB", 8192.5}, {"maxMiB", 8192}},
                                       QJsonObject{{"minMiB", 8192}, {"maxMiB", 999999}}}) {
                bool failed = false;
                try { Tatarin::memoryRequirements({{"memory", invalid}}); }
                catch (const std::exception&) { failed = true; }
                if (!failed)
                    throw std::runtime_error("Invalid memory specification accepted");
            }
            QTextStream(stdout) << "PASS: pack RAM requirements, insufficient total/free RAM, reserve and invalid values\n";
            return 0;
        }
        if (args.size() < 3 || args.size() > 4)
            throw std::runtime_error("Usage: tatarin-check source destination [install|sync|verify|cancel]");
        QNetworkAccessManager network;
        const auto source = Tatarin::sourceUrl(args[1]);
        const auto mode = args.value(3, "install");
        if (mode.startsWith("import=")) {
            const auto plan = Tatarin::planClientData(mode.mid(7), args[2], args[2] + "/minecraft");
            Tatarin::importClientData(plan, args[2], args[2] + "/minecraft");
            QTextStream(stdout) << "PASS: client data imported\n";
            return 0;
        }
        if (mode == "sync" || mode == "cancel" || mode == "failapply") {
            bool cancelled = false;
            int applyCount = 0;
            Tatarin::synchronize(&network, source, "regionalworlds", args[2], args[2] + "/minecraft",
                                 [&] { return cancelled; }, [&](qint64, qint64) { cancelled = mode == "cancel"; },
                                 [&](bool applying) {
                                     if (applying && ++applyCount == 2 && mode == "failapply")
                                         QDir().mkpath(args[2] + "/minecraft/mods/z-block.jar");
                                 });
            QTextStream(stdout) << "PASS: synchronized\n";
            return 0;
        }
        if (mode == "verify") {
            Tatarin::verifiedInstalled(args[2], args[2] + "/minecraft", "regionalworlds");
            QTextStream(stdout) << "PASS: verified installed release\n";
            return 0;
        }
        if (mode == "extras") {
            const auto value = Tatarin::readJson(args[2] + "/tatarin/installed.json");
            for (const auto& path : Tatarin::additionalMods(args[2] + "/minecraft", value))
                QTextStream(stdout) << path << '\n';
            return 0;
        }
        const auto packs = Tatarin::catalog(&network, source)["packs"].toArray();
        if (packs.isEmpty())
            throw std::runtime_error("Empty catalog");
        const auto value = Tatarin::manifest(&network, source, packs[0].toObject());
        if (QFileInfo::exists(args[2]))
            throw std::runtime_error("Destination exists");
        Tatarin::install(&network, source, value, args[2], args[2] + "/minecraft", {}, {});
        for (const auto& item : value["files"].toArray()) {
            const auto entry = item.toObject();
            if (Tatarin::fileHash(args[2] + "/minecraft/" + entry["path"].toString()) != entry["sha256"].toString())
                throw std::runtime_error("Installed file hash mismatch");
        }
        QTextStream(stdout) << "PASS: installed and verified " << value["files"].toArray().size() << " files\n";
        return 0;
    } catch (const std::exception& error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
