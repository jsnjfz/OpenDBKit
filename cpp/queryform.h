#ifndef QUERYFORM_H
#define QUERYFORM_H

#include "connectionmanager.h"
#include "myedit.h"
#include "resultform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QToolButton>
#include <QTabWidget>
#include <QWidget>
#include <QMap>
#include <QTableWidget>
#include <QPushButton>
#include <QHash>

class FlowLayout;
class QSqlQuery;
class QPlainTextEdit;
class QSqlDatabase;

class QueryForm : public QWidget
{
    Q_OBJECT
public:
    enum Mode {
        QueryMode,
        InspectMode
    };

    enum TableAction {
        NoneAction = 0,
        ViewStructure = 1,
        ViewData
    };
    Q_ENUM(TableAction)

    explicit QueryForm(QWidget *parent = nullptr, Mode mode = QueryMode, TableAction fixedAction = NoneAction);
    ~QueryForm() override;

    QString title() const;
    bool isModified() const;
    QString filePath() const;
    QString codecName() const;

    Mode mode() const { return m_mode; }
    bool isInspectTab() const { return m_mode == InspectMode; }
    QString inspectConnectionName() const { return inspectConn; }
    QString inspectDatabaseName() const { return inspectDb; }
    QString inspectTableName() const { return inspectTable; }

    void setConnection(const QString &connName, const QString &dbName);
    bool loadFromFile(const QString &filePath, const QByteArray &codec = QByteArray());
    bool saveToFile(const QString &filePath, const QByteArray &codec = QByteArray());

    MyEdit *editor() const;
    void openInspectTab(const QString &connName,
                        const QString &dbName,
                        const QString &tableName,
                        TableAction action);

signals:
    void titleChanged(const QString &title);
    void modifiedStateChanged(bool modified);
    void requestStatusMessage(const QString &text, int timeout);

private slots:
    void runQuery();
    void stopQuery();
    void formatSql();
    void updateTitleFromEditor();

private:
    struct RowEditState {
        QString rowId;
        QStringList originalValues;
        QStringList currentValues;
        QVector<bool> currentNullFlags;
        bool inserted = false;
        bool deleted = false;
        bool updated = false;
    };

    struct InspectPane {
        QString connName;
        QString dbName;
        QString tableName;
        TableAction currentAction = NoneAction;
        QWidget *tabWidget = nullptr;
        QWidget *widget = nullptr;
        QLabel *titleLabel = nullptr;
        QLabel *subtitleLabel = nullptr;
        QToolButton *tabButton = nullptr;
        QToolButton *tabCloseButton = nullptr;
        QToolButton *viewDataButton = nullptr;
        QToolButton *viewStructureButton = nullptr;
        QLineEdit *filterEdit = nullptr;
        QToolButton *whereSearchButton = nullptr;
        MyEdit *whereEdit = nullptr;
        QPushButton *whereApplyButton = nullptr;
        QPushButton *whereClearButton = nullptr;
        QComboBox *sortCombo = nullptr;
        QToolButton *sortAscButton = nullptr;
        QToolButton *sortDescButton = nullptr;
        QToolButton *fetchFirstButton = nullptr;
        QToolButton *fetchNextButton = nullptr;
        QToolButton *fetchAllButton = nullptr;
        QToolButton *fetchLastButton = nullptr;
        QLineEdit *pageEdit = nullptr;
        QToolButton *refreshButton = nullptr;
        QToolButton *addRowButton = nullptr;
        QToolButton *duplicateRowButton = nullptr;
        QToolButton *deleteRowButton = nullptr;
        QToolButton *saveRowsButton = nullptr;
        QToolButton *discardRowsButton = nullptr;
        ResultForm *resultForm = nullptr;
        QTableWidget *indexTable = nullptr;
        QToolButton *indexAddButton = nullptr;
        QToolButton *indexDeleteButton = nullptr;
        QPushButton *indexSaveButton = nullptr;
        QPushButton *indexRefreshButton = nullptr;
        QPushButton *indexCloseButton = nullptr;
        bool indexDirty = false;
        bool indexBlockSignals = false;
        QMap<QString, QStringList> indexOriginalData;
        QStringList indexPendingSql;
        ResultForm *foreignResult = nullptr;
        QToolButton *foreignAddButton = nullptr;
        QToolButton *foreignDeleteButton = nullptr;
        ResultForm *triggerResult = nullptr;
        QToolButton *triggerAddButton = nullptr;
        QToolButton *triggerDeleteButton = nullptr;
        QTabWidget *structureTabs = nullptr;
        QStackedWidget *viewStack = nullptr;
        QWidget *dataPage = nullptr;
        QWidget *structurePage = nullptr;
        QLineEdit *structureFilterEdit = nullptr;
        QLineEdit *structureTableNameEdit = nullptr;
        QLineEdit *structureDatabaseEdit = nullptr;
        QLineEdit *structureCommentEdit = nullptr;
        QTableWidget *structureTable = nullptr;
        QToolButton *structureAddButton = nullptr;
        QToolButton *structureRemoveButton = nullptr;
        QToolButton *structureUpButton = nullptr;
        QToolButton *structureDownButton = nullptr;
        QPushButton *structureSaveButton = nullptr;
        QPushButton *structureReloadButton = nullptr;
        QPushButton *structureCloseButton = nullptr;
        QLineEdit *optionEngineEdit = nullptr;
        QLineEdit *optionRowFormatEdit = nullptr;
        QLineEdit *optionCharsetEdit = nullptr;
        QLineEdit *optionCollationEdit = nullptr;
        QLineEdit *optionAutoIncrementEdit = nullptr;
        QLineEdit *optionAvgRowLengthEdit = nullptr;
        QLineEdit *optionTableRowsEdit = nullptr;
        QLineEdit *optionDataLengthEdit = nullptr;
        QLineEdit *optionDataFreeEdit = nullptr;
        QLineEdit *optionIndexLengthEdit = nullptr;
        QLineEdit *optionMaxRowCountEdit = nullptr;
        QLineEdit *optionCreateTimeEdit = nullptr;
        QLineEdit *optionUpdateTimeEdit = nullptr;
        QPlainTextEdit *ddlEditor = nullptr;
        QPlainTextEdit *sqlPreviewEditor = nullptr;
        QList<ResultForm::ColumnInfo> structureOriginalColumns;
        QList<ResultForm::ColumnInfo> structureWorkingColumns;
        QStringList structurePendingSql;
        bool structureDirty = false;
        bool structureBlockSignals = false;
        bool dataDirty = false;
        bool blockDataSignal = false;
        QStringList dataHeaders;
        QHash<QString, int> dataHeaderIndex;
        QStringList dataPrimaryKeys;
        QHash<QString, RowEditState> dataRowStates;
        int dataOffset = 0;
        int dataLimit = 100;
        bool hasMoreData = false;
        QString whereClause;
    };

    void initialiseUi();
    QWidget *buildQueryPage();
    QWidget *buildInspectPage();
    InspectPane *createInspectPane(const QString &connName,
                                   const QString &dbName,
                                   const QString &tableName,
                                   TableAction action);
    InspectPane *findInspectPane(const QString &connName,
                                 const QString &dbName,
                                 const QString &tableName) const;
    void selectInspectPane(InspectPane *pane);
    void populateConnections();
    void updateDatabaseList();
    void updateCompletionList();
    void showSampleResult();
    ConnectionInfo currentConnectionInfo() const;
    void showStatus(const QString &text, int timeout = 3000);
    QList<ResultForm::ColumnInfo> parseTableStructure(QSqlQuery &query) const;
    void enterInspectMode(const QString &connName,
                          const QString &dbName,
                          const QString &tableName,
                          TableAction action);
    void exitInspectMode();
    void refreshInspectData(InspectPane *pane);
    void fetchFirst(InspectPane *pane);
    void fetchNext(InspectPane *pane);
    void fetchAll(InspectPane *pane);
    void fetchLast(InspectPane *pane);
    void updateFetchButtons(InspectPane *pane);
    void changeInspectView(InspectPane *pane, TableAction action);
    void updateInspectSortOptions(InspectPane *pane);
    void applyInspectSort(InspectPane *pane, Qt::SortOrder order);
    void removeInspectPane(InspectPane *pane);
    void showInspectTabContextMenu(InspectPane *pane, const QPoint &globalPos);
    void closeInspectPane(InspectPane *pane);
    void closeOtherInspectTabs(InspectPane *pane);
    bool closeAllInspectTabs();
    void updateInspectPlaceholder();
    void prepareInspectOnlyUi();
    void refreshInspectStructure(InspectPane *pane);
    void updateInspectView(InspectPane *pane);
    void fillOptionsTab(InspectPane *pane, const QMap<QString, QString> &statusData);
    void updateSqlPreviewPane(InspectPane *pane, const QString &dbName);
    void showIndexInfo(InspectPane *pane, QSqlDatabase &db, const QString &dbName);
    void showForeignKeys(InspectPane *pane, QSqlDatabase &db, const QString &dbName);
    void showTriggers(InspectPane *pane, QSqlDatabase &db, const QString &dbName);
    void showDdlInfo(InspectPane *pane, QSqlDatabase &db);
    void handleStructureAdd(InspectPane *pane);
    void handleStructureRemove(InspectPane *pane);
    void handleStructureMove(InspectPane *pane, bool moveUp);
    void updateStructureButtons(InspectPane *pane);
    void handleIndexAdd(InspectPane *pane);
    void handleIndexDelete(InspectPane *pane);
    void showIndexColumnDialog(InspectPane *pane, int row);
    void populateIndexTable(InspectPane *pane);
    void saveIndexChanges(InspectPane *pane);
    void updateIndexDirtyState(InspectPane *pane);
    bool executeInspectSql(const QString &connName, const QString &dbName,
                           const QString &sql, QString *errorMessage);
    QString buildColumnDefinition(const ResultForm::ColumnInfo &info) const;
    QString escapeSqlValue(const QString &value) const;
    int selectedStructureRow(const InspectPane *pane) const;
    void rebuildStructureTable(InspectPane *pane);
    void applyStructureFilter(InspectPane *pane);
    QStringList generateStructureSqlStatements(InspectPane *pane) const;
    void updateStructureDirtyState(InspectPane *pane);
    bool ensureStructureChangesHandled(InspectPane *pane, bool allowCancel = true);
    bool ensureIndexChangesHandled(InspectPane *pane);
    bool saveStructureChanges(InspectPane *pane);
    void initialiseDataRows(InspectPane *pane,
                            const ConnectionInfo &info,
                            const QString &dbName,
                            const QStringList &headers,
                            const QList<QVariantList> &rows);
    void setupDataConnections(InspectPane *pane);
    void updateDataButtons(InspectPane *pane);
    void markDataDirty(InspectPane *pane);
    QString generateRowId() const;
    void tagRowWithId(InspectPane *pane, int row, const QString &rowId);
    QString rowIdForSourceRow(InspectPane *pane, int sourceRow) const;
    QStringList currentRowValues(InspectPane *pane, int sourceRow) const;
    void handleDataRowChanged(InspectPane *pane, int sourceRow);
    void addEmptyDataRow(InspectPane *pane);
    void duplicateSelectedRow(InspectPane *pane);
    void appendDataRow(InspectPane *pane, const QStringList &values, const QVector<bool> &nullFlags = {});
    void copyRowsToClipboard(InspectPane *pane);
    void pasteRowsFromClipboard(InspectPane *pane);
    void deleteSelectedRows(InspectPane *pane);
    bool saveDataChanges(InspectPane *pane);
    QString buildRowWhereClause(InspectPane *pane, const RowEditState &state, QString *errorMessage) const;
    QString buildInsertSql(InspectPane *pane, const RowEditState &state) const;
    QString buildUpdateSql(InspectPane *pane, const RowEditState &state, QString *errorMessage) const;
    QString buildDeleteSql(InspectPane *pane, const RowEditState &state, QString *errorMessage) const;
    QStringList fetchPrimaryKeys(const ConnectionInfo &info,
                                 const QString &dbName,
                                 const QString &tableName) const;
    void showDataContextMenu(InspectPane *pane, const QPoint &pos);

    QString m_title;
    Mode m_mode = QueryMode;
    TableAction m_fixedInspectAction = NoneAction;

    QComboBox *connCombo = nullptr;
    QComboBox *dbCombo = nullptr;
    QCheckBox *autoCommitCheck = nullptr;
    QToolButton *runButton = nullptr;
    QToolButton *stopButton = nullptr;
    QToolButton *formatButton = nullptr;

    MyEdit *textEdit = nullptr;
    ResultForm *resultForm = nullptr;
    QStackedWidget *pageStack = nullptr;
    QWidget *queryPage = nullptr;
    QWidget *inspectPage = nullptr;
    QWidget *inspectTabContainer = nullptr;
    FlowLayout *inspectTabFlow = nullptr;
    QStackedWidget *inspectStack = nullptr;
    QLabel *inspectPlaceholder = nullptr;
    QToolButton *inspectBackButton = nullptr;
    QPushButton *inspectCloseButton = nullptr;
    QList<InspectPane*> inspectPanes;
    bool inExecution = false;

    QString inspectConn;
    QString inspectDb;
    QString inspectTable;
};

#endif // QUERYFORM_H
