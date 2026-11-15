#include "updater/ReleaseInfo.h"
#include "updater/ReleaseQuery.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

void require(bool okay)
{
    if (!okay)
        throw std::runtime_error("Check failed");
}

void selftest()
{
    const auto v = LauncherUpdate::version;
    require(QVersionNumber::compare(v("v0.10.0"), v("0.3.0")) > 0);
    require(QVersionNumber::compare(v("v0.2.0"), v("0.2.0")) == 0);
    for (const auto& invalid : {"", "v0.3", "v01.2.0", "v0.3.0-beta", "v0.3.0.1", "v9999999999.0.0"}) {
        bool failed = false;
        try { v(invalid); } catch (const std::exception&) { failed = true; }
        require(failed);
    }
    require(LauncherUpdate::source({}, "https://github.com/Duby67/OldMoneyTatarin").toString() ==
            "https://api.github.com/repos/Duby67/OldMoneyTatarin/releases/latest");
    for (const auto& invalid : {"http://example.com/latest.json", "file:///tmp/latest.json", "https://user:secret@example.com/latest.json"}) {
        bool failed = false;
        try { LauncherUpdate::source(invalid, {}); } catch (const std::exception&) { failed = true; }
        require(failed);
    }
    auto object = QJsonObject{{"tag_name", "v0.3.0"}, {"draft", false}, {"prerelease", false},
                              {"html_url", "https://github.com/Duby67/OldMoneyTatarin/releases/tag/v0.3.0"}, {"body", "notes"}};
    require(LauncherUpdate::parse(QJsonDocument(object).toJson()).tag == "v0.3.0");
    for (const auto& key : {"draft", "prerelease"}) {
        object[key] = true;
        bool failed = false;
        try { LauncherUpdate::parse(QJsonDocument(object).toJson()); } catch (const std::exception&) { failed = true; }
        require(failed);
        object[key] = false;
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        if (args.size() == 1) {
            selftest();
            QTextStream(stdout) << "PASS: numeric versions, stable releases, source validation\n";
            return 0;
        }
        if (args.size() < 4 || args.size() > 5)
            throw std::runtime_error("Usage: update-check url current-version expected-state [timeout-ms]");
        const auto current = LauncherUpdate::version(args[2]);
        QNetworkAccessManager network;
        ReleaseQuery query(&network);
        QString state;
        QObject::connect(&query, &ReleaseQuery::releaseReady, &app, [&](const LauncherUpdate::ReleaseInfo& info) {
            state = QVersionNumber::compare(info.version, current) > 0 ? "update" : "current";
            QTextStream(stdout) << state << " " << info.tag << '\n';
            app.quit();
        });
        QObject::connect(&query, &ReleaseQuery::failed, &app, [&](const QString&) {
            state = "error";
            QTextStream(stdout) << state << '\n';
            app.quit();
        });
        QTimer::singleShot(0, &query, [&] { query.start(QUrl(args[1]), args.value(4, "2000").toInt()); });
        app.exec();
        require(state == args[3]);
        return 0;
    } catch (const std::exception& error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
