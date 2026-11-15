#pragma once

#include "ui/pages/BasePage.h"
#include <QWidget>
class BaseInstance;
class QPlainTextEdit;

namespace Tatarin {
class PackPage : public QWidget, public BasePage {
    Q_OBJECT
public:
    explicit PackPage(BaseInstance* instance);
    QString id() const override { return "tatarin-pack"; }
    QString displayName() const override { return QStringLiteral("TATARIN"); }
    QIcon icon() const override { return QIcon::fromTheme("viewfolder"); }
    void openedImpl() override;
private:
    BaseInstance* m_instance;
    QPlainTextEdit* m_text;
};
}
