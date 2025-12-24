#ifndef CONNDIALOG_H
#define CONNDIALOG_H

#include "connectionmanager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

class ConnDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnDialog(QWidget *parent = nullptr, const ConnectionInfo &info = ConnectionInfo());

private slots:
    void onTestConnection();
    void onSaveConnection();

private:
    void buildUi();
    QWidget *createGeneralTab();
    QWidget *createAdvancedTab();
    QWidget *createSshTab();
    QWidget *createPropertiesTab();
    QWidget *createStartupTab();
    QWidget *createDriversTab();
    void rebuildPropertyTable();
    void rebuildDriverTable();
    void applyPropertyFilter(const QString &text);
    QList<ConnectionProperty> collectPropertiesFromTable() const;
    void addPropertyRow();
    void removeSelectedProperty();
    void resetProperties();
    void updateSshFields();
    void updateConnectionModeFields();
    void addDriverEntry();
    void deleteDriverEntry();
    void editDriverEntry();
    void browseDriverFile();
    void refreshDriverEntries();
    ConnectionInfo gatherInfo() const;
    void fillFromInfo(const ConnectionInfo &info);
    QStringList driverChoices() const;
    void browseLocalClient();
    void parseUrlIntoInfo(ConnectionInfo &info) const;

    ConnectionInfo m_originalInfo;
    QList<ConnectionProperty> m_properties;
    QList<JdbcDriverEntry> m_drivers;
    bool m_blockPropertySignals = false;

    QTabWidget *tabWidget = nullptr;
    QLineEdit *nameEdit;
    QComboBox *driverCombo;
    QRadioButton *hostRadio;
    QRadioButton *urlRadio;
    QLineEdit *hostEdit;
    QSpinBox *portSpin;
    QLineEdit *urlEdit;
    QLineEdit *userEdit;
    QLineEdit *passwordEdit;
    QCheckBox *savePasswordCheck;
    QLineEdit *databaseEdit;
    QCheckBox *prodCheck;
    QComboBox *charsetCombo;
    QLineEdit *serverTimezoneEdit;
    QLineEdit *localClientEdit;
    QCheckBox *autoSubmitCheck;
    QCheckBox *sshEnableCheck;
    QLineEdit *sshHostEdit;
    QSpinBox *sshPortSpin;
    QLineEdit *sshUserEdit;
    QLineEdit *sshPasswordEdit;
    QCheckBox *sshSavePasswordCheck;
    QTableWidget *propertyTable;
    QLineEdit *propertyFilterEdit;
    QPlainTextEdit *startupScriptEdit;
    QTableWidget *driverTable;
};

#endif // CONNDIALOG_H
