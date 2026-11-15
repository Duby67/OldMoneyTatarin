#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVersionNumber>

namespace LauncherUpdate {
struct ReleaseInfo {
    QVersionNumber version;
    QString tag;
    QString notes;
    QUrl page;
};
QVersionNumber version(const QString& text);
QUrl source(const QString& overrideUrl, const QString& repository);
ReleaseInfo parse(const QByteArray& bytes);
}  // namespace LauncherUpdate
