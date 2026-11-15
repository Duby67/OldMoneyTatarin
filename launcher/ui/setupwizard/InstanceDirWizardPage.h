#pragma once

#include "BaseWizardPage.h"

class QLineEdit;

class InstanceDirWizardPage : public BaseWizardPage {
    Q_OBJECT
public:
    explicit InstanceDirWizardPage(QWidget* parent = nullptr);
    bool validatePage() override;
protected:
    void retranslate() override;
private:
    QLineEdit* m_directory;
};
