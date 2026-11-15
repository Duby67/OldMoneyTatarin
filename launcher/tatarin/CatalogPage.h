#pragma once

#include "ui/pages/BasePage.h"
#include <QWidget>
#include <QJsonArray>

class NewInstanceDialog;
class QLineEdit;
class QListWidget;
class QLabel;

namespace Tatarin {
class CatalogPage : public QWidget, public BasePage {
    Q_OBJECT
public:
    explicit CatalogPage(NewInstanceDialog* dialog);
    QString id() const override { return "tatarin"; }
    QString displayName() const override { return QStringLiteral("TATARIN"); }
    QIcon icon() const override { return QIcon::fromTheme("viewfolder"); }
    void openedImpl() override;
private:
    void refresh();
    void select();
    NewInstanceDialog* m_dialog;
    QLineEdit* m_source;
    QListWidget* m_list;
    QLabel* m_status;
    QJsonArray m_packs;
};
}
