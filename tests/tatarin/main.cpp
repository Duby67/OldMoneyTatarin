#include "tatarin/Repository.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QTextStream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() != 3)
            throw std::runtime_error("Usage: tatarin-check source destination");
        QNetworkAccessManager network;
        const auto source = Tatarin::sourceUrl(args[1]);
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
