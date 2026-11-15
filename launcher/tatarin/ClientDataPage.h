#pragma once

#include "ui/pages/BasePage.h"
#include <QWidget>
class BaseInstance;

class ClientDataPage : public QWidget, public BasePage {
    Q_OBJECT
public:
    explicit ClientDataPage(BaseInstance* instance);
    QString id() const override { return "client-data"; }
    QString displayName() const override { return QStringLiteral("Данные клиента"); }
    QIcon icon() const override { return QIcon::fromTheme("viewfolder"); }
private:
    void importData();
    BaseInstance* m_instance;
};
