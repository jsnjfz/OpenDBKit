#include "datasyncdialog.h"
#include "languagemanager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>
#include <utility>

namespace {
QString escapeIdentifier(const QString &name)
{
    QString value = name;
    value.replace(QLatin1Char('`'), QStringLiteral("``"));
    return QStringLiteral("`%1`").arg(value);
}

QString qualifiedTable(const QString &dbName, const QString &tableName)
{
    return QStringLiteral("%1.%2").arg(escapeIdentifier(dbName), escapeIdentifier(tableName));
}

QString uniqueConnectionName(const QString &prefix)
{
    return QStringLiteral("%1_%2_%3")
            .arg(prefix,
                 QString::number(QDateTime::currentMSecsSinceEpoch()),
                 QString::number(QRandomGenerator::global()->generate()));
}

bool configureDatabase(QSqlDatabase &db,
                       const ConnectionInfo &info,
                       const QString &dbName,
                       QString *error)
{
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    const QString finalDb = dbName.isEmpty() ? info.defaultDb : dbName;
    if(finalDb.isEmpty()){
        if(error){
            *error = trLang(QStringLiteral("数据库名称不能为空。"),
                            QStringLiteral("Database name cannot be empty."));
        }
        return false;
    }
    db.setDatabaseName(finalDb);
    if(!db.open()){
        if(error){
            *error = db.lastError().text();
        }
        return false;
    }
    return true;
}

}

DataSyncDialog::DataSyncDialog(QWidget *parent)
    : QDialog(parent)
{
    resize(1720, 1020);
    buildUi();

    auto *langMgr = LanguageManager::instance();
    connect(langMgr, &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });
    retranslateUi();

    loadConnections();
    updateNavigation();
}

DataSyncDialog::~DataSyncDialog()
{
    if(syncThread){
        syncThread->quit();
        syncThread->wait();
    }
}

void DataSyncDialog::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    stack = new QStackedWidget(this);
    pageSelect = createSourceTargetPage();
    pageMapping = createMappingPage();
    pageExecute = createExecutePage();
    stack->addWidget(pageSelect);
    stack->addWidget(pageMapping);
    stack->addWidget(pageExecute);
    mainLayout->addWidget(stack, 1);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    backButton = new QPushButton(this);
    nextButton = new QPushButton(this);
    startButton = new QPushButton(this);
    cancelButton = new QPushButton(this);
    buttonLayout->addWidget(backButton);
    buttonLayout->addWidget(nextButton);
    buttonLayout->addWidget(startButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(backButton, &QPushButton::clicked, this, &DataSyncDialog::goBack);
    connect(nextButton, &QPushButton::clicked, this, &DataSyncDialog::goNext);
    connect(startButton, &QPushButton::clicked, this, &DataSyncDialog::startSync);
    connect(cancelButton, &QPushButton::clicked, this, &DataSyncDialog::cancelDialog);
}

QWidget *DataSyncDialog::createSourceTargetPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(32);

    auto createSelector = [this, page](QGroupBox **groupOut,
                                       QLabel **datasourceLabel,
                                       QLabel **databaseLabel,
                                       QComboBox **connCombo,
                                       QComboBox **dbCombo) -> QGroupBox* {
        auto *group = new QGroupBox(page);
        auto *form = new QFormLayout(group);
        *connCombo = new QComboBox(group);
        *dbCombo = new QComboBox(group);
        (*dbCombo)->setEditable(false);
        auto *dsLabel = new QLabel(group);
        auto *dbLabel = new QLabel(group);
        form->addRow(dsLabel, *connCombo);
        form->addRow(dbLabel, *dbCombo);
        if(groupOut){
            *groupOut = group;
        }
        if(datasourceLabel){
            *datasourceLabel = dsLabel;
        }
        if(databaseLabel){
            *databaseLabel = dbLabel;
        }
        return group;
    };

    createSelector(&sourceGroupBox,
                   &sourceDatasourceLabel,
                   &sourceDatabaseLabel,
                   &sourceConnCombo,
                   &sourceDbCombo);
    createSelector(&targetGroupBox,
                   &targetDatasourceLabel,
                   &targetDatabaseLabel,
                   &targetConnCombo,
                   &targetDbCombo);

    swapButton = new QPushButton(QStringLiteral("⇄"), page);
    swapButton->setFixedWidth(40);
    connect(swapButton, &QPushButton::clicked, this, &DataSyncDialog::swapConnections);

    layout->addWidget(sourceGroupBox, 1);
    layout->addWidget(swapButton);
    layout->addWidget(targetGroupBox, 1);

    connect(sourceConnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DataSyncDialog::onSourceConnectionChanged);
    connect(targetConnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DataSyncDialog::onTargetConnectionChanged);
    connect(sourceDbCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DataSyncDialog::onSourceDbChanged);
    connect(targetDbCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DataSyncDialog::onTargetDbChanged);

    return page;
}

QWidget *DataSyncDialog::createMappingPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *infoLayout = new QHBoxLayout;
    sourceSummaryLabel = new QLabel(page);
    targetSummaryLabel = new QLabel(page);
    infoLayout->addWidget(sourceSummaryLabel, 1);
    infoLayout->addWidget(targetSummaryLabel, 1);
    layout->addLayout(infoLayout);

    filterEdit = new QLineEdit(page);
    layout->addWidget(filterEdit);

    mappingTable = new QTableWidget(page);
    mappingTable->setColumnCount(4);
    QStringList placeholderHeaders;
    placeholderHeaders << QString() << QString() << QString() << QString();
    mappingTable->setHorizontalHeaderLabels(placeholderHeaders);
    mappingTable->horizontalHeader()->setStretchLastSection(true);
    mappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mappingTable->verticalHeader()->setVisible(false);
    mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mappingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mappingTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(mappingTable, 1);

    auto *sideLayout = new QVBoxLayout;
    auto *sideWidget = new QWidget(page);
    sideWidget->setLayout(sideLayout);
    sideLayout->addStretch();
    syncAllButton = new QPushButton(sideWidget);
    clearAllButton = new QPushButton(sideWidget);
    editMappingButton = new QPushButton(sideWidget);
    sideLayout->addWidget(syncAllButton);
    sideLayout->addWidget(clearAllButton);
    sideLayout->addWidget(editMappingButton);
    sideLayout->addStretch();

    auto *tableWrapper = new QHBoxLayout;
    tableWrapper->addWidget(mappingTable, 1);
    tableWrapper->addWidget(sideWidget);
    layout->addLayout(tableWrapper, 1);

    connect(filterEdit, &QLineEdit::textChanged, this, &DataSyncDialog::applyFilter);
    connect(syncAllButton, &QPushButton::clicked, this, &DataSyncDialog::synchronizeAll);
    connect(clearAllButton, &QPushButton::clicked, this, &DataSyncDialog::clearAllSelections);
    connect(editMappingButton, &QPushButton::clicked, this, &DataSyncDialog::editMapping);
    connect(mappingTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if(!item){
            return;
        }
        const int row = item->row();
        const int column = item->column();
        if(row < 0 || row >= mappings.size()){
            return;
        }
        auto &entry = mappings[row];
        switch(column){
        case 0:
            entry.enabled = (item->checkState() == Qt::Checked);
            break;
        case 1:
            entry.targetTable = item->text().trimmed();
            break;
        case 2:
            updateCreateTableState(row, item->checkState() == Qt::Checked);
            break;
        case 3:
            entry.mappingLabel = item->text().trimmed();
            break;
        default:
            break;
        }
    });

    return page;
}

QWidget *DataSyncDialog::createExecutePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);

    auto *topLayout = new QFormLayout;
    batchSizeSpin = new QSpinBox(page);
    batchSizeSpin->setRange(100, 100000);
    batchSizeSpin->setSingleStep(100);
    batchSizeSpin->setValue(2000);
    continueOnErrorCheck = new QCheckBox(page);
    strictModeCheck = new QCheckBox(page);
    emptyTargetCheck = new QCheckBox(page);
    truncateCheck = new QCheckBox(page);
    truncateCheck->setEnabled(false);

    emptyTargetCheck->setChecked(false);

    connect(emptyTargetCheck, &QCheckBox::toggled, truncateCheck, &QCheckBox::setEnabled);

    batchSizeLabel = new QLabel(page);
    topLayout->addRow(batchSizeLabel, batchSizeSpin);
    topLayout->addRow(QString(), continueOnErrorCheck);
    topLayout->addRow(QString(), strictModeCheck);
    topLayout->addRow(QString(), emptyTargetCheck);
    topLayout->addRow(QString(), truncateCheck);
    layout->addLayout(topLayout);

    logEdit = new QPlainTextEdit(page);
    logEdit->setReadOnly(true);
    logEdit->setPlaceholderText(QString());
    layout->addWidget(logEdit, 1);

    progressBar = new QProgressBar(page);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    layout->addWidget(progressBar);

    return page;
}

void DataSyncDialog::retranslateUi()
{
    setWindowTitle(trLang(QStringLiteral("数据同步"), QStringLiteral("Data Synchronization")));

    if(sourceGroupBox){
        sourceGroupBox->setTitle(trLang(QStringLiteral("源"), QStringLiteral("Source")));
    }
    if(targetGroupBox){
        targetGroupBox->setTitle(trLang(QStringLiteral("目标"), QStringLiteral("Target")));
    }
    if(sourceDatasourceLabel){
        sourceDatasourceLabel->setText(trLang(QStringLiteral("数据源："), QStringLiteral("Datasource:")));
    }
    if(sourceDatabaseLabel){
        sourceDatabaseLabel->setText(trLang(QStringLiteral("数据库："), QStringLiteral("Database:")));
    }
    if(targetDatasourceLabel){
        targetDatasourceLabel->setText(trLang(QStringLiteral("数据源："), QStringLiteral("Datasource:")));
    }
    if(targetDatabaseLabel){
        targetDatabaseLabel->setText(trLang(QStringLiteral("数据库："), QStringLiteral("Database:")));
    }
    if(filterEdit){
        filterEdit->setPlaceholderText(trLang(QStringLiteral("正则过滤"), QStringLiteral("Regex Filter")));
    }
    if(mappingTable){
        mappingTable->setHorizontalHeaderLabels({
            trLang(QStringLiteral("源表"), QStringLiteral("Source Table")),
            trLang(QStringLiteral("目标表"), QStringLiteral("Target Table")),
            trLang(QStringLiteral("自动建表"), QStringLiteral("Create Table")),
            trLang(QStringLiteral("字段映射"), QStringLiteral("Field Mapping"))
        });
    }
    if(syncAllButton){
        syncAllButton->setText(trLang(QStringLiteral("全部同步"), QStringLiteral("Synchronize All")));
    }
    if(clearAllButton){
        clearAllButton->setText(trLang(QStringLiteral("取消全选"), QStringLiteral("Clear All")));
    }
    if(editMappingButton){
        editMappingButton->setText(trLang(QStringLiteral("编辑映射"), QStringLiteral("Edit Mapping")));
    }
    if(batchSizeLabel){
        batchSizeLabel->setText(trLang(QStringLiteral("批量插入大小："), QStringLiteral("Batch insert size:")));
    }
    if(continueOnErrorCheck){
        continueOnErrorCheck->setText(trLang(QStringLiteral("出错后继续"), QStringLiteral("Continue on error")));
    }
    if(strictModeCheck){
        strictModeCheck->setText(trLang(QStringLiteral("关闭目标端严格模式"), QStringLiteral("Turn off strict SQL mode")));
    }
    if(emptyTargetCheck){
        emptyTargetCheck->setText(trLang(QStringLiteral("同步前清空目标表"), QStringLiteral("Empty target table before importing")));
    }
    if(truncateCheck){
        truncateCheck->setText(trLang(QStringLiteral("使用 TRUNCATE TABLE"), QStringLiteral("Use TRUNCATE TABLE statement")));
    }
    if(logEdit){
        logEdit->setPlaceholderText(trLang(QStringLiteral("-- 等待开始同步 --"),
                                           QStringLiteral("-- Waiting to start synchronization --")));
    }
    if(backButton){
        backButton->setText(trLang(QStringLiteral("< 返回"), QStringLiteral("< Back")));
    }
    if(nextButton){
        nextButton->setText(trLang(QStringLiteral("下一步 >"), QStringLiteral("Next >")));
    }
    if(startButton){
        startButton->setText(trLang(QStringLiteral("开始"), QStringLiteral("Start")));
    }
    if(cancelButton){
        cancelButton->setText(trLang(QStringLiteral("取消"), QStringLiteral("Cancel")));
    }

    updateSummaryLabels();
}

void DataSyncDialog::updateSummaryLabels()
{
    auto makeText = [this](const QString &titleZh,
                           const QString &titleEn,
                           const QString &conn,
                           const QString &db) {
        const QString connText = conn.isEmpty()
                ? trLang(QStringLiteral("未选择"), QStringLiteral("<none>"))
                : conn;
        const QString dbText = db.isEmpty()
                ? trLang(QStringLiteral("未选择"), QStringLiteral("<none>"))
                : db;
        return trLang(QStringLiteral("%1：%2/%3").arg(titleZh, connText, dbText),
                      QStringLiteral("%1: %2/%3").arg(titleEn, connText, dbText));
    };

    if(sourceSummaryLabel){
        const QString conn = sourceConnCombo ? sourceConnCombo->currentData().toString() : QString();
        const QString db = sourceDbCombo ? sourceDbCombo->currentText() : QString();
        sourceSummaryLabel->setText(makeText(QStringLiteral("源"), QStringLiteral("Source"), conn, db));
    }
    if(targetSummaryLabel){
        const QString conn = targetConnCombo ? targetConnCombo->currentData().toString() : QString();
        const QString db = targetDbCombo ? targetDbCombo->currentText() : QString();
        targetSummaryLabel->setText(makeText(QStringLiteral("目标"), QStringLiteral("Target"), conn, db));
    }
}

void DataSyncDialog::setSourceContext(const QString &connName, const QString &dbName)
{
    int idx = sourceConnCombo->findData(connName);
    if(idx >= 0){
        sourceConnCombo->setCurrentIndex(idx);
    }
    if(!dbName.isEmpty()){
        populateDatabaseCombo(sourceDbCombo, connName, dbName);
        int dbIdx = sourceDbCombo->findText(dbName, Qt::MatchFixedString);
        if(dbIdx >= 0){
            sourceDbCombo->setCurrentIndex(dbIdx);
        }
    }
}

void DataSyncDialog::setTargetContext(const QString &connName, const QString &dbName)
{
    int idx = targetConnCombo->findData(connName);
    if(idx >= 0){
        targetConnCombo->setCurrentIndex(idx);
    }
    if(!dbName.isEmpty()){
        populateDatabaseCombo(targetDbCombo, connName, dbName);
        int dbIdx = targetDbCombo->findText(dbName, Qt::MatchFixedString);
        if(dbIdx >= 0){
            targetDbCombo->setCurrentIndex(dbIdx);
        }
    }
}

void DataSyncDialog::setInitialTableHint(const QString &tableName)
{
    sourceHintTable = tableName;
}

void DataSyncDialog::goNext()
{
    if(syncInProgress){
        return;
    }
    const int index = stack->currentIndex();
    if(index == 0){
        refreshMappingData();
    }else if(index == 1){
        // proceed to execute page
    }
    stack->setCurrentIndex(qMin(index + 1, stack->count() - 1));
    updateNavigation();
}

void DataSyncDialog::goBack()
{
    if(syncInProgress){
        return;
    }
    int index = stack->currentIndex();
    stack->setCurrentIndex(qMax(index - 1, 0));
    updateNavigation();
}

void DataSyncDialog::cancelDialog()
{
    if(syncInProgress){
        QMessageBox::information(this,
                                 trLang(QStringLiteral("同步进行中"),
                                        QStringLiteral("Synchronization In Progress")),
                                 trLang(QStringLiteral("请等待当前同步任务完成。"),
                                        QStringLiteral("Please wait until the current synchronization finishes.")));
        return;
    }
    reject();
}

void DataSyncDialog::startSync()
{
    if(syncInProgress){
        QMessageBox::information(this,
                                 trLang(QStringLiteral("同步"), QStringLiteral("Synchronization")),
                                 trLang(QStringLiteral("当前已在执行同步任务，请等待完成。"),
                                        QStringLiteral("A synchronization is already running. Please wait for it to finish.")));
        return;
    }
    logEdit->clear();
    if(stack && pageExecute){
        stack->setCurrentWidget(pageExecute);
    }
    auto comboDbName = [](QComboBox *combo) -> QString {
        if(!combo){
            return QString();
        }
        const QVariant data = combo->currentData();
        if(data.isValid()){
            const QString value = data.toString();
            if(!value.isEmpty()){
                return value;
            }
        }
        return combo->currentText().trimmed();
    };

    const QString sourceConnName = sourceConnCombo->currentData().toString();
    const QString targetConnName = targetConnCombo->currentData().toString();
    const QString sourceDbName = comboDbName(sourceDbCombo);
    const QString targetDbName = comboDbName(targetDbCombo);

    if(sourceConnName.isEmpty() || targetConnName.isEmpty() ||
            sourceDbName.isEmpty() || targetDbName.isEmpty()){
        QMessageBox::warning(this,
                             trLang(QStringLiteral("同步"), QStringLiteral("Synchronization")),
                             trLang(QStringLiteral("请先选择有效的源和目标连接以及数据库。"),
                                    QStringLiteral("Select valid source/target connections and databases first.")));
        return;
    }

    QVector<TableMappingEntry> tasks;
    for(const auto &entry : std::as_const(mappings)){
        if(!entry.enabled){
            continue;
        }
        TableMappingEntry normalized = entry;
        normalized.targetTable = normalized.targetTable.trimmed();
        if(normalized.targetTable.isEmpty()){
            normalized.targetTable = normalized.sourceTable;
        }
        tasks.append(normalized);
    }
    if(tasks.isEmpty()){
        appendLogMessage(trLang(QStringLiteral("未勾选任何待同步的表。"),
                                QStringLiteral("No tables were selected for synchronization.")));
        QMessageBox::information(this,
                                 trLang(QStringLiteral("同步"), QStringLiteral("Synchronization")),
                                 trLang(QStringLiteral("请选择至少一个表进行同步。"),
                                        QStringLiteral("Please choose at least one table to synchronize.")));
        return;
    }

    const ConnectionInfo sourceInfo = ConnectionManager::instance()->connection(sourceConnName);
    const ConnectionInfo targetInfo = ConnectionManager::instance()->connection(targetConnName);
    if(sourceInfo.name.isEmpty() || targetInfo.name.isEmpty()){
        QMessageBox::warning(this,
                             trLang(QStringLiteral("同步"), QStringLiteral("Synchronization")),
                             trLang(QStringLiteral("无法找到所选连接，请刷新连接列表后再试。"),
                                    QStringLiteral("Unable to locate the selected connections. Refresh the list and try again.")));
        return;
    }

    DataSyncOptions options;
    options.sourceInfo = sourceInfo;
    options.targetInfo = targetInfo;
    options.sourceDbName = sourceDbName;
    options.targetDbName = targetDbName;
    options.batchSize = batchSizeSpin->value();
    options.continueOnError = continueOnErrorCheck->isChecked();
    options.strictMode = strictModeCheck->isChecked();
    options.emptyTarget = emptyTargetCheck->isChecked();
    options.useTruncate = options.emptyTarget && truncateCheck->isChecked();

    if(syncThread){
        syncThread->quit();
        syncThread->wait();
        syncThread->deleteLater();
        syncThread = nullptr;
    }

    auto *worker = new DataSyncWorker(this, tasks, options);
    QThread *workerThread = new QThread(this);
    syncThread = workerThread;
    worker->moveToThread(workerThread);
    connect(workerThread, &QThread::started, worker, &DataSyncWorker::process);
    connect(worker, &DataSyncWorker::logMessage, this, &DataSyncDialog::appendLogMessage);
    connect(worker, &DataSyncWorker::progressChanged, this, [this](int current, int total) {
        if(progressBar && total > 0){
            progressBar->setRange(0, total);
            progressBar->setValue(current);
        }
    });
    connect(worker, &DataSyncWorker::finished, this, &DataSyncDialog::handleSyncFinished);
    connect(worker, &DataSyncWorker::finished, worker, &QObject::deleteLater);
    connect(worker, &DataSyncWorker::finished, workerThread, &QThread::quit);
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);
    connect(workerThread, &QThread::finished, this, [this, workerThread]() {
        if(syncThread == workerThread){
            syncThread = nullptr;
        }
    });

    setSyncRunning(true);
    if(progressBar){
        progressBar->setRange(0, tasks.size());
        progressBar->setValue(0);
    }
    appendLogMessage(trLang(QStringLiteral("开始同步，共 %1 个表。"),
                            QStringLiteral("Synchronization started, %1 tables to process.")).arg(tasks.size()));
    workerThread->start();
}

void DataSyncDialog::synchronizeAll()
{
    for(int row = 0; row < mappingTable->rowCount(); ++row){
        setEntryEnabled(row, true);
        setCreateFlag(row, true);
    }
    rebuildMappingTable();
}

void DataSyncDialog::clearAllSelections()
{
    for(int row = 0; row < mappingTable->rowCount(); ++row){
        setEntryEnabled(row, false);
    }
    rebuildMappingTable();
}

void DataSyncDialog::editMapping()
{
    QMessageBox::information(this,
                             trLang(QStringLiteral("字段映射"), QStringLiteral("Field Mapping")),
                             trLang(QStringLiteral("字段映射编辑将在后续版本提供，当前使用默认映射。"),
                                    QStringLiteral("Field mapping editing will be available in a future version. Default mapping is used now.")));
}

void DataSyncDialog::applyFilter(const QString &text)
{
    QRegularExpression regex;
    bool useRegex = false;
    if(!text.trimmed().isEmpty()){
        regex = QRegularExpression(text, QRegularExpression::CaseInsensitiveOption);
        useRegex = regex.isValid();
    }
    for(int row = 0; row < mappingTable->rowCount(); ++row){
        bool match = !useRegex;
        if(useRegex){
            for(int col = 0; col < mappingTable->columnCount(); ++col){
                const auto *item = mappingTable->item(row, col);
                if(item && regex.match(item->text()).hasMatch()){
                    match = true;
                    break;
                }
            }
        }
        mappingTable->setRowHidden(row, !match);
    }
}

void DataSyncDialog::swapConnections()
{
    int srcIndex = sourceConnCombo->currentIndex();
    int tgtIndex = targetConnCombo->currentIndex();
    const QString srcDb = sourceDbCombo->currentText();
    const QString tgtDb = targetDbCombo->currentText();
    sourceConnCombo->setCurrentIndex(tgtIndex);
    targetConnCombo->setCurrentIndex(srcIndex);
    populateDatabaseCombo(sourceDbCombo, sourceConnCombo->currentData().toString(), srcDb);
    populateDatabaseCombo(targetDbCombo, targetConnCombo->currentData().toString(), tgtDb);
    updateSummaryLabels();
}

void DataSyncDialog::onSourceConnectionChanged(int)
{
    populateDatabaseCombo(sourceDbCombo, sourceConnCombo->currentData().toString(), QString());
    updateSummaryLabels();
}

void DataSyncDialog::onTargetConnectionChanged(int)
{
    populateDatabaseCombo(targetDbCombo, targetConnCombo->currentData().toString(), QString());
    updateSummaryLabels();
}

void DataSyncDialog::onSourceDbChanged(int index)
{
    Q_UNUSED(index);
    updateSummaryLabels();
}

void DataSyncDialog::onTargetDbChanged(int index)
{
    Q_UNUSED(index);
    updateSummaryLabels();
}

void DataSyncDialog::handleSyncFinished(bool aborted,
                                        const QString &message,
                                        int successTables,
                                        int failedTables,
                                        qint64 totalRows)
{
    setSyncRunning(false);
    if(syncThread){
        syncThread->quit();
    }
    if(aborted){
        QMessageBox::critical(this,
                              trLang(QStringLiteral("同步失败"), QStringLiteral("Synchronization Failed")),
                              message);
        return;
    }
    if(failedTables > 0){
        QMessageBox::warning(this,
                             trLang(QStringLiteral("部分完成"), QStringLiteral("Partially Completed")),
                             trLang(QStringLiteral("成功 %1 个表，失败 %2 个，详情见日志。"),
                                    QStringLiteral("%1 tables succeeded, %2 failed. See the log for details."))
                             .arg(successTables)
                             .arg(failedTables));
    }else{
        QMessageBox::information(this,
                                 trLang(QStringLiteral("同步完成"), QStringLiteral("Synchronization Completed")),
                                 trLang(QStringLiteral("成功同步 %1 个表，共 %2 行。"),
                                        QStringLiteral("%1 tables synchronized, %2 rows total."))
                                 .arg(successTables)
                                 .arg(totalRows));
    }
}

void DataSyncDialog::updateNavigation()
{
    const int index = stack->currentIndex();
    const bool onLast = (index == stack->count() - 1);
    if(backButton){
        backButton->setEnabled(!syncInProgress && index > 0);
    }
    if(nextButton){
        nextButton->setVisible(!onLast);
        nextButton->setEnabled(!syncInProgress && !onLast);
        nextButton->setDefault(!syncInProgress && !onLast);
    }
    if(startButton){
        startButton->setVisible(onLast);
        startButton->setEnabled(!syncInProgress && onLast);
        startButton->setDefault(!syncInProgress && onLast);
    }
}

void DataSyncDialog::setSyncRunning(bool running)
{
    syncInProgress = running;
    if(pageSelect){
        pageSelect->setEnabled(!running);
    }
    if(pageMapping){
        pageMapping->setEnabled(!running);
    }
    if(pageExecute){
        pageExecute->setEnabled(true);
    }
    if(stack && pageExecute){
        stack->setCurrentWidget(pageExecute);
    }
    updateNavigation();
}

void DataSyncDialog::loadConnections()
{
    const auto connections = ConnectionManager::instance()->connections();
    auto fillCombo = [&](QComboBox *combo) {
        combo->clear();
        for(const auto &info : connections){
            combo->addItem(info.name, info.name);
        }
    };
    fillCombo(sourceConnCombo);
    fillCombo(targetConnCombo);
    if(!connections.isEmpty()){
        sourceConnCombo->setCurrentIndex(0);
        targetConnCombo->setCurrentIndex(connections.size() > 1 ? 1 : 0);
    }
    populateDatabaseCombo(sourceDbCombo, sourceConnCombo->currentData().toString(), QString());
    populateDatabaseCombo(targetDbCombo, targetConnCombo->currentData().toString(), QString());
    updateSummaryLabels();
}

void DataSyncDialog::populateDatabaseCombo(QComboBox *combo,
                                           const QString &connName,
                                           const QString &preferredDb)
{
    if(!combo){
        return;
    }
    combo->clear();
    if(connName.isEmpty()){
        return;
    }
    const ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    QString error;
    QStringList dbs = ConnectionManager::instance()->fetchDatabases(info, &error);
    if(dbs.isEmpty() && !info.defaultDb.isEmpty()){
        dbs << info.defaultDb;
    }
    if(dbs.isEmpty()){
        combo->addItem(trLang(QStringLiteral("[空]"), QStringLiteral("[Empty]")), QString());
    }else{
        for(const auto &db : dbs){
            combo->addItem(db, db);
        }
        int targetIndex = 0;
        if(!preferredDb.isEmpty()){
            targetIndex = combo->findText(preferredDb, Qt::MatchFixedString);
            if(targetIndex < 0){
                targetIndex = 0;
            }
        }
        combo->setCurrentIndex(targetIndex);
    }
    updateSummaryLabels();
}

void DataSyncDialog::refreshMappingData()
{
    mappings.clear();
    const QString connName = sourceConnCombo->currentData().toString();
    const QString dbName = sourceDbCombo->currentText();
    if(connName.isEmpty() || dbName.isEmpty()){
        QMessageBox::warning(this,
                             trLang(QStringLiteral("加载表"), QStringLiteral("Load Tables")),
                             trLang(QStringLiteral("请先选择有效的源连接和数据库。"),
                                    QStringLiteral("Select a valid source connection and database first.")));
        return;
    }
    const ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    QString error;
    QStringList tables = ConnectionManager::instance()->fetchTables(info, dbName, &error);
    if(tables.isEmpty()){
        if(error.isEmpty()){
            QMessageBox::information(this,
                                     trLang(QStringLiteral("加载表"), QStringLiteral("Load Tables")),
                                     trLang(QStringLiteral("未找到任何表。"),
                                            QStringLiteral("No tables found.")));
        }else{
            QMessageBox::warning(this,
                                 trLang(QStringLiteral("加载表"), QStringLiteral("Load Tables")),
                                 error);
        }
    }
    for(const auto &table : tables){
        const bool hinted = !sourceHintTable.isEmpty() &&
                (table.compare(sourceHintTable, Qt::CaseInsensitive) == 0);
        TableMappingEntry entry;
        entry.sourceTable = table;
        entry.targetTable = table;
        entry.createTable = hinted;
        entry.enabled = hinted;
        mappings.append(entry);
    }
    rebuildMappingTable();
    updateSummaryLabels();
}

void DataSyncDialog::rebuildMappingTable()
{
    if(!mappingTable){
        return;
    }
    mappingTable->blockSignals(true);
    mappingTable->setRowCount(mappings.size());
    for(int row = 0; row < mappings.size(); ++row){
        const auto &entry = mappings.at(row);
        auto *srcItem = new QTableWidgetItem(entry.sourceTable);
        srcItem->setFlags((srcItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        srcItem->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        auto *targetItem = new QTableWidgetItem(entry.targetTable);
        auto *createItem = new QTableWidgetItem;
        createItem->setFlags((createItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        createItem->setCheckState(entry.createTable ? Qt::Checked : Qt::Unchecked);
        auto *mappingItem = new QTableWidgetItem(entry.mappingLabel);
        mappingTable->setItem(row, 0, srcItem);
        mappingTable->setItem(row, 1, targetItem);
        mappingTable->setItem(row, 2, createItem);
        mappingTable->setItem(row, 3, mappingItem);
    }
    mappingTable->blockSignals(false);
}

void DataSyncDialog::setEntryEnabled(int row, bool value)
{
    if(row < 0 || row >= mappings.size()){
        return;
    }
    mappings[row].enabled = value;
    if(!mappingTable){
        return;
    }
    QTableWidgetItem *item = mappingTable->item(row, 0);
    if(!item){
        return;
    }
    QSignalBlocker blocker(mappingTable);
    item->setCheckState(value ? Qt::Checked : Qt::Unchecked);
}

void DataSyncDialog::setCreateFlag(int row, bool value)
{
    if(row < 0 || row >= mappings.size()){
        return;
    }
    mappings[row].createTable = value;
    if(!mappingTable){
        return;
    }
    QTableWidgetItem *item = mappingTable->item(row, 2);
    if(!item){
        return;
    }
    QSignalBlocker blocker(mappingTable);
    item->setCheckState(value ? Qt::Checked : Qt::Unchecked);
}

void DataSyncDialog::updateCreateTableState(int row, bool value)
{
    if(row < 0 || row >= mappings.size()){
        return;
    }
    mappings[row].createTable = value;
}

bool DataSyncDialog::ensureTargetTable(const TableMappingEntry &entry,
                                       QSqlDatabase &sourceDb,
                                       QSqlDatabase &targetDb,
                                       const QString &sourceDbName,
                                       const QString &targetDbName,
                                       QString *errorMessage)
{
    const QString targetTable = entry.targetTable.isEmpty() ? entry.sourceTable : entry.targetTable;
    if(targetTable.isEmpty()){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("目标表名称为空。"),
                                   QStringLiteral("Target table name is empty."));
        }
        return false;
    }
    const QString checkSql = QStringLiteral("SELECT 1 FROM information_schema.tables "
                                            "WHERE table_schema = ? AND table_name = ? LIMIT 1");
    QSqlQuery checkQuery(targetDb);
    checkQuery.prepare(checkSql);
    checkQuery.addBindValue(targetDbName);
    checkQuery.addBindValue(targetTable);
    if(!checkQuery.exec()){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("检查目标表失败：%1"),
                                   QStringLiteral("Failed to check target table: %1"))
                    .arg(checkQuery.lastError().text());
        }
        return false;
    }
    if(checkQuery.next()){
        return true;
    }
    if(!entry.createTable){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("目标表 %1 不存在，且未启用自动创建。"),
                                   QStringLiteral("Target table %1 does not exist and auto-create is disabled."))
                    .arg(targetTable);
        }
        return false;
    }

    QSqlQuery schemaQuery(sourceDb);
    const QString showSql = QStringLiteral("SHOW CREATE TABLE %1")
            .arg(qualifiedTable(sourceDbName, entry.sourceTable));
    if(!schemaQuery.exec(showSql) || !schemaQuery.next()){
        if(errorMessage){
            const QString detail = schemaQuery.lastError().isValid()
                    ? schemaQuery.lastError().text()
                    : trLang(QStringLiteral("无法读取源表结构。"),
                             QStringLiteral("Unable to read source table schema."));
            *errorMessage = trLang(QStringLiteral("读取源表结构失败：%1"),
                                   QStringLiteral("Failed to read source table schema: %1"))
                    .arg(detail);
        }
        return false;
    }

    QString createSql = schemaQuery.value(1).toString();
    if(createSql.isEmpty()){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("源表结构定义为空。"),
                                   QStringLiteral("Source table definition is empty."));
        }
        return false;
    }

    const QString quotedSource = QStringLiteral("`%1`").arg(entry.sourceTable);
    const QString quotedTarget = QStringLiteral("`%1`").arg(targetTable);
    createSql.replace(quotedSource, quotedTarget);

    QSqlQuery createQuery(targetDb);
    if(!createQuery.exec(createSql)){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("创建目标表失败：%1"),
                                   QStringLiteral("Failed to create target table: %1"))
                    .arg(createQuery.lastError().text());
        }
        return false;
    }
    return true;
}

bool DataSyncDialog::clearTargetTable(const QString &targetDbName,
                                      const QString &targetTable,
                                      QSqlDatabase &targetDb,
                                      bool useTruncate,
                                      QString *errorMessage)
{
    if(targetTable.isEmpty()){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("目标表名称为空。"),
                                   QStringLiteral("Target table name is empty."));
        }
        return false;
    }
    const QString qualified = qualifiedTable(targetDbName, targetTable);
    const QString sql = useTruncate
            ? QStringLiteral("TRUNCATE TABLE %1").arg(qualified)
            : QStringLiteral("DELETE FROM %1").arg(qualified);
    QSqlQuery query(targetDb);
    if(!query.exec(sql)){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("清空目标表失败：%1"),
                                   QStringLiteral("Failed to empty target table: %1"))
                    .arg(query.lastError().text());
        }
        return false;
    }
    return true;
}

bool DataSyncDialog::copyTableData(const TableMappingEntry &entry,
                                   QSqlDatabase &sourceDb,
                                   QSqlDatabase &targetDb,
                                   const QString &sourceDbName,
                                   const QString &targetDbName,
                                   int batchSize,
                                   bool continueOnError,
                                   qint64 *rowsCopied,
                                   QString *errorMessage,
                                   const std::function<void (const QString &)> &logCallback)
{
    const QString sourceQualified = qualifiedTable(sourceDbName, entry.sourceTable);
    const QString targetTable = entry.targetTable.isEmpty() ? entry.sourceTable : entry.targetTable;
    const QString targetQualified = qualifiedTable(targetDbName, targetTable);
    if(targetTable.isEmpty()){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("目标表名称为空。"),
                                   QStringLiteral("Target table name is empty."));
        }
        return false;
    }

    QSqlQuery selectQuery(sourceDb);
    if(!selectQuery.exec(QStringLiteral("SELECT * FROM %1").arg(sourceQualified))){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("读取源表失败：%1"),
                                   QStringLiteral("Failed to read source table: %1"))
                    .arg(selectQuery.lastError().text());
        }
        return false;
    }

    const QSqlRecord record = selectQuery.record();
    const int columnCount = record.count();
    if(columnCount == 0){
        if(rowsCopied){
            *rowsCopied = 0;
        }
        return true;
    }

    QStringList columnNames;
    QStringList placeholders;
    for(int i = 0; i < columnCount; ++i){
        columnNames << escapeIdentifier(record.fieldName(i));
        placeholders << QStringLiteral("?");
    }

    const QString insertSql = QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
            .arg(targetQualified,
                 columnNames.join(QStringLiteral(", ")),
                 placeholders.join(QStringLiteral(", ")));

    QSqlQuery insertQuery(targetDb);
    if(!insertQuery.prepare(insertSql)){
        if(errorMessage){
            *errorMessage = trLang(QStringLiteral("准备写入语句失败：%1"),
                                   QStringLiteral("Failed to prepare insert statement: %1"))
                    .arg(insertQuery.lastError().text());
        }
        return false;
    }

    if(batchSize <= 0){
        batchSize = 1000;
    }

    bool inTransaction = targetDb.transaction();
    if(!inTransaction){
        if(logCallback){
            logCallback(trLang(QStringLiteral("[WARN] 无法开启目标库事务，写入将逐条提交。"),
                               QStringLiteral("[WARN] Unable to start transaction on target DB, rows will be committed individually.")));
        }
    }

    qint64 totalRows = 0;
    int pending = 0;
    bool hadError = false;
    QString firstError;

    while(selectQuery.next()){
        for(int i = 0; i < columnCount; ++i){
            insertQuery.bindValue(i, selectQuery.value(i));
        }
        if(!insertQuery.exec()){
            hadError = true;
            const QString detail = insertQuery.lastError().text();
            if(firstError.isEmpty()){
                firstError = trLang(QStringLiteral("写入第 %1 行失败：%2"),
                                    QStringLiteral("Failed to insert row %1: %2"))
                        .arg(totalRows + 1)
                        .arg(detail);
            }
            if(logCallback){
                logCallback(trLang(QStringLiteral("  [WARN] 第 %1 行写入失败：%2"),
                                   QStringLiteral("  [WARN] Row %1 failed to insert: %2"))
                            .arg(totalRows + 1)
                            .arg(detail));
            }
            if(!continueOnError){
                if(inTransaction){
                    targetDb.rollback();
                }
                if(errorMessage){
                    *errorMessage = firstError;
                }
                return false;
            }
            continue;
        }

        ++totalRows;
        ++pending;
        if(inTransaction && pending >= batchSize){
            if(!targetDb.commit()){
                if(errorMessage){
                    *errorMessage = trLang(QStringLiteral("提交批次失败：%1"),
                                           QStringLiteral("Failed to commit batch: %1"))
                            .arg(targetDb.lastError().text());
                }
                return false;
            }
            if(!targetDb.transaction()){
                if(logCallback){
                    logCallback(trLang(QStringLiteral("[WARN] 无法重新开启事务，后续插入将直接提交。"),
                                       QStringLiteral("[WARN] Unable to restart transaction, subsequent inserts will autocommit.")));
                }
                inTransaction = false;
            }
            pending = 0;
        }
    }

    if(inTransaction){
        if(!targetDb.commit()){
            if(errorMessage){
                *errorMessage = trLang(QStringLiteral("提交最终事务失败：%1"),
                                       QStringLiteral("Failed to commit final transaction: %1"))
                        .arg(targetDb.lastError().text());
            }
            return false;
        }
    }

    if(rowsCopied){
        *rowsCopied = totalRows;
    }
    if(hadError){
        if(errorMessage && !firstError.isEmpty()){
            *errorMessage = firstError;
        }
        return false;
    }
    return true;
}

void DataSyncDialog::appendLogMessage(const QString &message)
{
    if(!logEdit){
        return;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, message));
}
DataSyncWorker::DataSyncWorker(DataSyncDialog *dialog,
                               QVector<DataSyncDialog::TableMappingEntry> tasks,
                               const DataSyncOptions &options)
    : QObject(nullptr)
    , m_dialog(dialog)
    , m_tasks(std::move(tasks))
    , m_options(options)
{
}

void DataSyncWorker::process()
{
    auto log = [this](const QString &msg) {
        emit logMessage(msg);
    };

    if(m_tasks.isEmpty()){
        const QString summary = trLang(QStringLiteral("未勾选任何待同步的表。"),
                                       QStringLiteral("No tables selected for synchronization."));
        log(summary);
        emit finished(true, summary, 0, 0, 0);
        return;
    }

    int successTables = 0;
    int failedTables = 0;
    qint64 totalRows = 0;
    bool aborted = false;
    QString abortMessage;

    const QString sourceHandle = uniqueConnectionName(QStringLiteral("datasync_src_worker"));
    const QString targetHandle = uniqueConnectionName(QStringLiteral("datasync_tgt_worker"));

    {
        QSqlDatabase sourceDb = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), sourceHandle);
        QSqlDatabase targetDb = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), targetHandle);

        QString errorText;
        if(!configureDatabase(sourceDb, m_options.sourceInfo, m_options.sourceDbName, &errorText)){
            aborted = true;
            abortMessage = trLang(QStringLiteral("连接源数据库失败：%1"),
                                  QStringLiteral("Failed to connect to source database: %1")).arg(errorText);
        }else if(!configureDatabase(targetDb, m_options.targetInfo, m_options.targetDbName, &errorText)){
            aborted = true;
            abortMessage = trLang(QStringLiteral("连接目标数据库失败：%1"),
                                  QStringLiteral("Failed to connect to target database: %1")).arg(errorText);
        }else{
            if(m_options.strictMode){
                QSqlQuery modeQuery(targetDb);
                if(!modeQuery.exec(QStringLiteral("SET SESSION sql_mode=''"))){
                    log(trLang(QStringLiteral("[WARN] 无法关闭目标端严格模式：%1"),
                               QStringLiteral("[WARN] Unable to disable strict mode on target: %1"))
                        .arg(modeQuery.lastError().text()));
                }
            }

            const int totalTasks = m_tasks.size();
            int currentTask = 0;
            for(const auto &entry : std::as_const(m_tasks)){
                emit progressChanged(currentTask, totalTasks);
                ++currentTask;
                const QString targetTable = entry.targetTable.isEmpty() ? entry.sourceTable : entry.targetTable;
                log(trLang(QStringLiteral("%1 -> %2"), QStringLiteral("%1 -> %2"))
                    .arg(entry.sourceTable, targetTable));
                QString stepError;

                if(!m_dialog->ensureTargetTable(entry, sourceDb, targetDb,
                                                m_options.sourceDbName, m_options.targetDbName, &stepError)){
                    log(trLang(QStringLiteral("  [ERROR] %1"), QStringLiteral("  [ERROR] %1")).arg(stepError));
                    ++failedTables;
                    if(!m_options.continueOnError){
                        aborted = true;
                        abortMessage = stepError;
                        break;
                    }
                    continue;
                }

                if(m_options.emptyTarget){
                    if(!m_dialog->clearTargetTable(m_options.targetDbName, targetTable, targetDb,
                                                   m_options.useTruncate, &stepError)){
                        log(trLang(QStringLiteral("  [ERROR] %1"), QStringLiteral("  [ERROR] %1")).arg(stepError));
                        ++failedTables;
                        if(!m_options.continueOnError){
                            aborted = true;
                            abortMessage = stepError;
                            break;
                        }
                        continue;
                    }
                }

                qint64 copiedRows = 0;
                auto warnLogger = [this](const QString &msg) {
                    emit logMessage(msg);
                };
                if(!m_dialog->copyTableData(entry, sourceDb, targetDb,
                                            m_options.sourceDbName, m_options.targetDbName,
                                            m_options.batchSize, m_options.continueOnError,
                                            &copiedRows, &stepError, warnLogger)){
                    log(trLang(QStringLiteral("  [ERROR] %1"), QStringLiteral("  [ERROR] %1")).arg(stepError));
                    ++failedTables;
                    if(!m_options.continueOnError){
                        aborted = true;
                        abortMessage = stepError;
                        break;
                    }
                    continue;
                }

                log(trLang(QStringLiteral("  [OK] %1 行。"),
                           QStringLiteral("  [OK] %1 rows.")).arg(copiedRows));
                totalRows += copiedRows;
                ++successTables;
            }
        }

        sourceDb.close();
        targetDb.close();
    }

    QSqlDatabase::removeDatabase(sourceHandle);
    QSqlDatabase::removeDatabase(targetHandle);

    QString summary;
    if(aborted){
        summary = abortMessage;
    }else if(failedTables > 0){
        summary = trLang(QStringLiteral("同步结束：成功 %1 个表，失败 %2 个。"),
                         QStringLiteral("Sync finished: %1 tables succeeded, %2 failed."))
                .arg(successTables)
                .arg(failedTables);
    }else{
        summary = trLang(QStringLiteral("同步完成，成功 %1 个表，共 %2 行。"),
                         QStringLiteral("Sync completed: %1 tables, %2 rows."))
                .arg(successTables)
                .arg(totalRows);
    }

    log(summary);
    emit progressChanged(m_tasks.size(), m_tasks.size());
    emit finished(aborted, summary, successTables, failedTables, totalRows);
}
