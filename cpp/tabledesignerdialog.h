#ifndef TABLEDESIGNERDIALOG_H
#define TABLEDESIGNERDIALOG_H

#include "connectionmanager.h"

#include <QDialog>
#include <QList>
#include <QStringList>

class QTabWidget;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;

class TableDesignerDialog : public QDialog
{
    Q_OBJECT
public:
    TableDesignerDialog(const ConnectionInfo &info,
                        const QString &database,
                        const QString &table,
                        QWidget *parent = nullptr);
    ~TableDesignerDialog() override;

private slots:
    void addColumn();
    void removeColumn();
    void moveColumnUp();
    void moveColumnDown();
    void refreshColumns();
    void saveChanges();
    void updateSqlPreview();

private:
    struct ColumnDefinition {
        QString name;
        QString originalName;  // Track original name for CHANGE COLUMN
        QString type;
        QString collation;     // CHARACTER SET and COLLATE info
        bool unsignedFlag = false;
        bool notNull = false;
        bool primaryKey = false;
        bool autoIncrement = false;
        QString defaultValue;
        QString comment;

        bool operator==(const ColumnDefinition &other) const;
        bool operator!=(const ColumnDefinition &other) const { return !(*this == other); }
    };

    struct IndexDefinition {
        QString name;
        QStringList columns;
        bool unique = false;
        QString type;
        QString comment;
    };

    void buildUi();
    QWidget *createColumnsPage();
    QWidget *createIndexesPage();
    QWidget *createForeignKeysPage();
    QWidget *createTriggersPage();
    QWidget *createOptionsPage();
    QWidget *createDdlPage();
    QWidget *createSqlPreviewPage();
    bool loadColumns();
    bool loadIndexes();
    bool loadCreateStatement();
    void populateColumnTable();
    void populateIndexTable();
    QList<ColumnDefinition> currentColumns() const;
    bool validateColumns(const QList<ColumnDefinition> &columns, QString *errorMessage) const;
    QString columnDefinitionSql(const ColumnDefinition &col) const;
    QString positionClause(int targetIndex, const QList<ColumnDefinition> &columns) const;
    QStringList generateAlterStatements(const QList<ColumnDefinition> &columns) const;
    bool applyStatements(const QStringList &statements);
    QString qualifiedTable() const;

    ConnectionInfo m_connection;
    QString m_databaseName;
    QString m_tableName;
    QList<ColumnDefinition> m_originalColumns;
    QList<IndexDefinition> m_indexes;
    QString m_createStatement;

    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_columnTable = nullptr;
    QTableWidget *m_indexTable = nullptr;
    QPlainTextEdit *m_sqlPreview = nullptr;
    QPlainTextEdit *m_ddlView = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    bool m_updatingTable = false;
};

#endif // TABLEDESIGNERDIALOG_H
