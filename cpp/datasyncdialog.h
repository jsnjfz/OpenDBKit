#ifndef DATASYNCDIALOG_H
#define DATASYNCDIALOG_H

#include "connectionmanager.h"

#include <QDialog>
#include <QObject>
#include <QVector>
#include <functional>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QPlainTextEdit;
class QCheckBox;
class QSqlDatabase;
class QThread;
class QProgressBar;

class DataSyncWorker;

struct DataSyncOptions {
    ConnectionInfo sourceInfo;
    ConnectionInfo targetInfo;
    QString sourceDbName;
    QString targetDbName;
    int batchSize = 1000;
    bool continueOnError = false;
    bool strictMode = false;
    bool emptyTarget = false;
    bool useTruncate = false;
};

class DataSyncDialog : public QDialog
{
    Q_OBJECT
public:
    struct TableMappingEntry {
        QString sourceTable;
        QString targetTable;
        bool createTable = false;
        bool enabled = false;
        QString mappingLabel = QStringLiteral("Default");
    };

    explicit DataSyncDialog(QWidget *parent = nullptr);
    ~DataSyncDialog() override;

    void setSourceContext(const QString &connName, const QString &dbName);
    void setTargetContext(const QString &connName, const QString &dbName);
    void setInitialTableHint(const QString &tableName);

private slots:
    void goNext();
    void goBack();
    void cancelDialog();
    void startSync();
    void synchronizeAll();
    void clearAllSelections();
    void editMapping();
    void applyFilter(const QString &text);
    void swapConnections();
    void onSourceConnectionChanged(int index);
    void onTargetConnectionChanged(int index);
    void onSourceDbChanged(int index);
    void onTargetDbChanged(int index);
    void handleSyncFinished(bool aborted,
                            const QString &message,
                            int successTables,
                            int failedTables,
                            qint64 totalRows);

private:

    void buildUi();
    QWidget *createSourceTargetPage();
    QWidget *createMappingPage();
    QWidget *createExecutePage();
    void retranslateUi();
    void updateNavigation();
    void loadConnections();
    void populateDatabaseCombo(QComboBox *combo, const QString &connName, const QString &preferredDb);
    void refreshMappingData();
    void rebuildMappingTable();
    void setEntryEnabled(int row, bool value);
    void setCreateFlag(int row, bool value);
    void updateCreateTableState(int row, bool value);
    void updateSummaryLabels();
    void appendLogMessage(const QString &message);
    bool ensureTargetTable(const TableMappingEntry &entry,
                           QSqlDatabase &sourceDb,
                           QSqlDatabase &targetDb,
                           const QString &sourceDbName,
                           const QString &targetDbName,
                           QString *errorMessage);
    bool clearTargetTable(const QString &targetDbName,
                          const QString &targetTable,
                          QSqlDatabase &targetDb,
                          bool useTruncate,
                          QString *errorMessage);
    bool copyTableData(const TableMappingEntry &entry,
                       QSqlDatabase &sourceDb,
                       QSqlDatabase &targetDb,
                       const QString &sourceDbName,
                       const QString &targetDbName,
                       int batchSize,
                       bool continueOnError,
                       qint64 *rowsCopied,
                       QString *errorMessage,
                       const std::function<void (const QString &)> &logCallback = {});
    void setSyncRunning(bool running);

    QStackedWidget *stack = nullptr;
    QWidget *pageSelect = nullptr;
    QWidget *pageMapping = nullptr;
    QWidget *pageExecute = nullptr;

    QComboBox *sourceConnCombo = nullptr;
    QComboBox *sourceDbCombo = nullptr;
    QComboBox *targetConnCombo = nullptr;
    QComboBox *targetDbCombo = nullptr;
    QPushButton *swapButton = nullptr;

    QLineEdit *filterEdit = nullptr;
    QTableWidget *mappingTable = nullptr;

    QSpinBox *batchSizeSpin = nullptr;
    QCheckBox *continueOnErrorCheck = nullptr;
    QCheckBox *strictModeCheck = nullptr;
    QCheckBox *emptyTargetCheck = nullptr;
    QCheckBox *truncateCheck = nullptr;
    QPlainTextEdit *logEdit = nullptr;
    QProgressBar *progressBar = nullptr;

    QPushButton *backButton = nullptr;
    QPushButton *nextButton = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *cancelButton = nullptr;

    QGroupBox *sourceGroupBox = nullptr;
    QGroupBox *targetGroupBox = nullptr;
    QLabel *sourceDatasourceLabel = nullptr;
    QLabel *sourceDatabaseLabel = nullptr;
    QLabel *targetDatasourceLabel = nullptr;
    QLabel *targetDatabaseLabel = nullptr;
    QLabel *sourceSummaryLabel = nullptr;
    QLabel *targetSummaryLabel = nullptr;
    QPushButton *syncAllButton = nullptr;
    QPushButton *clearAllButton = nullptr;
    QPushButton *editMappingButton = nullptr;
    QLabel *batchSizeLabel = nullptr;

    QVector<TableMappingEntry> mappings;
    QString sourceHintTable;
    QThread *syncThread = nullptr;
    bool syncInProgress = false;

    friend class DataSyncWorker;
};

class DataSyncWorker : public QObject
{
    Q_OBJECT
public:
    DataSyncWorker(DataSyncDialog *dialog,
                   QVector<DataSyncDialog::TableMappingEntry> tasks,
                   const DataSyncOptions &options);

public slots:
    void process();

signals:
    void logMessage(const QString &message);
    void progressChanged(int current, int total);
    void finished(bool aborted,
                  const QString &message,
                  int successTables,
                  int failedTables,
                  qint64 totalRows);

private:
    DataSyncDialog *m_dialog = nullptr;
    QVector<DataSyncDialog::TableMappingEntry> m_tasks;
    DataSyncOptions m_options;
};

#endif // DATASYNCDIALOG_H
