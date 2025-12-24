#ifndef IMPORTDIALOG_H
#define IMPORTDIALOG_H

#include "connectionmanager.h"

#include <QDialog>
#include <QVector>
#include <QStringList>

class QTabWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QSqlDatabase;

struct ImportOptions {
    QString filePath;
    QString format;      // csv/tsv
    QString encoding;
    QString delimiter;
    QString qualifier;
    bool hasHeader = true;
    int startRow = 1;
    int batchSize = 500;
    bool truncateBefore = false;
    bool ignoreErrors = false;
};

class ImportDialog : public QDialog
{
    Q_OBJECT
public:
    ImportDialog(const ConnectionInfo &info,
                 const QString &database,
                 const QString &table,
                 QWidget *parent = nullptr);
    ~ImportDialog() override;

private slots:
    void browseFile();
    void reloadPreview();
    void autoMapColumns();
    void clearMapping();
    void startImport();

private:
    struct TableColumn {
        QString name;
        QString type;
        bool nullable = true;
    };

    void buildUi();
    QWidget *createGeneralPage();
    QWidget *createMappingPage();
    QWidget *createLogPage();
    void appendLog(const QString &message);
    void setRunning(bool running);
    bool loadTargetColumns();
    void rebuildMappingTable();
    bool loadPreviewFromFile(QStringList *headers, QList<QStringList> *samples);
    QStringList parseLine(const QString &line) const;
    ImportOptions gatherOptions() const;
    bool openDatabase(QSqlDatabase *db, QString *errorMessage) const;
    bool truncateTarget(QSqlDatabase &db, QString *errorMessage) const;
    bool runImport(const ImportOptions &options);
    QVector<int> currentMapping() const;

    ConnectionInfo m_connection;
    QString m_databaseName;
    QString m_tableName;
    QVector<TableColumn> m_columns;
    QStringList m_sourceHeaders;

    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_fileEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_encodingCombo = nullptr;
    QLineEdit *m_delimiterEdit = nullptr;
    QLineEdit *m_qualifierEdit = nullptr;
    QCheckBox *m_headerCheck = nullptr;
    QSpinBox *m_startRowSpin = nullptr;
    QSpinBox *m_batchSpin = nullptr;
    QCheckBox *m_truncateCheck = nullptr;
    QCheckBox *m_ignoreErrorsCheck = nullptr;
    QTableWidget *m_mappingTable = nullptr;
    QVector<QComboBox*> m_mappingCombos;
    QPlainTextEdit *m_logEdit = nullptr;
    QPushButton *m_startButton = nullptr;
};

#endif // IMPORTDIALOG_H
