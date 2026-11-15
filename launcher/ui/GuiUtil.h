#pragma once

#include <QFileInfo>
#include <QWidget>
#include <optional>

namespace GuiUtil {
void setClipboardText(QString text);
QStringList BrowseForFiles(QString context, QString caption, QString filter, QString defaultPath, QWidget* parentWidget);
QString BrowseForFile(QString context, QString caption, QString filter, QString defaultPath, QWidget* parentWidget);
}  // namespace GuiUtil
