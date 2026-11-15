#include "ReleaseChecker.h"
#include "ReleaseInfo.h"
#include "ReleaseQuery.h"

#include "Application.h"
#include "BuildConfig.h"
#include "settings/SettingsObject.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>
#include <algorithm>
#include <chrono>
#include <exception>

ReleaseChecker::ReleaseChecker(QWidget* parent) : ExternalUpdater(), m_parent(parent)
{
    connect(&m_timer, &QTimer::timeout, this, [this] { check(false); });
    schedule();
    QTimer::singleShot(1500, this, [this] {
        if (getAutomaticallyChecksForUpdates())
            check(false);
    });
}

bool ReleaseChecker::getAutomaticallyChecksForUpdates() { return APPLICATION->settings()->get("LauncherAutoUpdateCheck").toBool(); }
double ReleaseChecker::getUpdateCheckInterval() { return APPLICATION->settings()->get("LauncherUpdateInterval").toDouble(); }
void ReleaseChecker::setAutomaticallyChecksForUpdates(bool check)
{
    APPLICATION->settings()->set("LauncherAutoUpdateCheck", check);
    schedule();
}
void ReleaseChecker::setUpdateCheckInterval(double seconds)
{
    APPLICATION->settings()->set("LauncherUpdateInterval", std::clamp(seconds, 0.0, 604800.0));
    schedule();
}
void ReleaseChecker::schedule()
{
    m_timer.stop();
    const auto seconds = std::clamp(getUpdateCheckInterval(), 0.0, 604800.0);
    if (getAutomaticallyChecksForUpdates() && seconds > 0)
        m_timer.start(std::chrono::seconds(static_cast<int>(seconds)));
}
void ReleaseChecker::checkForUpdates() { check(true); }

void ReleaseChecker::result(const QString& message, bool showDialog)
{
    APPLICATION->settings()->set("LauncherUpdateLastResult", message);
    APPLICATION->settings()->set("LauncherUpdateLastCheck", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    emit checkResult(message);
    if (showDialog) {
        QMessageBox box(QMessageBox::Information, QStringLiteral("Обновление OldMoneyTatarin"), message, QMessageBox::Ok, m_parent);
        box.setTextFormat(Qt::PlainText);
        box.exec();
    }
}

void ReleaseChecker::check(bool manual)
{
    if (m_busy)
        return;
    QUrl url;
    try {
        url = LauncherUpdate::source(APPLICATION->settings()->get("LauncherUpdateSource").toString(), BuildConfig.UPDATER_GITHUB_REPO);
    } catch (const std::exception& error) {
        result(QString::fromUtf8(error.what()), manual);
        return;
    }
    m_busy = true;
    emit canCheckForUpdatesChanged(false);
    emit checkResult(QStringLiteral("Проверка обновлений OldMoneyTatarin…"));
    auto* query = new ReleaseQuery(APPLICATION->network(), this);
    auto finish = [this, query] {
        m_busy = false;
        emit canCheckForUpdatesChanged(APPLICATION->updatesAreAllowed());
        query->deleteLater();
    };
    connect(query, &ReleaseQuery::failed, this, [this, finish, manual](const QString& error) {
        finish();
        result(QStringLiteral("Проверка обновлений не выполнена: %1").arg(error), manual);
    });
    connect(query, &ReleaseQuery::releaseReady, this, [this, finish, manual](const LauncherUpdate::ReleaseInfo& release) {
        finish();
        try {
            const auto current = LauncherUpdate::version(BuildConfig.versionString());
            if (QVersionNumber::compare(release.version, current) <= 0) {
                result(QStringLiteral("Обновлений нет. Установлена версия %1; последний стабильный выпуск в источнике — %2.")
                           .arg(BuildConfig.versionString(), release.tag), manual);
                return;
            }
            const auto message = QStringLiteral("Доступна новая версия OldMoneyTatarin: %1. Установлена: %2.")
                                     .arg(release.tag, BuildConfig.versionString());
            result(message, false);
            QMessageBox box(QMessageBox::Information, QStringLiteral("Доступно обновление OldMoneyTatarin"), message,
                            QMessageBox::Close, m_parent);
            box.setTextFormat(Qt::PlainText);
            box.setInformativeText(QStringLiteral("Откройте страницу выпуска для просмотра патчноута и загрузки установщика."));
            if (!release.notes.isEmpty())
                box.setDetailedText(release.notes);
            auto* open = box.addButton(QStringLiteral("Открыть страницу выпуска"), QMessageBox::ActionRole);
            box.exec();
            if (box.clickedButton() == open)
                QDesktopServices::openUrl(release.page);
        } catch (const std::exception& error) {
            result(QStringLiteral("Проверка обновлений не выполнена: %1").arg(QString::fromUtf8(error.what())), manual);
        }
    });
    query->start(url);
}
