#include "tabledesignerdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <algorithm>
#include <QSet>
#include <QVBoxLayout>

namespace {

QString quoted(const QString &identifier)
{
    QString escaped = identifier;
    escaped.replace(QLatin1Char('`'), QStringLiteral("``"));
    return QStringLiteral("`%1`").arg(escaped);
}

QString qualifiedTableName(const QString &database, const QString &table)
{
    if(database.isEmpty()){
        return quoted(table);
    }
    return QStringLiteral("%1.%2").arg(quoted(database), quoted(table));
}

bool configureDatabase(QSqlDatabase &db,
                       const ConnectionInfo &info,
                       const QString &database,
                       QString *errorMessage)
{
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    if(!database.isEmpty()){
        db.setDatabaseName(database);
    }
    if(!db.open()){
        if(errorMessage){
            *errorMessage = db.lastError().text();
        }
        return false;
    }
    return true;
}

QString uniqueConnectionName(const QString &prefix)
{
    static std::atomic<int> counter {0};
    return QStringLiteral("%1_design_%2").arg(prefix).arg(counter.fetch_add(1));
}

}

bool TableDesignerDialog::ColumnDefinition::operator==(const ColumnDefinition &other) const
{
    return name == other.name &&
            type.compare(other.type, Qt::CaseInsensitive) == 0 &&
            collation == other.collation &&
            unsignedFlag == other.unsignedFlag &&
            notNull == other.notNull &&
            primaryKey == other.primaryKey &&
            autoIncrement == other.autoIncrement &&
            defaultValue == other.defaultValue &&
            comment == other.comment;
}

TableDesignerDialog::TableDesignerDialog(const ConnectionInfo &info,
                                         const QString &database,
                                         const QString &table,
                                         QWidget *parent)
    : QDialog(parent)
    , m_connection(info)
    , m_databaseName(database)
    , m_tableName(table)
{
    setWindowTitle(tr("Design Table - %1").arg(table));
    resize(900, 640);
    buildUi();
    refreshColumns();
}

TableDesignerDialog::~TableDesignerDialog() = default;

void TableDesignerDialog::buildUi()
{
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createColumnsPage(), tr("General"));
    m_tabs->addTab(createIndexesPage(), tr("Indexes"));
    m_tabs->addTab(createForeignKeysPage(), tr("Foreign Keys"));
    m_tabs->addTab(createTriggersPage(), tr("Triggers"));
    m_tabs->addTab(createOptionsPage(), tr("Options"));
    m_tabs->addTab(createDdlPage(), tr("DDL"));
    m_tabs->addTab(createSqlPreviewPage(), tr("SQL Preview"));

    auto *buttonBox = new QDialogButtonBox(Qt::Horizontal, this);
    m_saveButton = buttonBox->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    m_refreshButton = buttonBox->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    m_closeButton = buttonBox->addButton(tr("Close"), QDialogButtonBox::RejectRole);

    connect(m_saveButton, &QPushButton::clicked, this, &TableDesignerDialog::saveChanges);
    connect(m_refreshButton, &QPushButton::clicked, this, &TableDesignerDialog::refreshColumns);
    connect(m_closeButton, &QPushButton::clicked, this, &TableDesignerDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttonBox);
    setLayout(layout);
}

QWidget *TableDesignerDialog::createColumnsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_columnTable = new QTableWidget(page);
    m_columnTable->setColumnCount(8);
    m_columnTable->setHorizontalHeaderLabels({
        tr("Column Name"),
        tr("Type"),
        tr("Unsigned"),
        tr("Not Null"),
        tr("Primary Key"),
        tr("Auto Increment"),
        tr("Default"),
        tr("Comment")
    });
    m_columnTable->horizontalHeader()->setStretchLastSection(true);
    m_columnTable->verticalHeader()->setVisible(false);
    m_columnTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_columnTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_columnTable, 1);

    connect(m_columnTable, &QTableWidget::cellChanged, this, &TableDesignerDialog::updateSqlPreview);

    auto *buttonLayout = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Add"), page);
    auto *removeBtn = new QPushButton(tr("Delete"), page);
    auto *upBtn = new QPushButton(tr("Up"), page);
    auto *downBtn = new QPushButton(tr("Down"), page);
    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(removeBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(upBtn);
    buttonLayout->addWidget(downBtn);
    layout->addLayout(buttonLayout);

    connect(addBtn, &QPushButton::clicked, this, &TableDesignerDialog::addColumn);
    connect(removeBtn, &QPushButton::clicked, this, &TableDesignerDialog::removeColumn);
    connect(upBtn, &QPushButton::clicked, this, &TableDesignerDialog::moveColumnUp);
    connect(downBtn, &QPushButton::clicked, this, &TableDesignerDialog::moveColumnDown);

    return page;
}

QWidget *TableDesignerDialog::createIndexesPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_indexTable = new QTableWidget(page);
    m_indexTable->setColumnCount(5);
    m_indexTable->setHorizontalHeaderLabels({
        tr("Name"),
        tr("Columns"),
        tr("Type"),
        tr("Unique"),
        tr("Comment")
    });
    m_indexTable->horizontalHeader()->setStretchLastSection(true);
    m_indexTable->verticalHeader()->setVisible(false);
    m_indexTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_indexTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_indexTable, 1);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    auto *refreshBtn = new QPushButton(tr("Refresh"), page);
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        loadIndexes();
    });
    buttonLayout->addWidget(refreshBtn);
    layout->addLayout(buttonLayout);
    return page;
}

QWidget *TableDesignerDialog::createForeignKeysPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(tr("Foreign key designer is under construction."), page);
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();
    return page;
}

QWidget *TableDesignerDialog::createTriggersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(tr("Trigger editor is not available yet."), page);
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();
    return page;
}

QWidget *TableDesignerDialog::createOptionsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *label = new QLabel(tr("Table options can be configured here later."), page);
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();
    return page;
}

QWidget *TableDesignerDialog::createDdlPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    m_ddlView = new QPlainTextEdit(page);
    m_ddlView->setReadOnly(true);
    layout->addWidget(m_ddlView, 1);
    return page;
}

QWidget *TableDesignerDialog::createSqlPreviewPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    m_sqlPreview = new QPlainTextEdit(page);
    m_sqlPreview->setReadOnly(true);
    layout->addWidget(m_sqlPreview, 1);
    return page;
}

void TableDesignerDialog::addColumn()
{
    if(!m_columnTable){
        return;
    }
    auto makeCheckItem = [](Qt::CheckState state = Qt::Unchecked) {
        auto *item = new QTableWidgetItem;
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(state);
        return item;
    };
    const int row = m_columnTable->rowCount();
    m_columnTable->insertRow(row);
    m_columnTable->setItem(row, 0, new QTableWidgetItem(tr("new_column")));
    m_columnTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("varchar(255)")));
    m_columnTable->setItem(row, 2, makeCheckItem());
    m_columnTable->setItem(row, 3, makeCheckItem());
    m_columnTable->setItem(row, 4, makeCheckItem());
    m_columnTable->setItem(row, 5, makeCheckItem());
    m_columnTable->setItem(row, 6, new QTableWidgetItem);
    m_columnTable->setItem(row, 7, new QTableWidgetItem);
    updateSqlPreview();
}

void TableDesignerDialog::removeColumn()
{
    if(!m_columnTable){
        return;
    }
    const int row = m_columnTable->currentRow();
    if(row < 0){
        return;
    }
    m_columnTable->removeRow(row);
    updateSqlPreview();
}

void TableDesignerDialog::moveColumnUp()
{
    if(!m_columnTable){
        return;
    }
    const int row = m_columnTable->currentRow();
    if(row <= 0){
        return;
    }
    m_columnTable->insertRow(row - 1);
    for(int c = 0; c < m_columnTable->columnCount(); ++c){
        m_columnTable->setItem(row - 1, c, m_columnTable->takeItem(row + 1, c));
        QWidget *cellWidget = m_columnTable->cellWidget(row + 1, c);
        if(cellWidget){
            m_columnTable->removeCellWidget(row + 1, c);
            m_columnTable->setCellWidget(row - 1, c, cellWidget);
        }
    }
    m_columnTable->removeRow(row + 1);
    m_columnTable->setCurrentCell(row - 1, 0);
    updateSqlPreview();
}

void TableDesignerDialog::moveColumnDown()
{
    if(!m_columnTable){
        return;
    }
    const int row = m_columnTable->currentRow();
    if(row < 0 || row >= m_columnTable->rowCount() - 1){
        return;
    }
    m_columnTable->insertRow(row + 2);
    for(int c = 0; c < m_columnTable->columnCount(); ++c){
        m_columnTable->setItem(row + 2, c, m_columnTable->takeItem(row, c));
        QWidget *cellWidget = m_columnTable->cellWidget(row, c);
        if(cellWidget){
            m_columnTable->removeCellWidget(row, c);
            m_columnTable->setCellWidget(row + 2, c, cellWidget);
        }
    }
    m_columnTable->removeRow(row);
    m_columnTable->setCurrentCell(row + 1, 0);
    updateSqlPreview();
}

void TableDesignerDialog::refreshColumns()
{
    if(!loadColumns()){
        return;
    }
    loadCreateStatement();
    populateColumnTable();
    loadIndexes();
    updateSqlPreview();
}

void TableDesignerDialog::saveChanges()
{
    const QList<ColumnDefinition> columns = currentColumns();
    QString validationError;
    if(!validateColumns(columns, &validationError)){
        QMessageBox::warning(this,
                             tr("Table Designer"),
                             validationError.isEmpty()
                                 ? tr("请检查列配置。")
                                 : validationError);
        return;
    }
    const QStringList statements = generateAlterStatements(columns);
    updateSqlPreview();
    if(statements.isEmpty()){
        QMessageBox::information(this, tr("Table Designer"), tr("No changes to apply."));
        return;
    }
    const auto reply = QMessageBox::question(this,
                                             tr("Apply Changes"),
                                             tr("Execute %1 statement(s)?").arg(statements.size()),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if(reply != QMessageBox::Yes){
        return;
    }
    if(!applyStatements(statements)){
        return;
    }
    refreshColumns();
    QMessageBox::information(this, tr("Table Designer"), tr("Table updated successfully."));
}

void TableDesignerDialog::updateSqlPreview()
{
    if(!m_sqlPreview || m_updatingTable){
        return;
    }
    const QList<ColumnDefinition> columns = currentColumns();
    QString validationError;
    if(!validateColumns(columns, &validationError)){
        m_sqlPreview->setPlainText(validationError);
        return;
    }
    const QStringList statements = generateAlterStatements(columns);
    m_sqlPreview->setPlainText(statements.join(QStringLiteral("\n")));
}

bool TableDesignerDialog::loadColumns()
{
    QString error;
    const QString handle = uniqueConnectionName(QStringLiteral("table"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
    if(!configureDatabase(db, m_connection, m_databaseName, &error)){
        QMessageBox::warning(this, tr("Table Designer"), tr("Connection failed: %1").arg(error));
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    QSqlQuery query(db);
    const QString sql = QStringLiteral("SHOW FULL COLUMNS FROM %1")
            .arg(qualifiedTableName(m_databaseName, m_tableName));
    if(!query.exec(sql)){
        QMessageBox::warning(this,
                             tr("Table Designer"),
                             tr("Failed to query columns: %1").arg(query.lastError().text()));
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    m_originalColumns.clear();
    while(query.next()){
        ColumnDefinition def;
        def.name = query.value(QStringLiteral("Field")).toString();
        def.originalName = def.name;  // Track original name
        def.type = query.value(QStringLiteral("Type")).toString();
        def.collation = query.value(QStringLiteral("Collation")).toString();
        def.notNull = query.value(QStringLiteral("Null")).toString().compare(QStringLiteral("NO"), Qt::CaseInsensitive) == 0;
        const QString key = query.value(QStringLiteral("Key")).toString();
        def.primaryKey = key.compare(QStringLiteral("PRI"), Qt::CaseInsensitive) == 0;
        const QString extra = query.value(QStringLiteral("Extra")).toString();
        def.autoIncrement = extra.contains(QStringLiteral("auto_increment"), Qt::CaseInsensitive);
        def.unsignedFlag = def.type.contains(QStringLiteral("unsigned"), Qt::CaseInsensitive);
        def.defaultValue = query.value(QStringLiteral("Default")).toString();
        def.comment = query.value(QStringLiteral("Comment")).toString();
        m_originalColumns.append(def);
    }
    db.close();
    QSqlDatabase::removeDatabase(handle);
    return true;
}

bool TableDesignerDialog::loadIndexes()
{
    if(!m_indexTable){
        return true;
    }
    QString error;
    const QString handle = uniqueConnectionName(QStringLiteral("index"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
    if(!configureDatabase(db, m_connection, m_databaseName, &error)){
        QMessageBox::warning(this, tr("Table Designer"), tr("Connection failed: %1").arg(error));
        QSqlDatabase::removeDatabase(handle);
        m_indexes.clear();
        populateIndexTable();
        return false;
    }
    QSqlQuery query(db);
    const QString sql = QStringLiteral("SHOW INDEX FROM %1")
            .arg(qualifiedTableName(m_databaseName, m_tableName));
    if(!query.exec(sql)){
        QMessageBox::warning(this,
                             tr("Table Designer"),
                             tr("Failed to query indexes: %1").arg(query.lastError().text()));
        m_indexes.clear();
        populateIndexTable();
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    QMap<QString, IndexDefinition> indexMap;
    while(query.next()){
        const QString keyName = query.value(QStringLiteral("Key_name")).toString();
        if(keyName.isEmpty()){
            continue;
        }
        IndexDefinition def = indexMap.value(keyName);
        def.name = keyName;
        def.unique = query.value(QStringLiteral("Non_unique")).toInt() == 0;
        const QString idxType = query.value(QStringLiteral("Index_type")).toString();
        if(!idxType.isEmpty()){
            def.type = idxType;
        }
        QString idxComment = query.value(QStringLiteral("Index_comment")).toString();
        if(idxComment.isEmpty()){
            idxComment = query.value(QStringLiteral("Comment")).toString();
        }
        if(!idxComment.isEmpty()){
            def.comment = idxComment;
        }
        const QString column = query.value(QStringLiteral("Column_name")).toString();
        int seq = query.value(QStringLiteral("Seq_in_index")).toInt();
        if(seq <= 0){
            seq = def.columns.size() + 1;
        }
        while(def.columns.size() < seq){
            def.columns.append(QString());
        }
        def.columns[seq - 1] = column;
        indexMap.insert(keyName, def);
    }
    db.close();
    QSqlDatabase::removeDatabase(handle);

    m_indexes = indexMap.values();
    std::sort(m_indexes.begin(), m_indexes.end(), [](const IndexDefinition &a, const IndexDefinition &b) {
        return a.name.toLower() < b.name.toLower();
    });
    populateIndexTable();
    return true;
}

bool TableDesignerDialog::loadCreateStatement()
{
    QString error;
    const QString handle = uniqueConnectionName(QStringLiteral("ddl"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
    if(!configureDatabase(db, m_connection, m_databaseName, &error)){
        if(m_ddlView){
            m_ddlView->setPlainText(tr("Connection failed: %1").arg(error));
        }
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    QSqlQuery query(db);
    const QString sql = QStringLiteral("SHOW CREATE TABLE %1")
            .arg(qualifiedTableName(m_databaseName, m_tableName));
    if(!query.exec(sql) || !query.next()){
        if(m_ddlView){
            m_ddlView->setPlainText(tr("Unable to load DDL: %1").arg(query.lastError().text()));
        }
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    m_createStatement = query.value(1).toString();
    if(m_ddlView){
        m_ddlView->setPlainText(m_createStatement);
    }
    db.close();
    QSqlDatabase::removeDatabase(handle);
    return true;
}

void TableDesignerDialog::populateColumnTable()
{
    if(!m_columnTable){
        return;
    }
    m_updatingTable = true;
    m_columnTable->clearContents();
    m_columnTable->setRowCount(m_originalColumns.size());
    for(int row = 0; row < m_originalColumns.size(); ++row){
        const ColumnDefinition &def = m_originalColumns.at(row);
        auto *nameItem = new QTableWidgetItem(def.name);
        nameItem->setData(Qt::UserRole, def.name);              // Store original column name directly
        nameItem->setData(Qt::UserRole + 1, def.collation);     // Store collation
        m_columnTable->setItem(row, 0, nameItem);
        m_columnTable->setItem(row, 1, new QTableWidgetItem(def.type));

        auto makeCheckItem = [&](bool checked) {
            auto *item = new QTableWidgetItem;
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            return item;
        };
        m_columnTable->setItem(row, 2, makeCheckItem(def.unsignedFlag));
        m_columnTable->setItem(row, 3, makeCheckItem(def.notNull));
        m_columnTable->setItem(row, 4, makeCheckItem(def.primaryKey));
        m_columnTable->setItem(row, 5, makeCheckItem(def.autoIncrement));

        m_columnTable->setItem(row, 6, new QTableWidgetItem(def.defaultValue));
        m_columnTable->setItem(row, 7, new QTableWidgetItem(def.comment));
    }
    m_updatingTable = false;
}

void TableDesignerDialog::populateIndexTable()
{
    if(!m_indexTable){
        return;
    }
    m_indexTable->clearContents();
    m_indexTable->setRowCount(m_indexes.size());
    for(int row = 0; row < m_indexes.size(); ++row){
        const IndexDefinition &idx = m_indexes.at(row);
        QStringList nonEmptyColumns;
        for(const QString &name : idx.columns){
            if(!name.trimmed().isEmpty()){
                nonEmptyColumns << name;
            }
        }
        m_indexTable->setItem(row, 0, new QTableWidgetItem(idx.name));
        m_indexTable->setItem(row, 1, new QTableWidgetItem(nonEmptyColumns.join(QStringLiteral(", "))));
        m_indexTable->setItem(row, 2, new QTableWidgetItem(idx.type));
        auto *uniqueItem = new QTableWidgetItem(idx.unique ? tr("Yes") : tr("No"));
        uniqueItem->setTextAlignment(Qt::AlignCenter);
        m_indexTable->setItem(row, 3, uniqueItem);
        m_indexTable->setItem(row, 4, new QTableWidgetItem(idx.comment));
    }
    if(m_indexes.isEmpty()){
        m_indexTable->setRowCount(0);
    }
}

QList<TableDesignerDialog::ColumnDefinition> TableDesignerDialog::currentColumns() const
{
    QList<ColumnDefinition> cols;
    if(!m_columnTable){
        return cols;
    }
    for(int row = 0; row < m_columnTable->rowCount(); ++row){
        ColumnDefinition def;
        def.name = m_columnTable->item(row, 0) ? m_columnTable->item(row, 0)->text().trimmed() : QString();
        // Get original column name and collation directly from Qt::UserRole
        if(m_columnTable->item(row, 0)){
            def.originalName = m_columnTable->item(row, 0)->data(Qt::UserRole).toString();
            def.collation = m_columnTable->item(row, 0)->data(Qt::UserRole + 1).toString();
        }
        def.type = m_columnTable->item(row, 1) ? m_columnTable->item(row, 1)->text().trimmed() : QString();
        def.unsignedFlag = m_columnTable->item(row, 2) && m_columnTable->item(row, 2)->checkState() == Qt::Checked;
        def.notNull = m_columnTable->item(row, 3) && m_columnTable->item(row, 3)->checkState() == Qt::Checked;
        def.primaryKey = m_columnTable->item(row, 4) && m_columnTable->item(row, 4)->checkState() == Qt::Checked;
        def.autoIncrement = m_columnTable->item(row, 5) && m_columnTable->item(row, 5)->checkState() == Qt::Checked;
        def.defaultValue = m_columnTable->item(row, 6) ? m_columnTable->item(row, 6)->text() : QString();
        def.comment = m_columnTable->item(row, 7) ? m_columnTable->item(row, 7)->text() : QString();
        if(!def.name.isEmpty()){
            cols.append(def);
        }
    }
    return cols;
}

bool TableDesignerDialog::validateColumns(const QList<ColumnDefinition> &columns, QString *errorMessage) const
{
    if(columns.isEmpty()){
        if(errorMessage){
            *errorMessage = tr("请至少保留一列。");
        }
        return false;
    }
    QSet<QString> names;
    bool hasPrimary = false;
    for(int i = 0; i < columns.size(); ++i){
        const auto &col = columns.at(i);
        if(col.name.isEmpty()){
            if(errorMessage){
                *errorMessage = tr("第 %1 行的列名不能为空。").arg(i + 1);
            }
            return false;
        }
        const QString lower = col.name.toLower();
        if(names.contains(lower)){
            if(errorMessage){
                *errorMessage = tr("列 \"%1\" 重复，请修改列名。").arg(col.name);
            }
            return false;
        }
        names.insert(lower);
        if(col.type.trimmed().isEmpty()){
            if(errorMessage){
                *errorMessage = tr("列 \"%1\" 未指定数据类型。").arg(col.name);
            }
            return false;
        }
        if(col.autoIncrement && !col.primaryKey){
            if(errorMessage){
                *errorMessage = tr("列 \"%1\" 设置了自增，必须勾选 Primary Key。").arg(col.name);
            }
            return false;
        }
        if(col.primaryKey){
            hasPrimary = true;
        }
    }
    // Auto increment columns imply PK already ensured; no need enforce at least one PK.
    if(!hasPrimary){
        bool hasAuto = std::any_of(columns.begin(), columns.end(), [](const ColumnDefinition &col) {
            return col.autoIncrement;
        });
        if(hasAuto && errorMessage){
            *errorMessage = tr("存在自增列但未设置主键。");
            return false;
        }
    }
    return true;
}

QString TableDesignerDialog::columnDefinitionSql(const ColumnDefinition &col) const
{
    QString def = quoted(col.name);
    QString type = col.type;
    if(type.isEmpty()){
        type = QStringLiteral("varchar(255)");
    }
    def += QStringLiteral(" %1").arg(type);
    if(col.unsignedFlag && !type.contains(QStringLiteral("unsigned"), Qt::CaseInsensitive)){
        def += QStringLiteral(" UNSIGNED");
    }
    // Add CHARACTER SET and COLLATE if collation is specified
    // Collation format: utf8mb4_general_ci -> CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci
    if(!col.collation.isEmpty()){
        const int underscorePos = col.collation.indexOf(QLatin1Char('_'));
        if(underscorePos > 0){
            const QString charset = col.collation.left(underscorePos);
            def += QStringLiteral(" CHARACTER SET %1 COLLATE %2").arg(charset, col.collation);
        }
    }
    def += col.notNull ? QStringLiteral(" NOT NULL") : QStringLiteral(" NULL");
    if(!col.defaultValue.isEmpty()){
        QString defaultValue = col.defaultValue;
        QString value;
        if(defaultValue.compare(QStringLiteral("NULL"), Qt::CaseInsensitive) == 0){
            value = QStringLiteral("NULL");
        }else{
            defaultValue.replace(QLatin1Char('\''), QStringLiteral("''"));
            value = QStringLiteral("'%1'").arg(defaultValue);
        }
        def += QStringLiteral(" DEFAULT %1").arg(value);
    }
    if(col.autoIncrement){
        def += QStringLiteral(" AUTO_INCREMENT");
    }
    if(!col.comment.isEmpty()){
        QString comment = col.comment;
        comment.replace(QLatin1Char('\''), QStringLiteral("''"));
        def += QStringLiteral(" COMMENT '%1'").arg(comment);
    }
    return def;
}

QString TableDesignerDialog::positionClause(int index, const QList<ColumnDefinition> &columns) const
{
    if(index <= 0){
        return QStringLiteral(" FIRST");
    }
    const QString prevName = columns.value(index - 1).name;
    if(prevName.isEmpty()){
        return QString();
    }
    return QStringLiteral(" AFTER %1").arg(quoted(prevName));
}

QStringList TableDesignerDialog::generateAlterStatements(const QList<ColumnDefinition> &columns) const
{
    QStringList stmts;
    QStringList originalPrimary;
    QStringList currentPrimary;
    QMap<QString, ColumnDefinition> originalMap;
    QMap<QString, int> originalOrder;
    for(int i = 0; i < m_originalColumns.size(); ++i){
        originalMap.insert(m_originalColumns.at(i).name, m_originalColumns.at(i));
        originalOrder.insert(m_originalColumns.at(i).name, i);
        if(m_originalColumns.at(i).primaryKey){
            originalPrimary << m_originalColumns.at(i).name;
        }
    }
    // Track which original columns are still referenced
    QSet<QString> usedOriginalNames;
    for(const auto &col : columns){
        if(!col.originalName.isEmpty()){
            usedOriginalNames.insert(col.originalName);
        }
        if(col.primaryKey){
            currentPrimary << col.name;
        }
    }
    // Drops - only drop columns that are not referenced by any current column
    for(const auto &orig : m_originalColumns){
        if(!usedOriginalNames.contains(orig.name)){
            stmts << QStringLiteral("ALTER TABLE %1 DROP COLUMN %2;")
                     .arg(qualifiedTable(), quoted(orig.name));
        }
    }
    // Adds / modifies / renames
    for(int i = 0; i < columns.size(); ++i){
        const ColumnDefinition &col = columns.at(i);
        if(col.name.isEmpty()){
            continue;
        }
        QString clause = positionClause(i, columns);
        // New column (no originalName means it's newly added)
        if(col.originalName.isEmpty()){
            stmts << QStringLiteral("ALTER TABLE %1 ADD COLUMN %2%3;")
                     .arg(qualifiedTable(),
                          columnDefinitionSql(col),
                          clause);
        }else{
            // Existing column - check if renamed or modified
            const ColumnDefinition &orig = originalMap.value(col.originalName);
            const bool nameChanged = col.name != col.originalName;
            const bool changedOrder = originalOrder.value(col.originalName) != i;
            if(nameChanged || col != orig || changedOrder){
                // Use CHANGE COLUMN for rename, MODIFY COLUMN otherwise
                if(nameChanged){
                    stmts << QStringLiteral("ALTER TABLE %1 CHANGE COLUMN %2 %3%4;")
                             .arg(qualifiedTable(),
                                  quoted(col.originalName),
                                  columnDefinitionSql(col),
                                  clause);
                }else{
                    stmts << QStringLiteral("ALTER TABLE %1 MODIFY COLUMN %2%3;")
                             .arg(qualifiedTable(),
                                  columnDefinitionSql(col),
                                  clause);
                }
            }
        }
    }
    if(originalPrimary != currentPrimary){
        if(!originalPrimary.isEmpty()){
            stmts.prepend(QStringLiteral("ALTER TABLE %1 DROP PRIMARY KEY;").arg(qualifiedTable()));
        }
        if(!currentPrimary.isEmpty()){
            QStringList quotedCols;
            for(const auto &name : currentPrimary){
                if(!name.isEmpty()){
                    quotedCols << quoted(name);
                }
            }
            if(!quotedCols.isEmpty()){
                stmts << QStringLiteral("ALTER TABLE %1 ADD PRIMARY KEY (%2);")
                         .arg(qualifiedTable(),
                              quotedCols.join(QStringLiteral(", ")));
            }
        }
    }
    return stmts;
}

bool TableDesignerDialog::applyStatements(const QStringList &statements)
{
    QString error;
    const QString handle = uniqueConnectionName(QStringLiteral("apply"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
    if(!configureDatabase(db, m_connection, m_databaseName, &error)){
        QMessageBox::warning(this, tr("Table Designer"), tr("Connection failed: %1").arg(error));
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    QSqlQuery query(db);
    for(const QString &sql : statements){
        if(sql.trimmed().isEmpty()){
            continue;
        }
        if(!query.exec(sql)){
            QMessageBox::warning(this,
                                 tr("Table Designer"),
                                 tr("Failed to execute:\n%1\nError: %2").arg(sql, query.lastError().text()));
            db.close();
            QSqlDatabase::removeDatabase(handle);
            return false;
        }
    }
    db.close();
    QSqlDatabase::removeDatabase(handle);
    return true;
}

QString TableDesignerDialog::qualifiedTable() const
{
    return qualifiedTableName(m_databaseName, m_tableName);
}
