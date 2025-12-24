#ifndef MYTREEWIDGET_H
#define MYTREEWIDGET_H

#include "connectionmanager.h"

#include <QTreeWidget>

class MyTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit MyTreeWidget(QWidget *parent = nullptr);

    enum TableAction {
        ViewTableData = 1,
        ViewTableStructure
    };
    Q_ENUM(TableAction)

signals:
    void openQueryRequested(const QString &connName, const QString &dbName);
    void tableActionRequested(const QString &connName,
                              const QString &dbName,
                              const QString &tableName,
                              TableAction action);
    void connectionEditRequested(const QString &connName);
    void connectionDeleteRequested(const QString &connName);
    void connectionTestRequested(const QString &connName);
    void dataSyncRequested(const QString &connName,
                           const QString &dbName,
                           const QString &tableName);
    void dataImportRequested(const QString &connName,
                             const QString &dbName,
                             const QString &tableName);

public slots:
    void refreshConnections();

private:
    enum TreeNodeType {
        ConnectionNode = 1,
        DatabaseNode,
        TableNode
    };
    enum DataRole {
        NameRole = Qt::UserRole,
        TypeRole,
        ConnectionRole,
        DatabaseRole,
        LoadedRole
    };

    void rebuildTree();
    void populateDatabases(QTreeWidgetItem *connItem, const ConnectionInfo &info);
    void ensureTablesLoaded(QTreeWidgetItem *dbItem);
    void handleDoubleClick(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &pos);

    QList<ConnectionInfo> m_connections;
};

#endif // MYTREEWIDGET_H
