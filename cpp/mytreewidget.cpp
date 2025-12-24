#include "mytreewidget.h"
#include "languagemanager.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QTextStream>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

MyTreeWidget::MyTreeWidget(QWidget *parent) : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setExpandsOnDoubleClick(true);
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        handleDoubleClick(item);
    });
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        ensureTablesLoaded(item);
    });
    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        ensureTablesLoaded(current);
    });
    connect(this, &QWidget::customContextMenuRequested, this, &MyTreeWidget::showContextMenu);

    refreshConnections();
    connect(ConnectionManager::instance(), &ConnectionManager::connectionsChanged,
            this, &MyTreeWidget::refreshConnections);
    connect(LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        refreshConnections();
    });
}

void MyTreeWidget::refreshConnections()
{
    m_connections = ConnectionManager::instance()->connections();
    rebuildTree();
}

void MyTreeWidget::rebuildTree()
{
    clear();
    const QIcon connIcon(QStringLiteral(":/images/connection.svg"));
    for(const auto &info : std::as_const(m_connections)){
        auto *connItem = new QTreeWidgetItem(QStringList(info.name));
        connItem->setIcon(0, connIcon);
        connItem->setData(0, NameRole, info.name);
        connItem->setData(0, TypeRole, ConnectionNode);
        connItem->setData(0, ConnectionRole, info.name);
        addTopLevelItem(connItem);
        populateDatabases(connItem, info);
    }
}

void MyTreeWidget::populateDatabases(QTreeWidgetItem *connItem, const ConnectionInfo &info)
{
    connItem->takeChildren();
    const QIcon dbIcon(QStringLiteral(":/images/database.svg"));
    QStringList dbs = ConnectionManager::instance()->fetchDatabases(info, nullptr);
    if(dbs.isEmpty() && !info.defaultDb.isEmpty()){
        dbs << info.defaultDb;
    }
    if(dbs.isEmpty()){
        auto *placeholder = new QTreeWidgetItem(QStringList(
                                                    trLang(QStringLiteral("[无可用数据库]"),
                                                           QStringLiteral("[No databases]"))));
        placeholder->setData(0, TypeRole, DatabaseNode);
        placeholder->setData(0, ConnectionRole, info.name);
        placeholder->setData(0, DatabaseRole, QString());
        placeholder->setData(0, LoadedRole, true);
        connItem->addChild(placeholder);
        return;
    }
    for(const auto &db : dbs){
        auto *child = new QTreeWidgetItem(QStringList(db));
        child->setIcon(0, dbIcon);
        child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        child->setData(0, NameRole, db);
        child->setData(0, TypeRole, DatabaseNode);
        child->setData(0, ConnectionRole, info.name);
        child->setData(0, DatabaseRole, db);
        child->setData(0, LoadedRole, false);
        connItem->addChild(child);
    }
}

void MyTreeWidget::handleDoubleClick(QTreeWidgetItem *item)
{
    if(!item){
        return;
    }
    const int type = item->data(0, TypeRole).toInt();
    QString connName;
    QString dbName;
    switch(type){
    case ConnectionNode:
        connName = item->data(0, ConnectionRole).toString();
        if(item->childCount() > 0){
            dbName = item->child(0)->data(0, NameRole).toString();
        }
        break;
    case DatabaseNode:
        connName = item->data(0, ConnectionRole).toString();
        dbName = item->data(0, DatabaseRole).toString();
        ensureTablesLoaded(item);
        break;
    case TableNode:
        connName = item->data(0, ConnectionRole).toString();
        dbName = item->data(0, DatabaseRole).toString();
        if(connName.isEmpty()){
            return;
        }
        {
            const QString tableName = item->data(0, NameRole).toString();
            if(tableName.isEmpty()){
                return;
            }
            emit tableActionRequested(connName, dbName, tableName, ViewTableData);
        }
        return;
    default:
        break;
    }
    if(connName.isEmpty()){
        return;
    }
    emit openQueryRequested(connName, dbName);
}

void MyTreeWidget::ensureTablesLoaded(QTreeWidgetItem *dbItem)
{
    if(!dbItem){
        return;
    }
    if(dbItem->data(0, TypeRole).toInt() != DatabaseNode){
        return;
    }
    if(dbItem->data(0, LoadedRole).toBool()){
        return;
    }
    const QString connName = dbItem->data(0, ConnectionRole).toString();
    const QString dbName = dbItem->data(0, DatabaseRole).toString();
    if(connName.isEmpty() || dbName.isEmpty()){
        return;
    }

    const ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    if(info.name.isEmpty()){
        return;
    }

    const QIcon tableIcon(QStringLiteral(":/images/table.svg"));
    QString errorMessage;
    const QStringList tables = ConnectionManager::instance()->fetchTables(info, dbName, &errorMessage);
    dbItem->takeChildren();
    if(!tables.isEmpty()){
        for(const auto &table : tables){
            auto *tableItem = new QTreeWidgetItem(QStringList(table));
            tableItem->setIcon(0, tableIcon);
            tableItem->setData(0, NameRole, table);
            tableItem->setData(0, TypeRole, TableNode);
            tableItem->setData(0, ConnectionRole, connName);
            tableItem->setData(0, DatabaseRole, dbName);
            tableItem->setData(0, LoadedRole, true);
            dbItem->addChild(tableItem);
        }
    }else if(!errorMessage.isEmpty()){
        auto *errorItem = new QTreeWidgetItem(QStringList(
                                                   trLang(QStringLiteral("[失败：%1]"),
                                                          QStringLiteral("[Failed: %1]")).arg(errorMessage)));
        errorItem->setData(0, TypeRole, TableNode);
        errorItem->setData(0, ConnectionRole, connName);
        errorItem->setData(0, DatabaseRole, dbName);
        errorItem->setData(0, LoadedRole, true);
        dbItem->addChild(errorItem);
    }else{
        auto *emptyItem = new QTreeWidgetItem(QStringList(
                                                   trLang(QStringLiteral("[无数据表]"),
                                                          QStringLiteral("[No tables]"))));
        emptyItem->setData(0, TypeRole, TableNode);
        emptyItem->setData(0, ConnectionRole, connName);
        emptyItem->setData(0, DatabaseRole, dbName);
        emptyItem->setData(0, LoadedRole, true);
        dbItem->addChild(emptyItem);
    }
    dbItem->setData(0, LoadedRole, true);
    dbItem->setExpanded(true);
}

void MyTreeWidget::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = itemAt(pos);
    if(!item){
        return;
    }
    const int nodeType = item->data(0, TypeRole).toInt();
    auto showComingSoon = [this](const QString &label) {
        QMessageBox::information(this,
                                 trLang(QStringLiteral("提示"), QStringLiteral("Info")),
                                 trLang(QStringLiteral("\"%1\" 功能正在持续完善中。"),
                                        QStringLiteral("\"%1\" is under continuous development.")).arg(label));
    };
    if(nodeType == ConnectionNode){
        const QString connName = item->data(0, ConnectionRole).toString();
        if(connName.isEmpty()){
            return;
        }
        const ConnectionInfo info = ConnectionManager::instance()->connection(connName);
        QMenu menu(this);
        QAction *openQueryAction = menu.addAction(trLang(QStringLiteral("新建查询"),
                                                         QStringLiteral("New Query")));
        QAction *editConnAction = menu.addAction(trLang(QStringLiteral("编辑连接..."),
                                                        QStringLiteral("Edit Connection...")));
        QAction *testConnAction = menu.addAction(trLang(QStringLiteral("测试连接"),
                                                        QStringLiteral("Test Connection")));
        QAction *deleteConnAction = menu.addAction(trLang(QStringLiteral("删除连接"),
                                                          QStringLiteral("Delete Connection")));
        menu.addSeparator();
        QAction *createDbAction = menu.addAction(trLang(QStringLiteral("新建数据库..."),
                                                        QStringLiteral("Create Database...")));
        menu.addSeparator();
        QAction *refreshConnAction = menu.addAction(trLang(QStringLiteral("刷新"),
                                                           QStringLiteral("Refresh")));
        QAction *selected = menu.exec(viewport()->mapToGlobal(pos));
        if(!selected){
            return;
        }
        if(selected == openQueryAction){
            emit openQueryRequested(connName, info.defaultDb);
            return;
        }
        if(selected == editConnAction){
            emit connectionEditRequested(connName);
            return;
        }
        if(selected == testConnAction){
            emit connectionTestRequested(connName);
            return;
        }
        if(selected == deleteConnAction){
            emit connectionDeleteRequested(connName);
            return;
        }
        if(selected == refreshConnAction){
            if(!info.name.isEmpty()){
                populateDatabases(item, info);
                item->setExpanded(true);
            }
            return;
        }
        if(selected == createDbAction){
            QDialog dlg(this);
            dlg.setWindowTitle(trLang(QStringLiteral("新建数据库"), QStringLiteral("Create Database")));
            auto *dlgLayout = new QVBoxLayout(&dlg);
            auto *label = new QLabel(trLang(QStringLiteral("数据库名称:"), QStringLiteral("Database name:")), &dlg);
            auto *lineEdit = new QLineEdit(&dlg);
            lineEdit->setMinimumSize(300, 36);
            lineEdit->setStyleSheet(QStringLiteral("padding: 6px 4px;"));
            auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            dlgLayout->addWidget(label);
            dlgLayout->addWidget(lineEdit);
            dlgLayout->addWidget(btnBox);
            if(dlg.exec() != QDialog::Accepted || lineEdit->text().trimmed().isEmpty()){
                return;
            }
            const QString newDbName = lineEdit->text().trimmed();
            const QString handle = QStringLiteral("createdb_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
            QString errorText;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
                db.setHostName(info.host);
                db.setPort(info.port);
                db.setUserName(info.user);
                db.setPassword(info.password);
                if(!db.open()){
                    errorText = db.lastError().text();
                }else{
                    QSqlQuery query(db);
                    const QString sql = QStringLiteral("CREATE DATABASE `%1`").arg(newDbName.trimmed());
                    if(!query.exec(sql)){
                        errorText = query.lastError().text();
                    }
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(handle);
            if(!errorText.isEmpty()){
                QMessageBox::warning(this,
                    trLang(QStringLiteral("新建数据库"), QStringLiteral("Create Database")),
                    trLang(QStringLiteral("创建数据库失败: %1"), QStringLiteral("Failed to create database: %1")).arg(errorText));
            }else{
                populateDatabases(item, info);
                item->setExpanded(true);
            }
            return;
        }
        return;
    }
    if(nodeType == DatabaseNode){
        const QString connName = item->data(0, ConnectionRole).toString();
        const QString dbName = item->data(0, DatabaseRole).toString();
        if(connName.isEmpty() || dbName.isEmpty()){
            return;
        }
        const ConnectionInfo info = ConnectionManager::instance()->connection(connName);
        if(info.name.isEmpty()){
            QMessageBox::information(this,
                                     trLang(QStringLiteral("提示"), QStringLiteral("Info")),
                                     trLang(QStringLiteral("连接 %1 已不存在，列表将刷新。"),
                                            QStringLiteral("Connection %1 no longer exists, refreshing list.")).arg(connName));
            refreshConnections();
            return;
        }
        auto executeStatement = [&](const QString &sql, QString *errorMessage) -> bool {
            bool ok = false;
            QString err;
            const QString handle = QStringLiteral("dbop_%1_%2")
                    .arg(connName,
                         QUuid::createUuid().toString(QUuid::Id128));
            {
                QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
                db.setHostName(info.host);
                db.setPort(info.port);
                db.setUserName(info.user);
                db.setPassword(info.password);
                if(!db.open()){
                    err = db.lastError().text();
                }else{
                    QSqlQuery query(db);
                    if(query.exec(sql)){
                        ok = true;
                    }else{
                        err = query.lastError().text();
                    }
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(handle);
            if(!ok && errorMessage){
                *errorMessage = err;
            }
            return ok;
        };

        QMenu menu(this);
        QAction *closeDbAction = menu.addAction(trLang(QStringLiteral("关闭数据库"),
                                                       QStringLiteral("Close Database")));
        QAction *manageObjectsAction = menu.addAction(trLang(QStringLiteral("数据库对象管理"),
                                                             QStringLiteral("Database Object Management")));
        QAction *newQueryAction = menu.addAction(trLang(QStringLiteral("新建查询"),
                                                        QStringLiteral("New Query")));
        menu.addSeparator();
        QAction *editDbAction = menu.addAction(trLang(QStringLiteral("编辑数据库"),
                                                      QStringLiteral("Edit Database")));
        QAction *createDbAction = menu.addAction(trLang(QStringLiteral("新建数据库"),
                                                        QStringLiteral("Create Database")));
        QAction *deleteDbAction = menu.addAction(trLang(QStringLiteral("删除数据库"),
                                                        QStringLiteral("Delete Database")));
        menu.addSeparator();
        QAction *importAction = menu.addAction(trLang(QStringLiteral("导入..."),
                                                      QStringLiteral("Import...")));
        QAction *exportAction = menu.addAction(trLang(QStringLiteral("导出..."),
                                                      QStringLiteral("Export...")));
        menu.addSeparator();
        QAction *syncDataAction = menu.addAction(trLang(QStringLiteral("数据同步..."),
                                                        QStringLiteral("Data Synchronization...")));
        QAction *syncSchemaAction = menu.addAction(trLang(QStringLiteral("结构同步..."),
                                                          QStringLiteral("Structure Synchronization...")));
        menu.addSeparator();
        QAction *findAction = menu.addAction(trLang(QStringLiteral("在对象中查找..."),
                                                    QStringLiteral("Find in Objects...")));
        QMenu *maintainMenu = menu.addMenu(trLang(QStringLiteral("维护"),
                                                  QStringLiteral("Maintenance")));
        QAction *maintainAnalyze = maintainMenu->addAction(trLang(QStringLiteral("分析表..."),
                                                                  QStringLiteral("Analyze Table...")));
        QAction *maintainOptimize = maintainMenu->addAction(trLang(QStringLiteral("优化表..."),
                                                                   QStringLiteral("Optimize Table...")));
        menu.addSeparator();
        QAction *toolsLabel = menu.addAction(trLang(QStringLiteral("工具"), QStringLiteral("Tools")));
        toolsLabel->setEnabled(false);
        menu.addSeparator();
        QAction *collapseAction = menu.addAction(trLang(QStringLiteral("折叠\tCtrl+Shift+C"),
                                                        QStringLiteral("Collapse\tCtrl+Shift+C")));
        QAction *refreshAction = menu.addAction(trLang(QStringLiteral("刷新"),
                                                       QStringLiteral("Refresh")));

        QAction *selected = menu.exec(viewport()->mapToGlobal(pos));
        if(!selected){
            return;
        }
        if(selected == newQueryAction){
            emit openQueryRequested(connName, dbName);
            return;
        }
        if(selected == syncDataAction){
            emit dataSyncRequested(connName, dbName, QString());
            return;
        }
        if(selected == closeDbAction){
            item->setExpanded(false);
            item->takeChildren();
            item->setData(0, LoadedRole, false);
            return;
        }
        if(selected == manageObjectsAction){
            showComingSoon(selected->text());
            return;
        }
        if(selected == editDbAction){
            QMessageBox::information(this,
                                     trLang(QStringLiteral("编辑数据库"), QStringLiteral("Edit Database")),
                                     trLang(QStringLiteral("当前版本暂不支持直接重命名或编辑数据库属性。"),
                                            QStringLiteral("The current version does not support renaming or editing database properties.")));
            return;
        }
        if(selected == createDbAction){
            QDialog dlg(this);
            dlg.setWindowTitle(trLang(QStringLiteral("新建数据库"), QStringLiteral("Create Database")));
            auto *dlgLayout = new QVBoxLayout(&dlg);
            auto *label = new QLabel(trLang(QStringLiteral("数据库名称:"), QStringLiteral("Database name:")), &dlg);
            auto *lineEdit = new QLineEdit(&dlg);
            lineEdit->setMinimumSize(300, 36);
            lineEdit->setStyleSheet(QStringLiteral("padding: 6px 4px;"));
            auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            dlgLayout->addWidget(label);
            dlgLayout->addWidget(lineEdit);
            dlgLayout->addWidget(btnBox);
            if(dlg.exec() != QDialog::Accepted || lineEdit->text().trimmed().isEmpty()){
                return;
            }
            const QString name = lineEdit->text().trimmed();
            QString escaped = name;
            escaped.replace(QLatin1Char('`'), QStringLiteral("``"));
            QString errorMessage;
            if(executeStatement(QStringLiteral("CREATE DATABASE `%1`").arg(escaped), &errorMessage)){
                QMessageBox::information(this,
                                         trLang(QStringLiteral("新建数据库"), QStringLiteral("Create Database")),
                                         trLang(QStringLiteral("数据库 \"%1\" 创建完成。"),
                                                QStringLiteral("Database \"%1\" has been created.")).arg(name));
                if(auto *connItem = item->parent()){
                    populateDatabases(connItem, info);
                    connItem->setExpanded(true);
                }else{
                    refreshConnections();
                }
            }else{
                QMessageBox::warning(this,
                                     trLang(QStringLiteral("新建数据库"), QStringLiteral("Create Database")),
                                     errorMessage.isEmpty()
                                         ? trLang(QStringLiteral("执行失败。"), QStringLiteral("Execution failed."))
                                         : trLang(QStringLiteral("执行失败：%1"),
                                                  QStringLiteral("Execution failed: %1")).arg(errorMessage));
            }
            return;
        }
        if(selected == importAction || selected == exportAction ||
                selected == syncSchemaAction || selected == findAction ||
                selected == maintainAnalyze || selected == maintainOptimize){
            showComingSoon(selected->text());
            return;
        }
        if(selected == collapseAction){
            item->setExpanded(false);
            return;
        }
        if(selected == refreshAction){
            item->setData(0, LoadedRole, false);
            ensureTablesLoaded(item);
            return;
        }
        if(selected == deleteDbAction){
            const auto reply = QMessageBox::question(this,
                                                     trLang(QStringLiteral("删除数据库"), QStringLiteral("Delete Database")),
                                                     trLang(QStringLiteral("确定要删除数据库 \"%1\" 吗？\n该操作不可撤销。"),
                                                            QStringLiteral("Delete database \"%1\"?\nThis action cannot be undone.")).arg(dbName),
                                                     QMessageBox::Yes | QMessageBox::No,
                                                     QMessageBox::No);
            if(reply != QMessageBox::Yes){
                return;
            }
            const QString handle = QStringLiteral("dropdb_%1_%2")
                    .arg(connName,
                         QUuid::createUuid().toString(QUuid::Id128));
            bool dropOk = false;
            QString dropError;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
                db.setHostName(info.host);
                db.setPort(info.port);
                db.setUserName(info.user);
                db.setPassword(info.password);
                if(!db.open()){
                    dropError = db.lastError().text();
                }else{
                    QString escapedName = dbName;
                    escapedName.replace(QLatin1Char('`'), QStringLiteral("``"));
                    const QString sql = QStringLiteral("DROP DATABASE `%1`").arg(escapedName);
                    QSqlQuery query(db);
                    if(!query.exec(sql)){
                        dropError = query.lastError().text();
                    }else{
                        dropOk = true;
                    }
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(handle);
            if(!dropOk){
                QMessageBox::warning(this,
                                     trLang(QStringLiteral("删除数据库"), QStringLiteral("Delete Database")),
                                     dropError.isEmpty()
                                         ? trLang(QStringLiteral("无法连接到数据库服务器。"),
                                                  QStringLiteral("Unable to connect to database server."))
                                         : trLang(QStringLiteral("执行失败：%1"),
                                                  QStringLiteral("Execution failed: %1")).arg(dropError));
                return;
            }
            QMessageBox::information(this,
                                     trLang(QStringLiteral("删除数据库"), QStringLiteral("Delete Database")),
                                     trLang(QStringLiteral("数据库 \"%1\" 已删除。"),
                                            QStringLiteral("Database \"%1\" has been deleted.")).arg(dbName));
            if(auto *connItem = item->parent()){
                populateDatabases(connItem, info);
                connItem->setExpanded(true);
            }else{
                refreshConnections();
            }
            return;
        }
        showComingSoon(selected->text());
        return;
    }
    if(nodeType != TableNode){
        return;
    }
    const QString connName = item->data(0, ConnectionRole).toString();
    const QString dbName = item->data(0, DatabaseRole).toString();
    const QString tableName = item->data(0, NameRole).toString();
    if(connName.isEmpty() || tableName.isEmpty()){
        return;
    }

    QMenu menu(this);
    QAction *designAction = menu.addAction(trLang(QStringLiteral("设计"),
                                                  QStringLiteral("Design")));
    QAction *editDataAction = menu.addAction(trLang(QStringLiteral("编辑数据"),
                                                    QStringLiteral("Edit Data")));
    menu.addSeparator();
    QAction *createTableAction = menu.addAction(trLang(QStringLiteral("新建表"),
                                                       QStringLiteral("Create Table")));
    QAction *copyTableAction = menu.addAction(trLang(QStringLiteral("复制表"),
                                                     QStringLiteral("Copy Table")));
    QAction *newQueryAction = menu.addAction(trLang(QStringLiteral("新建查询"),
                                                    QStringLiteral("New Query")));
    menu.addSeparator();
    QAction *importAction = menu.addAction(trLang(QStringLiteral("导入..."),
                                                  QStringLiteral("Import...")));
    QAction *exportAction = menu.addAction(trLang(QStringLiteral("导出..."),
                                                  QStringLiteral("Export...")));
    QAction *syncDataAction = menu.addAction(trLang(QStringLiteral("数据同步..."),
                                                    QStringLiteral("Data Synchronization...")));
    QAction *syncSchemaAction = menu.addAction(trLang(QStringLiteral("结构同步..."),
                                                      QStringLiteral("Structure Synchronization...")));
    QAction *findObjectAction = menu.addAction(trLang(QStringLiteral("在对象中查找..."),
                                                      QStringLiteral("Find in Objects...")));
    QAction *generateSqlAction = menu.addAction(trLang(QStringLiteral("生成 SQL"),
                                                       QStringLiteral("Generate SQL")));
    QAction *maintainAction = menu.addAction(trLang(QStringLiteral("维护"),
                                                    QStringLiteral("Maintenance")));
    menu.addSeparator();
    QAction *toolsLabel = menu.addAction(trLang(QStringLiteral("工具"), QStringLiteral("Tools")));
    toolsLabel->setEnabled(false);
    QAction *emptyAction = menu.addAction(trLang(QStringLiteral("清空表"),
                                                 QStringLiteral("Empty Table")));
    QAction *truncateAction = menu.addAction(trLang(QStringLiteral("截断表"),
                                                    QStringLiteral("Truncate Table")));
    QAction *renameAction = menu.addAction(trLang(QStringLiteral("重命名"),
                                                  QStringLiteral("Rename")));
    QAction *dropAction = menu.addAction(trLang(QStringLiteral("删除表"),
                                                QStringLiteral("Drop Table")));
    menu.addSeparator();
    QAction *collapseAction = menu.addAction(trLang(QStringLiteral("折叠\tCtrl+Shift+C"),
                                                    QStringLiteral("Collapse\tCtrl+Shift+C")));
    QAction *refreshAction = menu.addAction(trLang(QStringLiteral("刷新"),
                                                   QStringLiteral("Refresh")));

    QAction *selected = menu.exec(viewport()->mapToGlobal(pos));
    if(!selected){
        return;
    }
    if(selected == designAction){
        emit tableActionRequested(connName, dbName, tableName, ViewTableStructure);
        return;
    }
    if(selected == editDataAction){
        emit tableActionRequested(connName, dbName, tableName, ViewTableData);
        return;
    }
    if(selected == syncDataAction){
        emit dataSyncRequested(connName, dbName, tableName);
        return;
    }
    if(selected == importAction){
        emit dataImportRequested(connName, dbName, tableName);
        return;
    }
    if(selected == collapseAction){
        if(auto *parentItem = item->parent()){
            parentItem->setExpanded(false);
        }
        return;
    }
    if(selected == refreshAction){
        auto *dbItem = item->parent();
        if(dbItem){
            dbItem->setData(0, LoadedRole, false);
            ensureTablesLoaded(dbItem);
        }
        return;
    }

    if(selected == createTableAction || selected == copyTableAction ||
            selected == newQueryAction || selected == exportAction ||
            selected == syncSchemaAction || selected == findObjectAction ||
            selected == generateSqlAction || selected == maintainAction ||
            selected == emptyAction || selected == truncateAction ||
            selected == renameAction || selected == dropAction){
        showComingSoon(selected->text());
    }
}
