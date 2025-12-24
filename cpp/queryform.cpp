#include "queryform.h"
#include "mainwindow.h"
#include "flowlayout.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QGridLayout>
#include <QFontDatabase>
#include <QMessageBox>
#include <QInputDialog>
#include <QSet>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableView>
#include <QHeaderView>
#include <QRegularExpression>
#include <QScrollArea>
#include <QMenu>
#include <QShortcut>
#include <QClipboard>
#include <QApplication>
#include <QItemSelectionModel>
#include <QVector>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTextStream>
#include <QUuid>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlField>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

// Custom delegate to remove frame from editor
class NoFrameItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (auto *lineEdit = qobject_cast<QLineEdit *>(editor)) {
            lineEdit->setFrame(false);
            lineEdit->setTextMargins(0, 0, 0, 0);
            lineEdit->setContentsMargins(0, 0, 0, 0);
            lineEdit->setStyleSheet(QStringLiteral(
                "QLineEdit { "
                "background-color: #ffffff; "
                "border: none; "
                "border-radius: 0px; "
                "padding: 0px 2px; "
                "margin: 0px; "
                "}"
            ));
        }
        return editor;
    }
};

// MySQL data types for structure editor (complete list, alphabetical)
static const QStringList kMySqlDataTypes = {
    QStringLiteral("bigint"), QStringLiteral("binary()"), QStringLiteral("bit"),
    QStringLiteral("blob"), QStringLiteral("bool"), QStringLiteral("boolean"),
    QStringLiteral("char()"), QStringLiteral("date"), QStringLiteral("datetime"),
    QStringLiteral("dec"), QStringLiteral("decimal"), QStringLiteral("double"),
    QStringLiteral("enum()"), QStringLiteral("fixed"), QStringLiteral("float"),
    QStringLiteral("geometry"), QStringLiteral("geometrycollection"),
    QStringLiteral("int"), QStringLiteral("integer"), QStringLiteral("json"),
    QStringLiteral("linestring"), QStringLiteral("longblob"), QStringLiteral("longtext"),
    QStringLiteral("mediumblob"), QStringLiteral("mediumint"), QStringLiteral("mediumtext"),
    QStringLiteral("multilinestring"), QStringLiteral("multipoint"), QStringLiteral("multipolygon"),
    QStringLiteral("numeric"), QStringLiteral("point"), QStringLiteral("polygon"),
    QStringLiteral("real"), QStringLiteral("set()"), QStringLiteral("smallint"),
    QStringLiteral("text"), QStringLiteral("time"), QStringLiteral("timestamp"),
    QStringLiteral("tinyblob"), QStringLiteral("tinyint"), QStringLiteral("tinytext"),
    QStringLiteral("varbinary()"), QStringLiteral("varchar()"), QStringLiteral("year")
};

// Delegate for Type column with editable combobox
class TypeComboDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->setEditable(true);
        combo->addItems(kMySqlDataTypes);
        combo->setFrame(false);
        combo->setStyleSheet(QStringLiteral(
            "QComboBox { background: #fff; border: none; padding: 0 2px; }"
            "QComboBox::drop-down { border: none; width: 16px; }"
        ));
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if(combo){
            combo->setCurrentText(index.data(Qt::EditRole).toString());
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = qobject_cast<QComboBox *>(editor);
        if(combo){
            model->setData(index, combo->currentText(), Qt::EditRole);
        }
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }
};

namespace {
QStringList sampleHeaders()
{
    return {"id", "product_name", "report_month", "title", "pdf_url"};
}

QList<QStringList> sampleRows()
{
    return {
        {"154", "PP", "2025-11", QObject::tr("Market insight report"), "https://example.com/pp.pdf"},
        {"155", "\u78e8\u7c73\u6027PP", "2025-11", QObject::tr("PP monthly update"), "https://example.com/pp_update.pdf"},
        {"156", QObject::tr("ABS \u69fd"), "2025-11", QObject::tr("ABS market trend"), "https://example.com/abs.pdf"},
        {"157", QObject::tr("PVC \u4ef7\u683c"), "2025-11", QObject::tr("PVC weekly report"), "https://example.com/pvc.pdf"}
    };
}

QString escapeIdentifier(const QString &name)
{
    QString value = name;
    value.replace(QLatin1Char('`'), QStringLiteral("``"));
    return QStringLiteral("`%1`").arg(value);
}

QString qualifiedName(const QString &dbName, const QString &tableName)
{
    if(dbName.isEmpty()){
        return escapeIdentifier(tableName);
    }
    return QStringLiteral("%1.%2").arg(escapeIdentifier(dbName), escapeIdentifier(tableName));
}
}

QueryForm::QueryForm(QWidget *parent, Mode mode, TableAction fixedAction) :
    QWidget(parent),
    m_mode(mode),
    m_fixedInspectAction(fixedAction)
{
    initialiseUi();
    populateConnections();
    connect(ConnectionManager::instance(), &ConnectionManager::connectionsChanged,
            this, &QueryForm::populateConnections);

    connect(textEdit->document(), &QTextDocument::modificationChanged, this, [this](bool modified) {
        emit modifiedStateChanged(modified);
        updateTitleFromEditor();
    });

    connect(connCombo, &QComboBox::currentTextChanged, this, [=](const QString &) {
        updateDatabaseList();
        updateTitleFromEditor();
    });
    connect(dbCombo, &QComboBox::currentTextChanged, this, [this](const QString &){
        updateTitleFromEditor();
        updateCompletionList();
    });

    if(m_mode == InspectMode){
        prepareInspectOnlyUi();
    }
}

QueryForm::~QueryForm()
{
    closeAllInspectTabs();
}

QString QueryForm::title() const
{
    return m_title;
}

bool QueryForm::isModified() const
{
    return textEdit->document()->isModified();
}

QString QueryForm::filePath() const
{
    return textEdit->filePath();
}

QString QueryForm::codecName() const
{
    return textEdit->codecName();
}

void QueryForm::setConnection(const QString &connName, const QString &dbName)
{
    if(!connName.isEmpty()){
        int idx = connCombo->findText(connName);
        if(idx >= 0){
            connCombo->setCurrentIndex(idx);
        }else{
            connCombo->addItem(connName);
            connCombo->setCurrentIndex(connCombo->count() - 1);
        }
    }
    updateDatabaseList();
    if(!dbName.isEmpty()){
        int idx = dbCombo->findText(dbName);
        if(idx >= 0){
            dbCombo->setCurrentIndex(idx);
        }
    }
    updateTitleFromEditor();
}

bool QueryForm::loadFromFile(const QString &filePath, const QByteArray &codec)
{
    bool ok = textEdit->loadFromFile(filePath, codec);
    if(ok){
        updateTitleFromEditor();
    }
    return ok;
}

bool QueryForm::saveToFile(const QString &filePath, const QByteArray &codec)
{
    bool ok = textEdit->saveToFile(filePath, codec);
    if(ok){
        updateTitleFromEditor();
    }
    return ok;
}

MyEdit *QueryForm::editor() const
{
    return textEdit;
}

void QueryForm::openInspectTab(const QString &connName,
                               const QString &dbName,
                               const QString &tableName,
                               TableAction action)
{
    if(m_mode == QueryMode){
        setConnection(connName, dbName);
    }
    if(tableName.isEmpty()){
        showStatus(tr("请选择有效的表。"), 4000);
        return;
    }
    const QString resolvedConn = (m_mode == QueryMode) ? connCombo->currentText() : connName;
    ConnectionInfo info = ConnectionManager::instance()->connection(resolvedConn);
    if(info.name.isEmpty()){
        showStatus(tr("连接不存在: %1").arg(resolvedConn), 4000);
        return;
    }
    QString targetDb = dbName;
    if(targetDb.isEmpty()){
        targetDb = info.defaultDb;
    }
    if(targetDb.isEmpty()){
        showStatus(tr("连接 %1 未配置默认数据库。").arg(info.name), 5000);
        return;
    }
    TableAction mode = action == NoneAction ? ViewData : action;
    if(m_mode == InspectMode && m_fixedInspectAction != NoneAction){
        mode = m_fixedInspectAction;
    }
    inspectConn = info.name;
    inspectDb = targetDb;
    inspectTable = tableName;
    enterInspectMode(info.name, targetDb, tableName, mode);
}

void QueryForm::runQuery()
{
    if(inExecution){
        return;
    }
    const QString sql = textEdit->toPlainText().trimmed();
    if(sql.isEmpty()){
        resultForm->showMessage(tr("Input SQL statement first."));
        return;
    }

    ConnectionInfo info = currentConnectionInfo();
    if(info.name.isEmpty()){
        resultForm->showMessage(tr("Please select or create a connection."));
        return;
    }

    QString dbName = dbCombo->currentText().trimmed();
    if(dbName.isEmpty()){
        dbName = info.defaultDb;
    }

    inExecution = true;
    runButton->setEnabled(false);
    stopButton->setEnabled(false);
    showStatus(tr("Executing on %1...").arg(info.name), 0);

    const QString connId = QStringLiteral("exec_%1_%2")
            .arg(info.name)
            .arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    if(!dbName.isEmpty()){
        db.setDatabaseName(dbName);
    }

    if(!db.open()){
        resultForm->showMessage(tr("Unable to connect: %1").arg(db.lastError().text()));
        showStatus(tr("Connection failed."), 5000);
        inExecution = false;
        runButton->setEnabled(true);
        stopButton->setEnabled(false);
        QSqlDatabase::removeDatabase(connId);
        return;
    }

    QElapsedTimer timer;
    timer.start();
    QSqlQuery query(db);
    if(!query.exec(sql)){
        resultForm->showMessage(tr("Query failed: %1").arg(query.lastError().text()));
        showStatus(tr("Query failed."), 5000);
    }else{
        qint64 elapsed = timer.elapsed();
        if(query.isSelect()){
            QStringList headers;
            const auto record = query.record();
            for(int i = 0; i < record.count(); ++i){
                headers << record.fieldName(i);
            }
            QList<QVariantList> rows;
            while(query.next()){
                QVariantList row;
                for(int col = 0; col < record.count(); ++col){
                    row << query.value(col);
                }
                rows << row;
            }
            resultForm->showRows(headers, rows, elapsed);
            showStatus(tr("Rows: %1, Time: %2 ms").arg(rows.count()).arg(elapsed), 7000);
        }else{
            const int affected = query.numRowsAffected();
            resultForm->showAffectRows(affected, elapsed);
            showStatus(tr("Affected rows: %1, Time: %2 ms").arg(affected).arg(elapsed), 7000);
        }
    }
    db.close();
    QSqlDatabase::removeDatabase(connId);

    inExecution = false;
    runButton->setEnabled(true);
    stopButton->setEnabled(false);
}

void QueryForm::stopQuery()
{
    emit requestStatusMessage(tr("Stop is not available for synchronous execution."), 2000);
    runButton->setEnabled(true);
    stopButton->setEnabled(false);
}

void QueryForm::formatSql()
{
    QString text = textEdit->toPlainText();
    text = text.trimmed();
    textEdit->setPlainText(text + QLatin1Char('\n'));
}

void QueryForm::updateTitleFromEditor()
{
    QString name = textEdit->filePath();
    if(name.isEmpty()){
        name = connCombo->currentText();
    }
    if(name.isEmpty()){
        name = tr("Untitled");
    }
    m_title = QFileInfo(name).fileName();
    emit titleChanged(m_title);
}

void QueryForm::initialiseUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(0);

    pageStack = new QStackedWidget(this);
    layout->addWidget(pageStack);

    queryPage = buildQueryPage();
    inspectPage = buildInspectPage();

    pageStack->addWidget(queryPage);
    pageStack->addWidget(inspectPage);
    pageStack->setCurrentWidget(queryPage);

    connect(runButton, &QToolButton::clicked, this, &QueryForm::runQuery);
    connect(stopButton, &QToolButton::clicked, this, &QueryForm::stopQuery);
    connect(formatButton, &QToolButton::clicked, this, &QueryForm::formatSql);
    connect(textEdit, &QPlainTextEdit::cursorPositionChanged, this, &QueryForm::updateTitleFromEditor);

    if(inspectBackButton){
        connect(inspectBackButton, &QToolButton::clicked, this, &QueryForm::exitInspectMode);
    }
}

QWidget *QueryForm::buildQueryPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    connCombo = new QComboBox(page);
    connCombo->setEditable(false);
    connCombo->setMinimumWidth(140);
    dbCombo = new QComboBox(page);
    dbCombo->setMinimumWidth(120);

    runButton = new QToolButton(page);
    runButton->setToolTip(tr("Run Query (F5)"));
    runButton->setIcon(QIcon(QStringLiteral(":/images/run.svg")));
    runButton->setIconSize(QSize(24, 24));
    runButton->setMinimumSize(36, 36);
    runButton->setToolButtonStyle(Qt::ToolButtonIconOnly);

    stopButton = new QToolButton(page);
    stopButton->setToolTip(tr("Stop Query"));
    stopButton->setIcon(QIcon(QStringLiteral(":/images/stop.svg")));
    stopButton->setIconSize(QSize(24, 24));
    stopButton->setMinimumSize(36, 36);
    stopButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopButton->setEnabled(false);

    autoCommitCheck = new QCheckBox(tr("AutoCommit"), page);
    autoCommitCheck->setChecked(true);

    formatButton = new QToolButton(page);
    formatButton->setToolTip(tr("Format SQL"));
    formatButton->setIcon(QIcon(QStringLiteral(":/images/format.svg")));
    formatButton->setIconSize(QSize(24, 24));
    formatButton->setMinimumSize(36, 36);
    formatButton->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto *connIcon = new QLabel(page);
    connIcon->setPixmap(QIcon(QStringLiteral(":/images/connection.svg")).pixmap(16, 16));
    toolbar->addWidget(connIcon);
    toolbar->addWidget(connCombo, 1);
    auto *dbIcon = new QLabel(page);
    dbIcon->setPixmap(QIcon(QStringLiteral(":/images/database.svg")).pixmap(16, 16));
    toolbar->addWidget(dbIcon);
    toolbar->addWidget(dbCombo, 1);
    toolbar->addSpacing(8);
    toolbar->addWidget(runButton);
    toolbar->addWidget(stopButton);
    toolbar->addWidget(formatButton);
    toolbar->addSpacing(8);
    toolbar->addWidget(autoCommitCheck);
    toolbar->addStretch();

    layout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Vertical, page);
    textEdit = new MyEdit(splitter);
    textEdit->setPlaceholderText(tr("-- Type SQL here"));

    resultForm = new ResultForm(splitter);
    resultForm->setToolbarVisible(false);
    connect(resultForm, &ResultForm::summaryChanged, this, [this](const QString &text) {
        emit requestStatusMessage(text, 0);
    });
    splitter->addWidget(textEdit);
    splitter->addWidget(resultForm);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    layout->addWidget(splitter);
    return page;
}

QWidget *QueryForm::buildInspectPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);
    inspectBackButton = new QToolButton(page);
    inspectBackButton->setText(tr("返回SQL"));
    toolbar->addWidget(inspectBackButton);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    inspectTabContainer = new QWidget(page);
    inspectTabFlow = new FlowLayout(inspectTabContainer, 0, 6, 6);
    inspectTabContainer->setLayout(inspectTabFlow);
    inspectTabContainer->setVisible(false);
    layout->addWidget(inspectTabContainer);

    inspectStack = new QStackedWidget(page);
    inspectPlaceholder = new QLabel(tr("在左侧选择数据库和表以浏览。"), page);
    inspectPlaceholder->setAlignment(Qt::AlignCenter);
    inspectPlaceholder->setStyleSheet(QStringLiteral("color:#666666;font-size:12pt;"));
    layout->addWidget(inspectPlaceholder, 1);
    layout->addWidget(inspectStack, 1);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->addStretch();
    auto *closeButton = new QPushButton(tr("关闭"), page);
    inspectCloseButton = closeButton;
    footer->addWidget(closeButton);
    layout->addLayout(footer);
    connect(closeButton, &QPushButton::clicked, this, &QueryForm::exitInspectMode);

    return page;
}

QueryForm::InspectPane *QueryForm::createInspectPane(const QString &connName,
                                                     const QString &dbName,
                                                     const QString &tableName,
                                                     TableAction action)
{
    if(!inspectStack || !inspectTabFlow){
        return nullptr;
    }
    auto *pane = new InspectPane;
    pane->connName = connName;
    pane->dbName = dbName;
    pane->tableName = tableName;
    pane->currentAction = action == NoneAction ? ViewData : action;
    pane->widget = new QWidget(inspectStack);
    auto *layout = new QVBoxLayout(pane->widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *titleLayout = new QHBoxLayout;
    pane->titleLabel = new QLabel(pane->widget);
    pane->titleLabel->setStyleSheet(QStringLiteral("font-size:14pt;font-weight:600;"));
    pane->subtitleLabel = new QLabel(pane->widget);
    pane->subtitleLabel->setStyleSheet(QStringLiteral("color:#666666;font-size:12pt;"));
    titleLayout->addWidget(pane->titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(pane->subtitleLabel);
    layout->addLayout(titleLayout);

    const bool showViewToggle = !(m_mode == InspectMode && m_fixedInspectAction != NoneAction);
    if(showViewToggle){
        auto *viewLayout = new QHBoxLayout;
        viewLayout->setSpacing(6);
        viewLayout->addWidget(new QLabel(tr("视图:"), pane->widget));
        pane->viewDataButton = new QToolButton(pane->widget);
        pane->viewDataButton->setText(tr("数据"));
        pane->viewDataButton->setCheckable(true);
        pane->viewStructureButton = new QToolButton(pane->widget);
        pane->viewStructureButton->setText(tr("表结构"));
        pane->viewStructureButton->setCheckable(true);
        pane->viewDataButton->setChecked(pane->currentAction != ViewStructure);
        pane->viewStructureButton->setChecked(pane->currentAction == ViewStructure);
        viewLayout->addWidget(pane->viewStructureButton);
        viewLayout->addWidget(pane->viewDataButton);
        viewLayout->addStretch();
        layout->addLayout(viewLayout);
    }

    pane->viewStack = new QStackedWidget(pane->widget);
    layout->addWidget(pane->viewStack, 1);

    pane->dataPage = new QWidget(pane->viewStack);
    auto *dataLayout = new QVBoxLayout(pane->dataPage);
    dataLayout->setContentsMargins(0, 0, 0, 0);
    dataLayout->setSpacing(6);

    auto *dataToolbar = new QHBoxLayout;
    dataToolbar->setSpacing(6);
    // Fetch buttons
    pane->fetchFirstButton = new QToolButton(pane->dataPage);
    pane->fetchFirstButton->setToolTip(tr("第一页"));
    pane->fetchFirstButton->setIcon(QIcon(QStringLiteral(":/images/fetch-first.svg")));
    pane->fetchFirstButton->setIconSize(QSize(24, 24));
    pane->fetchFirstButton->setMinimumSize(32, 32);
    pane->fetchNextButton = new QToolButton(pane->dataPage);
    pane->fetchNextButton->setToolTip(tr("下一页"));
    pane->fetchNextButton->setIcon(QIcon(QStringLiteral(":/images/fetch-next.svg")));
    pane->fetchNextButton->setIconSize(QSize(24, 24));
    pane->fetchNextButton->setMinimumSize(32, 32);
    pane->fetchAllButton = new QToolButton(pane->dataPage);
    pane->fetchAllButton->setToolTip(tr("全部"));
    pane->fetchAllButton->setIcon(QIcon(QStringLiteral(":/images/fetch-all.svg")));
    pane->fetchAllButton->setIconSize(QSize(24, 24));
    pane->fetchAllButton->setMinimumSize(32, 32);
    pane->fetchLastButton = new QToolButton(pane->dataPage);
    pane->fetchLastButton->setToolTip(tr("最后一页"));
    pane->fetchLastButton->setIcon(QIcon(QStringLiteral(":/images/fetch-last.svg")));
    pane->fetchLastButton->setIconSize(QSize(24, 24));
    pane->fetchLastButton->setMinimumSize(32, 32);
    pane->pageEdit = new QLineEdit(pane->dataPage);
    pane->pageEdit->setFixedWidth(60);
    pane->pageEdit->setAlignment(Qt::AlignCenter);
    pane->pageEdit->setPlaceholderText(tr("页"));
    pane->pageEdit->setText(QStringLiteral("1"));
    pane->pageEdit->setValidator(new QIntValidator(1, 999999, pane->pageEdit));
    pane->refreshButton = new QToolButton(pane->dataPage);
    pane->refreshButton->setToolTip(tr("刷新"));
    pane->refreshButton->setIcon(QIcon(QStringLiteral(":/images/refresh.svg")));
    pane->refreshButton->setIconSize(QSize(28, 28));
    pane->refreshButton->setMinimumSize(40, 40);
    pane->addRowButton = new QToolButton(pane->dataPage);
    pane->addRowButton->setToolTip(tr("新增行"));
    pane->addRowButton->setIcon(QIcon(QStringLiteral(":/images/add.svg")));
    pane->addRowButton->setIconSize(QSize(28, 28));
    pane->addRowButton->setMinimumSize(40, 40);
    pane->duplicateRowButton = new QToolButton(pane->dataPage);
    pane->duplicateRowButton->setToolTip(tr("复制行"));
    pane->duplicateRowButton->setIcon(QIcon(QStringLiteral(":/images/copy.svg")));
    pane->duplicateRowButton->setIconSize(QSize(28, 28));
    pane->duplicateRowButton->setMinimumSize(40, 40);
    pane->deleteRowButton = new QToolButton(pane->dataPage);
    pane->deleteRowButton->setToolTip(tr("删除行"));
    pane->deleteRowButton->setIcon(QIcon(QStringLiteral(":/images/delete.svg")));
    pane->deleteRowButton->setIconSize(QSize(28, 28));
    pane->deleteRowButton->setMinimumSize(40, 40);
    pane->saveRowsButton = new QToolButton(pane->dataPage);
    pane->saveRowsButton->setToolTip(tr("保存"));
    pane->saveRowsButton->setIcon(QIcon(QStringLiteral(":/images/save.svg")));
    pane->saveRowsButton->setIconSize(QSize(28, 28));
    pane->saveRowsButton->setMinimumSize(40, 40);
    pane->discardRowsButton = new QToolButton(pane->dataPage);
    pane->discardRowsButton->setToolTip(tr("撤销更改"));
    pane->discardRowsButton->setIcon(QIcon(QStringLiteral(":/images/undo.svg")));
    pane->discardRowsButton->setIconSize(QSize(28, 28));
    pane->discardRowsButton->setMinimumSize(40, 40);
    pane->sortCombo = new QComboBox(pane->dataPage);
    pane->sortCombo->setMinimumWidth(160);
    pane->sortAscButton = new QToolButton(pane->dataPage);
    pane->sortAscButton->setToolTip(tr("升序"));
    pane->sortAscButton->setIcon(QIcon(QStringLiteral(":/images/sort-up.svg")));
    pane->sortAscButton->setIconSize(QSize(28, 28));
    pane->sortAscButton->setMinimumSize(40, 40);
    pane->sortDescButton = new QToolButton(pane->dataPage);
    pane->sortDescButton->setToolTip(tr("降序"));
    pane->sortDescButton->setIcon(QIcon(QStringLiteral(":/images/sort-down.svg")));
    pane->sortDescButton->setIconSize(QSize(28, 28));
    pane->sortDescButton->setMinimumSize(40, 40);
    pane->filterEdit = new QLineEdit(pane->dataPage);
    pane->filterEdit->setPlaceholderText(tr("搜索当前页"));
    pane->whereSearchButton = new QToolButton(pane->dataPage);
    pane->whereSearchButton->setIcon(QIcon(QStringLiteral(":/images/filter.svg")));
    pane->whereSearchButton->setIconSize(QSize(28, 28));
    pane->whereSearchButton->setToolTip(tr("数据库条件查询"));
    pane->whereSearchButton->setCheckable(true);

    dataToolbar->addWidget(pane->fetchFirstButton);
    dataToolbar->addWidget(pane->fetchNextButton);
    dataToolbar->addWidget(pane->fetchAllButton);
    dataToolbar->addWidget(pane->fetchLastButton);
    dataToolbar->addWidget(pane->pageEdit);
    dataToolbar->addWidget(pane->refreshButton);
    dataToolbar->addWidget(pane->whereSearchButton);
    dataToolbar->addSpacing(12);
    dataToolbar->addWidget(pane->addRowButton);
    dataToolbar->addWidget(pane->duplicateRowButton);
    dataToolbar->addWidget(pane->deleteRowButton);
    dataToolbar->addSpacing(12);
    dataToolbar->addWidget(pane->saveRowsButton);
    dataToolbar->addWidget(pane->discardRowsButton);
    dataToolbar->addSpacing(12);
    dataToolbar->addWidget(new QLabel(tr("排序列:"), pane->dataPage));
    dataToolbar->addWidget(pane->sortCombo);
    dataToolbar->addWidget(pane->sortAscButton);
    dataToolbar->addWidget(pane->sortDescButton);
    dataToolbar->addStretch(1);
    dataToolbar->addWidget(new QLabel(tr("页内搜索:"), pane->dataPage));
    pane->filterEdit->setMinimumWidth(400);
    dataToolbar->addWidget(pane->filterEdit);
    dataLayout->addLayout(dataToolbar);

    auto *whereLayout = new QHBoxLayout();
    whereLayout->setContentsMargins(0, 0, 0, 0);
    whereLayout->setSpacing(6);
    pane->whereEdit = new MyEdit(pane->dataPage);
    pane->whereEdit->setPlaceholderText(tr("输入WHERE条件，例如: id > 10 and name like '%test%'"));
    pane->whereEdit->setMaximumHeight(60);
    pane->whereApplyButton = new QPushButton(tr("应用"), pane->dataPage);
    pane->whereClearButton = new QPushButton(tr("Clear"), pane->dataPage);
    whereLayout->addWidget(pane->whereEdit, 1);
    whereLayout->addWidget(pane->whereApplyButton);
    whereLayout->addWidget(pane->whereClearButton);
    auto *whereContainer = new QWidget(pane->dataPage);
    whereContainer->setLayout(whereLayout);
    whereContainer->setVisible(false);
    dataLayout->addWidget(whereContainer);

    pane->resultForm = new ResultForm(pane->dataPage);
    pane->resultForm->setToolbarVisible(false);
    pane->resultForm->setSelectionBehavior(QAbstractItemView::SelectRows);
    pane->resultForm->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(pane->resultForm, &ResultForm::summaryChanged, this, [this](const QString &text) {
        emit requestStatusMessage(text, 0);
    });
    dataLayout->addWidget(pane->resultForm, 1);
    if(auto *table = pane->resultForm->tableWidget()){
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(table, &QWidget::customContextMenuRequested, this, [this, pane](const QPoint &pos) {
            showDataContextMenu(pane, pos);
        });
        // Ctrl+C to copy selected rows
        auto *copyShortcut = new QShortcut(QKeySequence::Copy, table);
        connect(copyShortcut, &QShortcut::activated, this, [this, pane]() {
            copyRowsToClipboard(pane);
        });
        // Ctrl+V to paste rows
        auto *pasteShortcut = new QShortcut(QKeySequence::Paste, table);
        connect(pasteShortcut, &QShortcut::activated, this, [this, pane]() {
            pasteRowsFromClipboard(pane);
        });
    }
    pane->viewStack->addWidget(pane->dataPage);

    pane->structurePage = new QWidget(pane->viewStack);
    auto *structureLayout = new QVBoxLayout(pane->structurePage);
    structureLayout->setContentsMargins(0, 0, 0, 0);
    structureLayout->setSpacing(6);

    pane->structureTabs = new QTabWidget(pane->structurePage);
    pane->structureTabs->setObjectName(QStringLiteral("inspectStructureTabs"));
    structureLayout->addWidget(pane->structureTabs, 1);

    auto *generalTab = new QWidget(pane->structureTabs);
    auto *generalLayout = new QVBoxLayout(generalTab);
    generalLayout->setContentsMargins(8, 8, 8, 8);
    generalLayout->setSpacing(8);

    auto *infoGrid = new QGridLayout;
    infoGrid->setHorizontalSpacing(12);
    infoGrid->setVerticalSpacing(6);
    infoGrid->addWidget(new QLabel(tr("表名:"), generalTab), 0, 0);
    pane->structureTableNameEdit = new QLineEdit(generalTab);
    pane->structureTableNameEdit->setReadOnly(true);
    infoGrid->addWidget(pane->structureTableNameEdit, 0, 1);
    infoGrid->addWidget(new QLabel(tr("数据库:"), generalTab), 0, 2);
    pane->structureDatabaseEdit = new QLineEdit(generalTab);
    pane->structureDatabaseEdit->setReadOnly(true);
    infoGrid->addWidget(pane->structureDatabaseEdit, 0, 3);
    infoGrid->addWidget(new QLabel(tr("备注:"), generalTab), 1, 0);
    pane->structureCommentEdit = new QLineEdit(generalTab);
    pane->structureCommentEdit->setReadOnly(true);
    infoGrid->addWidget(pane->structureCommentEdit, 1, 1, 1, 3);
    generalLayout->addLayout(infoGrid);

    auto *columnsBar = new QHBoxLayout;
    columnsBar->setSpacing(6);
    auto makeActionButton = [generalTab](const QString &text) {
        auto *btn = new QToolButton(generalTab);
        btn->setText(text);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };
    pane->structureAddButton = makeActionButton(tr("+ 添加"));
    pane->structureRemoveButton = makeActionButton(tr("- 删除"));
    pane->structureUpButton = makeActionButton(tr("↑ Up"));
    pane->structureDownButton = makeActionButton(tr("↓ Down"));
    columnsBar->addWidget(pane->structureAddButton);
    columnsBar->addWidget(pane->structureRemoveButton);
    columnsBar->addWidget(pane->structureUpButton);
    columnsBar->addWidget(pane->structureDownButton);
    columnsBar->addSpacing(12);
    columnsBar->addWidget(new QLabel(tr("Regex Filter:"), generalTab));
    pane->structureFilterEdit = new QLineEdit(generalTab);
    pane->structureFilterEdit->setPlaceholderText(tr("输入正则过滤列"));
    columnsBar->addWidget(pane->structureFilterEdit, 1);
    generalLayout->addLayout(columnsBar);

    pane->structureTable = new QTableWidget(generalTab);
    pane->structureTable->setItemDelegate(new NoFrameItemDelegate(pane->structureTable));
    pane->structureTable->setItemDelegateForColumn(1, new TypeComboDelegate(pane->structureTable));
    pane->structureTable->setColumnCount(10);
    pane->structureTable->setHorizontalHeaderLabels({
        tr("Name"), tr("Type"), tr("Unsigned"), tr("Zerofill"),
        tr("Not Null"), tr("Key"), tr("Auto Inc"), tr("Default/Expr"),
        tr("Generated"), tr("Comment")
    });
    pane->structureTable->horizontalHeader()->setStretchLastSection(true);
    pane->structureTable->horizontalHeader()->setMinimumSectionSize(80);
    pane->structureTable->verticalHeader()->setDefaultSectionSize(26);
    pane->structureTable->setAlternatingRowColors(true);
    pane->structureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pane->structureTable->setSelectionMode(QAbstractItemView::SingleSelection);
    pane->structureTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
    pane->structureTable->setStyleSheet(QStringLiteral(
        "QTableWidget {"
        "  background: #fdfdfd;"
        "  border: 1px solid #dfe4ea;"
        "  gridline-color: #e5e9f2;"
        "  alternate-background-color: #f6f9ff;"
        "}"
        "QTableWidget::item:selected {"
        "  background: #d0e8ff;"
        "}"
    ));
    generalLayout->addWidget(pane->structureTable, 1);

    auto *structureFooter = new QHBoxLayout;
    structureFooter->setSpacing(10);
    pane->structureSaveButton = new QPushButton(tr("保存"), generalTab);
    pane->structureReloadButton = new QPushButton(tr("刷新"), generalTab);
    pane->structureCloseButton = new QPushButton(tr("关闭"), generalTab);
    pane->structureSaveButton->setEnabled(false);
    structureFooter->addWidget(pane->structureSaveButton);
    structureFooter->addWidget(pane->structureReloadButton);
    structureFooter->addWidget(pane->structureCloseButton);
    structureFooter->addStretch();
    generalLayout->addLayout(structureFooter);

    pane->structureTabs->addTab(generalTab, tr("常规"));

    auto makeTabButton = [](QWidget *parent, const QString &text) {
        auto *btn = new QToolButton(parent);
        btn->setText(text);
        return btn;
    };

    auto createListTab = [&](const QStringList &headers,
                             ResultForm **resultPtr,
                             QToolButton **addBtn,
                             QToolButton **delBtn) -> QWidget * {
        auto *tab = new QWidget(pane->structureTabs);
        auto *tabLayout = new QVBoxLayout(tab);
        tabLayout->setContentsMargins(8, 8, 8, 8);
        tabLayout->setSpacing(6);
        auto *toolbar = new QHBoxLayout;
        toolbar->setSpacing(6);
        *addBtn = makeTabButton(tab, tr("+ 添加"));
        *delBtn = makeTabButton(tab, tr("- 删除"));
        (*delBtn)->setEnabled(false);
        toolbar->addWidget(*addBtn);
        toolbar->addWidget(*delBtn);
        toolbar->addStretch();
        tabLayout->addLayout(toolbar);
        *resultPtr = new ResultForm(tab);
        (*resultPtr)->setToolbarVisible(false);
        tabLayout->addWidget(*resultPtr, 1);
        if(headers.isEmpty()){
            (*resultPtr)->showMessage(tr("加载中..."));
        }
        return tab;
    };

    // Indexes tab with editable QTableWidget
    {
        auto *indexTab = new QWidget(pane->structureTabs);
        auto *indexLayout = new QVBoxLayout(indexTab);
        indexLayout->setContentsMargins(8, 8, 8, 8);
        indexLayout->setSpacing(6);
        auto *toolbar = new QHBoxLayout;
        toolbar->setSpacing(6);
        pane->indexAddButton = makeTabButton(indexTab, tr("+ Add"));
        pane->indexDeleteButton = makeTabButton(indexTab, tr("- Remove"));
        pane->indexDeleteButton->setEnabled(false);
        toolbar->addWidget(pane->indexAddButton);
        toolbar->addWidget(pane->indexDeleteButton);
        toolbar->addStretch();
        indexLayout->addLayout(toolbar);
        pane->indexTable = new QTableWidget(indexTab);
        pane->indexTable->setColumnCount(6);
        pane->indexTable->setHorizontalHeaderLabels({tr("Name"), tr("Columns"), QString(), tr("Type"), tr("Index method"), tr("Comment")});
        pane->indexTable->horizontalHeader()->setStretchLastSection(true);
        pane->indexTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        pane->indexTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        pane->indexTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        pane->indexTable->horizontalHeader()->resizeSection(2, 30);
        pane->indexTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        pane->indexTable->setSelectionMode(QAbstractItemView::SingleSelection);
        pane->indexTable->verticalHeader()->setVisible(true);
        connect(pane->indexTable, &QTableWidget::itemChanged, this, [this, pane]() {
            if(!pane->indexBlockSignals){
                updateIndexDirtyState(pane);
            }
        });
        indexLayout->addWidget(pane->indexTable, 1);
        auto *bottomBar = new QHBoxLayout;
        pane->indexSaveButton = new QPushButton(tr("保存"), indexTab);
        pane->indexSaveButton->setEnabled(false);
        pane->indexRefreshButton = new QPushButton(tr("刷新"), indexTab);
        pane->indexCloseButton = new QPushButton(tr("关闭"), indexTab);
        bottomBar->addWidget(pane->indexSaveButton);
        bottomBar->addWidget(pane->indexRefreshButton);
        bottomBar->addWidget(pane->indexCloseButton);
        bottomBar->addStretch();
        indexLayout->addLayout(bottomBar);
        pane->structureTabs->addTab(indexTab, tr("Indexes"));
    }
    pane->structureTabs->addTab(
        createListTab({tr("Name"), tr("Columns"), tr("引用数据库"), tr("引用表"), tr("引用列")},
                      &pane->foreignResult, &pane->foreignAddButton, &pane->foreignDeleteButton),
        tr("Foreign Keys"));
    pane->structureTabs->addTab(
        createListTab({tr("Name"), tr("Timing"), tr("Insert"), tr("Update"), tr("Delete"), tr("Trigger body")},
                      &pane->triggerResult, &pane->triggerAddButton, &pane->triggerDeleteButton),
        tr("Triggers"));

    auto *optionTab = new QWidget(pane->structureTabs);
    auto *optionLayout = new QGridLayout(optionTab);
    optionLayout->setContentsMargins(6, 6, 6, 6);
    optionLayout->setHorizontalSpacing(8);
    optionLayout->setVerticalSpacing(4);
    auto createOptionEdit = [&](QLineEdit **target) {
        *target = new QLineEdit(optionTab);
        (*target)->setReadOnly(true);
        (*target)->setPlaceholderText(tr("--"));
        return *target;
    };
    int row = 0;
    optionLayout->addWidget(new QLabel(tr("Engine:"), optionTab), row, 0);
    createOptionEdit(&pane->optionEngineEdit);
    optionLayout->addWidget(pane->optionEngineEdit, row, 1);
    optionLayout->addWidget(new QLabel(tr("Row format:"), optionTab), row, 2);
    createOptionEdit(&pane->optionRowFormatEdit);
    optionLayout->addWidget(pane->optionRowFormatEdit, row, 3);
    row++;
    optionLayout->addWidget(new QLabel(tr("Character Set:"), optionTab), row, 0);
    createOptionEdit(&pane->optionCharsetEdit);
    optionLayout->addWidget(pane->optionCharsetEdit, row, 1);
    optionLayout->addWidget(new QLabel(tr("collation:"), optionTab), row, 2);
    createOptionEdit(&pane->optionCollationEdit);
    optionLayout->addWidget(pane->optionCollationEdit, row, 3);
    row++;
    optionLayout->addWidget(new QLabel(tr("Auto Increment:"), optionTab), row, 0);
    createOptionEdit(&pane->optionAutoIncrementEdit);
    optionLayout->addWidget(pane->optionAutoIncrementEdit, row, 1);
    optionLayout->addWidget(new QLabel(tr("Avg row length:"), optionTab), row, 2);
    createOptionEdit(&pane->optionAvgRowLengthEdit);
    optionLayout->addWidget(pane->optionAvgRowLengthEdit, row, 3);
    row++;
    optionLayout->addWidget(new QLabel(tr("Table rows:"), optionTab), row, 0);
    createOptionEdit(&pane->optionTableRowsEdit);
    optionLayout->addWidget(pane->optionTableRowsEdit, row, 1);
    optionLayout->addWidget(new QLabel(tr("Max row count:"), optionTab), row, 2);
    createOptionEdit(&pane->optionMaxRowCountEdit);
    optionLayout->addWidget(pane->optionMaxRowCountEdit, row, 3);
    row++;
    optionLayout->addWidget(new QLabel(tr("Data length:"), optionTab), row, 0);
    createOptionEdit(&pane->optionDataLengthEdit);
    optionLayout->addWidget(pane->optionDataLengthEdit, row, 1);
    optionLayout->addWidget(new QLabel(tr("Data Free:"), optionTab), row, 2);
    createOptionEdit(&pane->optionDataFreeEdit);
    optionLayout->addWidget(pane->optionDataFreeEdit, row, 3);
    row++;
    optionLayout->addWidget(new QLabel(tr("Index Length:"), optionTab), row, 0);
    createOptionEdit(&pane->optionIndexLengthEdit);
    optionLayout->addWidget(pane->optionIndexLengthEdit, row, 1);
    optionLayout->addWidget(new QLabel(tr("Create Time:"), optionTab), row, 2);
    createOptionEdit(&pane->optionCreateTimeEdit);
    optionLayout->addWidget(pane->optionCreateTimeEdit, row, 3);
    row++;
    optionLayout->addWidget(new QLabel(tr("Update Time:"), optionTab), row, 0);
    createOptionEdit(&pane->optionUpdateTimeEdit);
    optionLayout->addWidget(pane->optionUpdateTimeEdit, row, 1);
    optionLayout->setRowStretch(row + 1, 1);
    pane->structureTabs->addTab(optionTab, tr("选项"));

    auto *ddlTab = new QWidget(pane->structureTabs);
    auto *ddlLayout = new QVBoxLayout(ddlTab);
    ddlLayout->setContentsMargins(8, 8, 8, 8);
    ddlLayout->setSpacing(6);
    pane->ddlEditor = new QPlainTextEdit(ddlTab);
    pane->ddlEditor->setReadOnly(true);
    pane->ddlEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pane->ddlEditor->setPlaceholderText(tr("-- 暂无 DDL 内容 --"));
    ddlLayout->addWidget(pane->ddlEditor, 1);
    pane->structureTabs->addTab(ddlTab, tr("DDL"));

    auto *previewTab = new QWidget(pane->structureTabs);
    auto *previewLayout = new QVBoxLayout(previewTab);
    previewLayout->setContentsMargins(8, 8, 8, 8);
    previewLayout->setSpacing(6);
    pane->sqlPreviewEditor = new QPlainTextEdit(previewTab);
    pane->sqlPreviewEditor->setReadOnly(true);
    pane->sqlPreviewEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pane->sqlPreviewEditor->setPlaceholderText(tr("-- 暂无 SQL 预览 --"));
    previewLayout->addWidget(pane->sqlPreviewEditor, 1);
    pane->structureTabs->addTab(previewTab, tr("SQL Preview"));
    pane->viewStack->addWidget(pane->structurePage);

    updateInspectView(pane);
    updateStructureButtons(pane);
    setupDataConnections(pane);
    updateDataButtons(pane);
    updateFetchButtons(pane);

    connect(pane->fetchFirstButton, &QToolButton::clicked, this, [this, pane]() {
        fetchFirst(pane);
    });
    connect(pane->fetchNextButton, &QToolButton::clicked, this, [this, pane]() {
        fetchNext(pane);
    });
    connect(pane->fetchAllButton, &QToolButton::clicked, this, [this, pane]() {
        fetchAll(pane);
    });
    connect(pane->fetchLastButton, &QToolButton::clicked, this, [this, pane]() {
        fetchLast(pane);
    });
    connect(pane->pageEdit, &QLineEdit::returnPressed, this, [this, pane]() {
        int page = pane->pageEdit->text().toInt();
        if(page < 1) page = 1;
        pane->dataOffset = (page - 1) * pane->dataLimit;
        refreshInspectData(pane);
    });
    connect(pane->refreshButton, &QToolButton::clicked, this, [this, pane]() {
        pane->dataOffset = 0; // Reset to first page on refresh
        refreshInspectData(pane);
    });
    if(pane->addRowButton){
        connect(pane->addRowButton, &QToolButton::clicked, this, [this, pane]() {
            addEmptyDataRow(pane);
        });
    }
    if(pane->duplicateRowButton){
        connect(pane->duplicateRowButton, &QToolButton::clicked, this, [this, pane]() {
            duplicateSelectedRow(pane);
        });
    }
    if(pane->deleteRowButton){
        connect(pane->deleteRowButton, &QToolButton::clicked, this, [this, pane]() {
            deleteSelectedRows(pane);
        });
    }
    if(pane->saveRowsButton){
        connect(pane->saveRowsButton, &QToolButton::clicked, this, [this, pane]() {
            saveDataChanges(pane);
        });
    }
    if(pane->discardRowsButton){
        connect(pane->discardRowsButton, &QToolButton::clicked, this, [this, pane]() {
            refreshInspectData(pane);
        });
    }
    if(pane->structureAddButton){
        connect(pane->structureAddButton, &QToolButton::clicked, this, [this, pane]() {
            handleStructureAdd(pane);
        });
    }
    if(pane->structureRemoveButton){
        connect(pane->structureRemoveButton, &QToolButton::clicked, this, [this, pane]() {
            handleStructureRemove(pane);
        });
    }
    if(pane->structureUpButton){
        connect(pane->structureUpButton, &QToolButton::clicked, this, [this, pane]() {
            handleStructureMove(pane, true);
        });
    }
    if(pane->structureDownButton){
        connect(pane->structureDownButton, &QToolButton::clicked, this, [this, pane]() {
            handleStructureMove(pane, false);
        });
    }
    if(pane->structureTable){
        connect(pane->structureTable, &QTableWidget::itemChanged, this, [this, pane](QTableWidgetItem *item) {
            if(!pane || pane->structureBlockSignals || !item){
                return;
            }
            const int row = item->row();
            if(row < 0 || row >= pane->structureWorkingColumns.size()){
                return;
            }
            auto &info = pane->structureWorkingColumns[row];
            const int column = item->column();
            switch(column){
            case 0:
                info.name = item->text().trimmed();
                break;
            case 1:
                info.type = item->text().trimmed();
                break;
            case 2:
                info.unsignedFlag = item->checkState() == Qt::Checked;
                item->setText(info.unsignedFlag ? tr("是") : tr("否"));
                break;
            case 3:
                info.zeroFill = item->checkState() == Qt::Checked;
                item->setText(info.zeroFill ? tr("是") : tr("否"));
                break;
            case 4:
                info.notNull = item->checkState() == Qt::Checked;
                item->setText(info.notNull ? tr("是") : tr("否"));
                break;
            case 5:
                info.key = item->checkState() == Qt::Checked;
                item->setText(info.key ? tr("是") : tr("否"));
                break;
            case 6:
                info.autoIncrement = item->checkState() == Qt::Checked;
                item->setText(info.autoIncrement ? tr("是") : tr("否"));
                break;
            case 7:
                info.defaultExpression = item->text();
                break;
            case 8:
                info.generated = item->checkState() == Qt::Checked;
                item->setText(info.generated ? tr("是") : tr("否"));
                break;
            case 9:
                info.comment = item->text();
                break;
            default:
                break;
            }
            pane->structureDirty = true;
            updateStructureDirtyState(pane);
        });
        if(auto *selection = pane->structureTable->selectionModel()){
            connect(selection, &QItemSelectionModel::currentRowChanged, this,
                    [this, pane](const QModelIndex &, const QModelIndex &) {
                updateStructureButtons(pane);
            });
            connect(selection, &QItemSelectionModel::selectionChanged, this,
                    [this, pane](const QItemSelection &, const QItemSelection &) {
                updateStructureButtons(pane);
            });
        }
    }
    if(pane->structureSaveButton){
        connect(pane->structureSaveButton, &QPushButton::clicked, this, [this, pane]() {
            saveStructureChanges(pane);
        });
    }
    if(pane->structureReloadButton){
        connect(pane->structureReloadButton, &QPushButton::clicked, this, [this, pane]() {
            if(!ensureStructureChangesHandled(pane)){
                return;
            }
            refreshInspectStructure(pane);
        });
    }
    if(pane->structureCloseButton){
        connect(pane->structureCloseButton, &QPushButton::clicked, this, [this, pane]() {
            closeInspectPane(pane);
        });
    }
    if(pane->indexAddButton){
        connect(pane->indexAddButton, &QToolButton::clicked, this, [this, pane]() {
            handleIndexAdd(pane);
        });
    }
    if(pane->indexDeleteButton){
        connect(pane->indexDeleteButton, &QToolButton::clicked, this, [this, pane]() {
            handleIndexDelete(pane);
        });
    }
    if(pane->indexSaveButton){
        connect(pane->indexSaveButton, &QPushButton::clicked, this, [this, pane]() {
            saveIndexChanges(pane);
        });
    }
    if(pane->indexRefreshButton){
        connect(pane->indexRefreshButton, &QPushButton::clicked, this, [this, pane]() {
            if(!ensureIndexChangesHandled(pane)){
                return;
            }
            populateIndexTable(pane);
        });
    }
    if(pane->indexCloseButton){
        connect(pane->indexCloseButton, &QPushButton::clicked, this, [this, pane]() {
            closeInspectPane(pane);
        });
    }
    if(pane->indexTable){
        connect(pane->indexTable, &QTableWidget::itemSelectionChanged, this, [pane]() {
            if(pane->indexDeleteButton){
                pane->indexDeleteButton->setEnabled(pane->indexTable->currentRow() >= 0);
            }
            const int selectedRow = pane->indexTable->currentRow();
            const QString selectedStyle = QStringLiteral("background-color: #cce5ff;");
            const QString normalStyle;
            for(int r = 0; r < pane->indexTable->rowCount(); ++r){
                const QString style = (r == selectedRow) ? selectedStyle : normalStyle;
                if(auto *w = pane->indexTable->cellWidget(r, 2)) w->setStyleSheet(style);
                if(auto *w = pane->indexTable->cellWidget(r, 3)) w->setStyleSheet(style);
                if(auto *w = pane->indexTable->cellWidget(r, 4)) w->setStyleSheet(style);
            }
        });
    }
    connect(pane->filterEdit, &QLineEdit::textChanged, this, [pane](const QString &text) {
        if(pane->resultForm){
            pane->resultForm->setFilterText(text);
        }
    });
    connect(pane->whereSearchButton, &QToolButton::toggled, this, [this, pane](bool checked) {
        if(auto *container = pane->whereEdit->parentWidget()){
            container->setVisible(checked);
        }
        if(!checked && !pane->whereClause.isEmpty()){
            pane->whereEdit->clear();
            pane->whereClause.clear();
            pane->dataOffset = 0;
            refreshInspectData(pane);
        }
    });
    connect(pane->whereEdit, &MyEdit::searchTriggered, this, [this, pane]() {
        pane->whereClause = pane->whereEdit->toPlainText().trimmed();
        pane->dataOffset = 0;
        refreshInspectData(pane);
    });
    connect(pane->whereApplyButton, &QPushButton::clicked, this, [this, pane]() {
        pane->whereClause = pane->whereEdit->toPlainText().trimmed();
        pane->dataOffset = 0;
        refreshInspectData(pane);
    });
    connect(pane->whereClearButton, &QPushButton::clicked, this, [this, pane]() {
        pane->whereEdit->clear();
        pane->whereClause.clear();
        pane->dataOffset = 0;
        refreshInspectData(pane);
    });
    if(pane->structureFilterEdit){
        connect(pane->structureFilterEdit, &QLineEdit::textChanged, this, [this, pane](const QString &) {
            applyStructureFilter(pane);
        });
    }
    if(pane->sortCombo){
        connect(pane->sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pane](int) {
            applyInspectSort(pane, Qt::AscendingOrder);
        });
    }
    if(pane->sortAscButton){
        connect(pane->sortAscButton, &QToolButton::clicked, this, [this, pane]() {
            applyInspectSort(pane, Qt::AscendingOrder);
        });
    }
    if(pane->sortDescButton){
        connect(pane->sortDescButton, &QToolButton::clicked, this, [this, pane]() {
            applyInspectSort(pane, Qt::DescendingOrder);
        });
    }
    if(pane->viewDataButton && pane->viewStructureButton){
        auto *viewGroup = new QButtonGroup(pane->widget);
        viewGroup->addButton(pane->viewDataButton, ViewData);
        viewGroup->addButton(pane->viewStructureButton, ViewStructure);
        connect(viewGroup, &QButtonGroup::idClicked, this, [this, pane](int id) {
            changeInspectView(pane, static_cast<TableAction>(id));
        });
    }

    if(pane->sortCombo){
        pane->sortCombo->setEnabled(false);
    }
    if(pane->sortAscButton){
        pane->sortAscButton->setEnabled(false);
    }
    if(pane->sortDescButton){
        pane->sortDescButton->setEnabled(false);
    }

    inspectPanes.append(pane);
    QString title = QStringLiteral("%1@%2").arg(tableName, connName);
    if(m_mode == InspectMode){
        title = QStringLiteral("%1@%2").arg(tableName, dbName);
    }
    const bool useTabFlow = (m_mode != InspectMode);
    if(useTabFlow){
        pane->tabWidget = new QWidget(inspectTabContainer);
        pane->tabWidget->setObjectName(QStringLiteral("inspectTabWidget"));
        pane->tabWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        auto *tabLayout = new QHBoxLayout(pane->tabWidget);
        tabLayout->setContentsMargins(14, 4, 10, 4);
        tabLayout->setSpacing(6);

        pane->tabButton = new QToolButton(pane->tabWidget);
        pane->tabButton->setText(title);
        pane->tabButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
        pane->tabButton->setAutoRaise(true);
        pane->tabButton->setCursor(Qt::PointingHandCursor);
        pane->tabButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

        pane->tabCloseButton = new QToolButton(pane->tabWidget);
        pane->tabCloseButton->setAutoRaise(true);
        pane->tabCloseButton->setText(QStringLiteral("×"));
        pane->tabCloseButton->setCursor(Qt::PointingHandCursor);
        pane->tabCloseButton->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  border:none;"
            "  color:#96A0AF;"
            "  font-size:14px;"
            "}"
            "QToolButton:hover {"
            "  color:#ff5c5c;"
            "}"
        ));

        tabLayout->addWidget(pane->tabButton, 1);
        tabLayout->addWidget(pane->tabCloseButton, 0);

        pane->tabWidget->setToolTip(title);
        pane->tabWidget->setStyleSheet(QStringLiteral(
            "#inspectTabWidget {"
            "  background:#f5f7fa;"
            "  border:1px solid #dfe4ea;"
            "  border-radius:4px;"
            "  color:#344563;"
            "  margin:4px 6px;"
            "  padding-left:4px;"
            "}"
            "#inspectTabWidget[selected=\"true\"] {"
            "  background:#ffffff;"
            "  border-color:#1a73e8;"
            "  color:#1a73e8;"
            "}"
            "#inspectTabWidget QToolButton {"
            "  border:none;"
            "  background:transparent;"
        "}"
            "#inspectTabWidget QToolButton:hover {"
            "  color:#1a73e8;"
            "}"
        ));
        pane->tabWidget->setProperty("selected", false);

        if(inspectTabFlow){
            inspectTabFlow->addWidget(pane->tabWidget);
        }
        pane->tabWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(pane->tabWidget, &QWidget::customContextMenuRequested, this, [this, pane](const QPoint &pos) {
            showInspectTabContextMenu(pane, pane->tabWidget->mapToGlobal(pos));
        });
        connect(pane->tabButton, &QToolButton::clicked, this, [this, pane]() {
            selectInspectPane(pane);
        });
        connect(pane->tabCloseButton, &QToolButton::clicked, this, [this, pane]() {
            closeInspectPane(pane);
        });
    }
    if(inspectStack){
        inspectStack->addWidget(pane->widget);
    }
    updateInspectPlaceholder();
    selectInspectPane(pane);
    return pane;
}

QueryForm::InspectPane *QueryForm::findInspectPane(const QString &connName,
                                                   const QString &dbName,
                                                   const QString &tableName) const
{
    for(auto *pane : inspectPanes){
        if(pane->connName == connName
                && pane->dbName == dbName
                && pane->tableName == tableName){
            return pane;
        }
    }
    return nullptr;
}

void QueryForm::selectInspectPane(InspectPane *pane)
{
    if(!pane){
        return;
    }
    if(inspectStack){
        inspectStack->setCurrentWidget(pane->widget);
    }
    for(auto *other : inspectPanes){
        const bool active = (other == pane);
        if(other->tabWidget){
            other->tabWidget->setProperty("selected", active);
            other->tabWidget->style()->unpolish(other->tabWidget);
            other->tabWidget->style()->polish(other->tabWidget);
            other->tabWidget->update();
        }
    }
    updateInspectPlaceholder();
    if(pane->titleLabel){
        m_title = pane->titleLabel->text();
        emit titleChanged(m_title);
    }
    if(m_mode == QueryMode && pageStack && inspectPage){
        pageStack->setCurrentWidget(inspectPage);
    }
}

void QueryForm::populateConnections()
{
    connCombo->clear();
    const auto conns = ConnectionManager::instance()->connections();
    for(const auto &info : conns){
        connCombo->addItem(info.name);
    }
    if(connCombo->count() > 0){
        updateDatabaseList();
    }else{
        dbCombo->clear();
    }
}

void QueryForm::updateDatabaseList()
{
    dbCombo->clear();
    ConnectionInfo info = currentConnectionInfo();
    if(info.name.isEmpty()){
        return;
    }
    QString msg;
    const auto dbs = ConnectionManager::instance()->fetchDatabases(info, &msg);
    if(!dbs.isEmpty()){
        dbCombo->addItems(dbs);
    }else if(!info.defaultDb.isEmpty()){
        dbCombo->addItem(info.defaultDb);
    }else if(!msg.isEmpty()){
        showStatus(tr("Failed to list databases: %1").arg(msg), 5000);
    }
    if(dbCombo->count() == 0){
        dbCombo->addItem(QString());
    }
    updateCompletionList();
}

void QueryForm::updateCompletionList()
{
    static const QStringList sqlKeywords = {
        QStringLiteral("select * from"), QStringLiteral("select"),
        QStringLiteral("from"), QStringLiteral("where"),
        QStringLiteral("insert into"), QStringLiteral("insert"),
        QStringLiteral("into"), QStringLiteral("values"),
        QStringLiteral("update"), QStringLiteral("set"), QStringLiteral("delete"),
        QStringLiteral("create table"), QStringLiteral("create"),
        QStringLiteral("table"), QStringLiteral("alter"),
        QStringLiteral("drop"), QStringLiteral("index"), QStringLiteral("view"),
        QStringLiteral("database"), QStringLiteral("schema"), QStringLiteral("truncate"),
        QStringLiteral("left join"), QStringLiteral("right join"),
        QStringLiteral("inner join"), QStringLiteral("outer join"),
        QStringLiteral("join"), QStringLiteral("left"), QStringLiteral("right"),
        QStringLiteral("inner"), QStringLiteral("outer"), QStringLiteral("cross"),
        QStringLiteral("on"), QStringLiteral("and"), QStringLiteral("or"),
        QStringLiteral("not"), QStringLiteral("null"), QStringLiteral("is"),
        QStringLiteral("in"), QStringLiteral("like"), QStringLiteral("between"),
        QStringLiteral("exists"), QStringLiteral("having"),
        QStringLiteral("group by"), QStringLiteral("group"),
        QStringLiteral("order by"), QStringLiteral("order"),
        QStringLiteral("by"), QStringLiteral("asc"), QStringLiteral("desc"),
        QStringLiteral("limit"), QStringLiteral("offset"),
        QStringLiteral("distinct"), QStringLiteral("as"), QStringLiteral("case"),
        QStringLiteral("when"), QStringLiteral("then"), QStringLiteral("else"),
        QStringLiteral("end"), QStringLiteral("union all"), QStringLiteral("union"),
        QStringLiteral("all"), QStringLiteral("primary key"), QStringLiteral("primary"),
        QStringLiteral("key"), QStringLiteral("foreign key"), QStringLiteral("foreign"),
        QStringLiteral("references"), QStringLiteral("unique"), QStringLiteral("default"),
        QStringLiteral("auto_increment"), QStringLiteral("comment"), QStringLiteral("engine"),
        QStringLiteral("charset"), QStringLiteral("collate"), QStringLiteral("if"),
        QStringLiteral("count"), QStringLiteral("sum"), QStringLiteral("avg"),
        QStringLiteral("max"), QStringLiteral("min"), QStringLiteral("concat"),
        QStringLiteral("substring"), QStringLiteral("length"), QStringLiteral("replace"),
        QStringLiteral("coalesce"), QStringLiteral("ifnull"), QStringLiteral("now"),
        QStringLiteral("date"), QStringLiteral("time"), QStringLiteral("datetime"),
        QStringLiteral("timestamp"), QStringLiteral("year"), QStringLiteral("month"),
        QStringLiteral("day"), QStringLiteral("hour"), QStringLiteral("minute"),
        QStringLiteral("second"), QStringLiteral("varchar"), QStringLiteral("int"),
        QStringLiteral("bigint"), QStringLiteral("decimal"), QStringLiteral("float"),
        QStringLiteral("double"), QStringLiteral("text"), QStringLiteral("blob"),
        QStringLiteral("boolean"), QStringLiteral("enum"), QStringLiteral("explain"),
        QStringLiteral("show"), QStringLiteral("describe"), QStringLiteral("use")
    };

    QList<MyEdit::CompletionItem> items;
    QList<MyEdit::CompletionItem> keywordItems;
    for(const QString &kw : sqlKeywords){
        keywordItems.append({kw, MyEdit::KeywordType, QString(), QString()});
    }

    ConnectionInfo info = currentConnectionInfo();
    const QString dbName = dbCombo->currentText();
    if(info.name.isEmpty() || dbName.isEmpty()){
        textEdit->setCompletionItems(keywordItems);
        return;
    }

    QStringList tableNames;
    QString connHandle = QStringLiteral("completion_%1").arg(quintptr(this), 0, 16);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connHandle);
        db.setHostName(info.host);
        db.setPort(info.port);
        db.setUserName(info.user);
        db.setPassword(info.password);
        db.setDatabaseName(dbName);
        if(db.open()){
            QSqlQuery query(db);
            if(query.exec(QStringLiteral("SHOW TABLES"))){
                while(query.next()){
                    tableNames << query.value(0).toString();
                }
            }
            for(const QString &tableName : qAsConst(tableNames)){
                QSqlQuery colQuery(db);
                if(colQuery.exec(QStringLiteral("SHOW COLUMNS FROM `%1`").arg(tableName))){
                    while(colQuery.next()){
                        const QString colName = colQuery.value(0).toString();
                        const QString colType = colQuery.value(1).toString().toUpper();
                        items.append({colName, MyEdit::ColumnType, colType, tableName});
                    }
                }
            }
            for(const QString &tableName : qAsConst(tableNames)){
                items.append({tableName, MyEdit::TableType, QString(), QString()});
            }
        }
    }
    QSqlDatabase::removeDatabase(connHandle);
    items.append(keywordItems);
    textEdit->setCompletionItems(items);
}

ConnectionInfo QueryForm::currentConnectionInfo() const
{
    return ConnectionManager::instance()->connection(connCombo->currentText());
}

void QueryForm::showStatus(const QString &text, int timeout)
{
    emit requestStatusMessage(text, timeout);
}

void QueryForm::showSampleResult()
{
    resultForm->showRows(sampleHeaders(), sampleRows(), 0);
}

void QueryForm::enterInspectMode(const QString &connName,
                                 const QString &dbName,
                                 const QString &tableName,
                                 TableAction action)
{
    if(!inspectStack || !inspectTabFlow){
        return;
    }
    if(m_mode == InspectMode){
        if(!closeAllInspectTabs()){
            return;
        }
    }
    const TableAction targetAction = action == NoneAction ? ViewData : action;
    QString titleText = QStringLiteral("%1@%2").arg(tableName, connName);
    if(m_mode == InspectMode){
        titleText = QStringLiteral("%1@%2").arg(tableName, dbName);
    }
    InspectPane *pane = findInspectPane(connName, dbName, tableName);
    if(!pane){
        pane = createInspectPane(connName, dbName, tableName, targetAction);
    }
    if(!pane){
        return;
    }
    pane->connName = connName;
    pane->dbName = dbName;
    pane->tableName = tableName;
    pane->currentAction = targetAction;
    pane->titleLabel->setText(titleText);
    pane->subtitleLabel->setText(dbName.isEmpty()
                                 ? tr("数据库未指定")
                                 : tr("数据库: %1").arg(dbName));
    pane->filterEdit->clear();
    pane->resultForm->reset();
    if(pane->structureFilterEdit){
        pane->structureFilterEdit->clear();
    }
    if(pane->indexTable){
        pane->indexTable->setRowCount(0);
    }
    if(pane->foreignResult){
        pane->foreignResult->reset();
    }
    if(pane->triggerResult){
        pane->triggerResult->reset();
    }
    if(pane->ddlEditor){
        pane->ddlEditor->clear();
    }
    if(pane->sqlPreviewEditor){
        pane->sqlPreviewEditor->clear();
    }
    if(pane->structureTableNameEdit){
        pane->structureTableNameEdit->setText(tableName);
    }
    if(pane->structureDatabaseEdit){
        pane->structureDatabaseEdit->setText(dbName);
    }
    if(pane->structureCommentEdit){
        pane->structureCommentEdit->clear();
    }
    pane->structureOriginalColumns.clear();
    pane->structureWorkingColumns.clear();
    pane->structurePendingSql.clear();
    pane->structureDirty = false;
    if(pane->structureTable){
        pane->structureTable->clearContents();
        pane->structureTable->setRowCount(0);
    }
    updateStructureButtons(pane);
    auto clearOptionField = [](QLineEdit *edit) {
        if(edit){
            edit->clear();
        }
    };
    clearOptionField(pane->optionEngineEdit);
    clearOptionField(pane->optionRowFormatEdit);
    clearOptionField(pane->optionCharsetEdit);
    clearOptionField(pane->optionCollationEdit);
    clearOptionField(pane->optionAutoIncrementEdit);
    clearOptionField(pane->optionAvgRowLengthEdit);
    clearOptionField(pane->optionTableRowsEdit);
    clearOptionField(pane->optionMaxRowCountEdit);
    clearOptionField(pane->optionDataLengthEdit);
    clearOptionField(pane->optionDataFreeEdit);
    clearOptionField(pane->optionIndexLengthEdit);
    clearOptionField(pane->optionCreateTimeEdit);
    clearOptionField(pane->optionUpdateTimeEdit);
    if(pane->viewDataButton){
        pane->viewDataButton->setChecked(targetAction != ViewStructure);
    }
    if(pane->viewStructureButton){
        pane->viewStructureButton->setChecked(targetAction == ViewStructure);
    }
    if(pane->tabButton){
        pane->tabButton->setText(titleText);
    }
    if(pane->tabWidget){
        pane->tabWidget->setToolTip(dbName.isEmpty()
                                    ? titleText
                                    : QStringLiteral("%1\nDB: %2").arg(titleText, dbName));
    }
    updateInspectView(pane);
    selectInspectPane(pane);
    refreshInspectData(pane);
    m_title = titleText;
    emit titleChanged(m_title);
}

void QueryForm::exitInspectMode()
{
    if(pageStack){
        pageStack->setCurrentWidget(queryPage);
    }
    updateTitleFromEditor();
}

void QueryForm::refreshInspectData(InspectPane *pane)
{
    if(!pane){
        return;
    }
    auto resetDataState = [this, pane]() {
        pane->dataRowStates.clear();
        pane->dataHeaders.clear();
        pane->dataHeaderIndex.clear();
        pane->dataPrimaryKeys.clear();
        pane->dataDirty = false;
        pane->dataOffset = 0;
        pane->hasMoreData = false;
        updateDataButtons(pane);
        updateFetchButtons(pane);
    };
    if(pane->currentAction == ViewStructure){
        refreshInspectStructure(pane);
        return;
    }
    if(!pane->resultForm){
        return;
    }
    if(pane->connName.isEmpty() || pane->tableName.isEmpty()){
        pane->resultForm->showMessage(tr("请选择左侧的表。"));
        resetDataState();
        return;
    }
    ConnectionInfo info = ConnectionManager::instance()->connection(pane->connName);
    if(info.name.isEmpty()){
        pane->resultForm->showMessage(tr("连接 %1 不存在。").arg(pane->connName));
        resetDataState();
        return;
    }
    QString dbName = pane->dbName;
    if(dbName.isEmpty()){
        dbName = info.defaultDb;
    }
    if(dbName.isEmpty()){
        pane->resultForm->showMessage(tr("连接 %1 未配置默认数据库。").arg(info.name));
        resetDataState();
        return;
    }

    pane->resultForm->showMessage(tr("正在加载 %1...").arg(pane->tableName));
    const QString connId = QStringLiteral("inspect_%1_%2_%3")
            .arg(info.name,
                 pane->tableName,
                 QString::number(QDateTime::currentMSecsSinceEpoch()));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    db.setDatabaseName(dbName);

    if(!db.open()){
        pane->resultForm->showMessage(tr("连接失败: %1").arg(db.lastError().text()));
        QSqlDatabase::removeDatabase(connId);
        resetDataState();
        return;
    }

    QString sql;
    if(pane->currentAction == ViewStructure){
        sql = QStringLiteral("SHOW FULL COLUMNS FROM %1 FROM %2;")
                .arg(escapeIdentifier(pane->tableName), escapeIdentifier(dbName));
    }else{
        // Use LIMIT with +1 to detect if there's more data
        if(pane->whereClause.isEmpty()){
            sql = QStringLiteral("SELECT * FROM %1 LIMIT %2 OFFSET %3;")
                    .arg(qualifiedName(dbName, pane->tableName))
                    .arg(pane->dataLimit + 1)
                    .arg(pane->dataOffset);
        }else{
            sql = QStringLiteral("SELECT * FROM %1 WHERE %2 LIMIT %3 OFFSET %4;")
                    .arg(qualifiedName(dbName, pane->tableName))
                    .arg(pane->whereClause)
                    .arg(pane->dataLimit + 1)
                    .arg(pane->dataOffset);
        }
    }

    QElapsedTimer timer;
    timer.start();
    QSqlQuery query(db);
    if(!query.exec(sql)){
        pane->resultForm->showMessage(tr("查询失败: %1").arg(query.lastError().text()));
        db.close();
        QSqlDatabase::removeDatabase(connId);
        resetDataState();
        return;
    }
    const qint64 elapsed = timer.elapsed();
    if(pane->currentAction == ViewStructure){
        const auto columns = parseTableStructure(query);
        pane->resultForm->showTableStructure(columns, elapsed);
    }else{
        QStringList headers;
        QVector<int> columnTypes;
        const auto record = query.record();
        for(int i = 0; i < record.count(); ++i){
            headers << record.fieldName(i);
            columnTypes << static_cast<int>(record.field(i).type());
        }
        QList<QVariantList> rows;
        while(query.next()){
            QVariantList row;
            for(int col = 0; col < record.count(); ++col){
                row << query.value(col);
            }
            rows << row;
        }
        // Check if there's more data (we fetched limit+1)
        pane->hasMoreData = (rows.size() > pane->dataLimit);
        if(pane->hasMoreData){
            rows.removeLast(); // Remove the extra row
        }
        QString note;
        if(pane->dataOffset == 0 && !pane->hasMoreData){
            note = tr("共 %1 行").arg(rows.size());
        } else {
            note = tr("第 %1-%2 行").arg(pane->dataOffset + 1).arg(pane->dataOffset + rows.size());
            if(pane->hasMoreData){
                note += tr(" (还有更多)");
            }
        }
        pane->resultForm->showRows(headers, rows, elapsed, note, true, columnTypes);
        initialiseDataRows(pane, info, dbName, headers, rows);
        updateFetchButtons(pane);
        // Update whereEdit completion with column names only (no table names)
        if(pane->whereEdit){
            QList<MyEdit::CompletionItem> items;
            for(const QString &h : headers){
                items.append({h, MyEdit::ColumnType, QString(), QString()});
            }
            static const QStringList condKeywords = {
                QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("not"),
                QStringLiteral("in"), QStringLiteral("like"), QStringLiteral("between"),
                QStringLiteral("is null"), QStringLiteral("is not null"),
                QStringLiteral("exists"), QStringLiteral("asc"), QStringLiteral("desc")
            };
            for(const QString &kw : condKeywords){
                items.append({kw, MyEdit::KeywordType, QString(), QString()});
            }
            pane->whereEdit->setCompletionItems(items);
        }
    }
    db.close();
    QSqlDatabase::removeDatabase(connId);
    updateInspectSortOptions(pane);
    applyInspectSort(pane, Qt::AscendingOrder);
}

void QueryForm::refreshInspectStructure(InspectPane *pane)
{
    if(!pane){
        return;
    }
    if(pane->connName.isEmpty() || pane->tableName.isEmpty()){
        QMessageBox::information(this, tr("提示"), tr("请选择左侧的表。"));
        return;
    }
    ConnectionInfo info = ConnectionManager::instance()->connection(pane->connName);
    if(info.name.isEmpty()){
        QMessageBox::warning(this, tr("提示"), tr("连接 %1 不存在。").arg(pane->connName));
        return;
    }
    QString dbName = pane->dbName;
    if(dbName.isEmpty()){
        dbName = info.defaultDb;
    }
    if(dbName.isEmpty()){
        QMessageBox::warning(this, tr("提示"), tr("连接 %1 未配置默认数据库。").arg(info.name));
        return;
    }
    if(pane->structureDatabaseEdit){
        pane->structureDatabaseEdit->setText(dbName);
    }

    const QString connId = QStringLiteral("inspect_%1_%2_%3")
            .arg(info.name,
                 pane->tableName,
                 QString::number(QDateTime::currentMSecsSinceEpoch()));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    db.setDatabaseName(dbName);

    if(!db.open()){
        QMessageBox::warning(this, tr("提示"), tr("连接失败: %1").arg(db.lastError().text()));
        QSqlDatabase::removeDatabase(connId);
        return;
    }

    const QString sql = QStringLiteral("SHOW FULL COLUMNS FROM %1 FROM %2;")
            .arg(escapeIdentifier(pane->tableName), escapeIdentifier(dbName));

    QElapsedTimer timer;
    timer.start();
    QSqlQuery query(db);
    if(!query.exec(sql)){
        QMessageBox::warning(this, tr("提示"), tr("查询失败: %1").arg(query.lastError().text()));
        db.close();
        QSqlDatabase::removeDatabase(connId);
        return;
    }
    const qint64 elapsed = timer.elapsed();
    const auto columns = parseTableStructure(query);
    Q_UNUSED(elapsed);
    pane->structureOriginalColumns = columns;
    pane->structureWorkingColumns = columns;
    pane->structurePendingSql.clear();
    pane->structureDirty = false;
    rebuildStructureTable(pane);
    updateStructureDirtyState(pane);

    QString tableNameLiteral = pane->tableName;
    tableNameLiteral.replace(QLatin1Char('\''), QStringLiteral("''"));
    const QString statusSql = QStringLiteral("SHOW TABLE STATUS FROM %1 LIKE '%2'")
            .arg(escapeIdentifier(dbName), tableNameLiteral);
    QSqlQuery statusQuery(db);
    QMap<QString, QString> statusData;
    if(statusQuery.exec(statusSql) && statusQuery.next()){
        const QString comment = statusQuery.value(QStringLiteral("Comment")).toString();
        if(pane->structureCommentEdit){
            pane->structureCommentEdit->setText(comment);
        }
        const QStringList fields = {
            QStringLiteral("Engine"),
            QStringLiteral("Row_format"),
            QStringLiteral("Create_time"),
            QStringLiteral("Update_time"),
            QStringLiteral("Collation"),
            QStringLiteral("Auto_increment"),
            QStringLiteral("Avg_row_length"),
            QStringLiteral("Rows"),
            QStringLiteral("Max_data_length"),
            QStringLiteral("Data_length"),
            QStringLiteral("Data_free"),
            QStringLiteral("Index_length"),
            QStringLiteral("Create_options")
        };
        for(const auto &field : fields){
            statusData.insert(field, statusQuery.value(field).toString());
        }
    }
    fillOptionsTab(pane, statusData);

    showIndexInfo(pane, db, dbName);
    showForeignKeys(pane, db, dbName);
    showTriggers(pane, db, dbName);
    showDdlInfo(pane, db);
    updateSqlPreviewPane(pane, dbName);
    updateStructureButtons(pane);

    db.close();
    QSqlDatabase::removeDatabase(connId);
}

void QueryForm::changeInspectView(InspectPane *pane, TableAction action)
{
    if(!pane){
        return;
    }
    if(pane->viewDataButton){
        pane->viewDataButton->setChecked(action == ViewData);
    }
    if(pane->viewStructureButton){
        pane->viewStructureButton->setChecked(action == ViewStructure);
    }
    if(pane->currentAction == action){
        return;
    }
    pane->currentAction = action;
    updateInspectView(pane);
    refreshInspectData(pane);
}

void QueryForm::updateInspectView(InspectPane *pane)
{
    if(!pane || !pane->viewStack){
        return;
    }
    QWidget *target = pane->dataPage;
    if(pane->currentAction == ViewStructure && pane->structurePage){
        target = pane->structurePage;
    }
    if(target){
        pane->viewStack->setCurrentWidget(target);
    }
}

int QueryForm::selectedStructureRow(const InspectPane *pane) const
{
    if(!pane || !pane->structureTable){
        return -1;
    }
    return pane->structureTable->currentRow();
}

void QueryForm::handleStructureAdd(InspectPane *pane)
{
    if(!pane){
        return;
    }
    ResultForm::ColumnInfo info;
    info.name = QStringLiteral("new_column_%1").arg(pane->structureWorkingColumns.size() + 1);
    info.type = QStringLiteral("varchar(255)");
    pane->structureWorkingColumns.append(info);
    pane->structureDirty = true;
    rebuildStructureTable(pane);
    const int newRow = pane->structureWorkingColumns.size() - 1;
    if(pane->structureTable){
        pane->structureTable->setCurrentCell(newRow, 0);
        pane->structureTable->editItem(pane->structureTable->item(newRow, 0));
    }
    updateStructureDirtyState(pane);
}

void QueryForm::handleStructureRemove(InspectPane *pane)
{
    if(!pane){
        return;
    }
    const int row = selectedStructureRow(pane);
    if(row < 0 || row >= pane->structureWorkingColumns.size()){
        QMessageBox::information(this, tr("删除列"), tr("请选择要删除的列。"));
        return;
    }
    pane->structureWorkingColumns.removeAt(row);
    pane->structureDirty = true;
    rebuildStructureTable(pane);
    if(pane->structureTable){
        const int nextRow = qMin(row, pane->structureWorkingColumns.size() - 1);
        if(nextRow >= 0){
            pane->structureTable->setCurrentCell(nextRow, 0);
        }
    }
    updateStructureDirtyState(pane);
}

void QueryForm::handleStructureMove(InspectPane *pane, bool moveUp)
{
    if(!pane || pane->structureWorkingColumns.isEmpty()){
        return;
    }
    const int row = selectedStructureRow(pane);
    if(row < 0 || row >= pane->structureWorkingColumns.size()){
        QMessageBox::information(this, tr("调整列顺序"), tr("请选择要调整的列。"));
        return;
    }
    const int newIndex = moveUp ? row - 1 : row + 1;
    if(newIndex < 0 || newIndex >= pane->structureWorkingColumns.size()){
        return;
    }
    pane->structureWorkingColumns.move(row, newIndex);
    pane->structureDirty = true;
    rebuildStructureTable(pane);
    if(pane->structureTable){
        pane->structureTable->setCurrentCell(newIndex, 0);
    }
    updateStructureDirtyState(pane);
}

void QueryForm::handleIndexAdd(InspectPane *pane)
{
    if(!pane || !pane->indexTable){
        return;
    }
    const int newRow = pane->indexTable->rowCount();
    pane->indexTable->insertRow(newRow);
    pane->indexTable->setItem(newRow, 0, new QTableWidgetItem(QStringLiteral("idx_new_%1").arg(newRow + 1)));
    pane->indexTable->setItem(newRow, 1, new QTableWidgetItem());
    auto *colBtn = new QToolButton(pane->indexTable);
    colBtn->setText(QStringLiteral("..."));
    connect(colBtn, &QToolButton::clicked, this, [this, pane, newRow]() {
        showIndexColumnDialog(pane, newRow);
    });
    pane->indexTable->setCellWidget(newRow, 2, colBtn);
    auto *typeCombo = new QComboBox(pane->indexTable);
    typeCombo->addItems({tr("普通索引"), tr("唯一索引")});
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pane]() {
        updateIndexDirtyState(pane);
    });
    pane->indexTable->setCellWidget(newRow, 3, typeCombo);
    auto *methodCombo = new QComboBox(pane->indexTable);
    methodCombo->addItems({QStringLiteral("BTREE"), QStringLiteral("HASH")});
    connect(methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pane]() {
        updateIndexDirtyState(pane);
    });
    pane->indexTable->setCellWidget(newRow, 4, methodCombo);
    pane->indexTable->setItem(newRow, 5, new QTableWidgetItem());
    pane->indexTable->setCurrentCell(newRow, 0);
    pane->indexTable->editItem(pane->indexTable->item(newRow, 0));
    updateIndexDirtyState(pane);
}

void QueryForm::handleIndexDelete(InspectPane *pane)
{
    if(!pane || !pane->indexTable){
        return;
    }
    const int row = pane->indexTable->currentRow();
    if(row < 0){
        QMessageBox::information(this, tr("删除索引"), tr("请选择要删除的索引。"));
        return;
    }
    pane->indexTable->removeRow(row);
    updateIndexDirtyState(pane);
}

void QueryForm::showIndexColumnDialog(InspectPane *pane, int row)
{
    if(!pane || !pane->indexTable){
        return;
    }
    QStringList columnNames;
    for(const auto &col : pane->structureWorkingColumns){
        columnNames << col.name;
    }
    if(columnNames.isEmpty()){
        QMessageBox::warning(this, tr("选择列"), tr("无法获取表的列信息。"));
        return;
    }
    auto *colItem = pane->indexTable->item(row, 1);
    QStringList currentCols;
    if(colItem){
        currentCols = colItem->text().split(QStringLiteral(","), Qt::SkipEmptyParts);
        for(auto &c : currentCols){
            c = c.trimmed();
        }
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("选择列"));
    dlg.setMinimumSize(300, 400);
    auto *layout = new QVBoxLayout(&dlg);
    auto *scrollArea = new QScrollArea(&dlg);
    scrollArea->setWidgetResizable(true);
    auto *listWidget = new QWidget(scrollArea);
    auto *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(4, 4, 4, 4);
    listLayout->setSpacing(2);
    QList<QCheckBox*> checkBoxes;
    QList<QComboBox*> orderCombos;
    for(const QString &colName : columnNames){
        auto *rowWidget = new QWidget(listWidget);
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *cb = new QCheckBox(colName, rowWidget);
        if(currentCols.contains(colName)){
            cb->setChecked(true);
        }
        auto *orderCombo = new QComboBox(rowWidget);
        orderCombo->addItems({QString(), QStringLiteral("ASC"), QStringLiteral("DESC")});
        orderCombo->setFixedWidth(60);
        rowLayout->addWidget(cb, 1);
        rowLayout->addWidget(orderCombo);
        listLayout->addWidget(rowWidget);
        checkBoxes << cb;
        orderCombos << orderCombo;
    }
    listLayout->addStretch();
    scrollArea->setWidget(listWidget);
    layout->addWidget(scrollArea, 1);
    auto *filterEdit = new QLineEdit(&dlg);
    filterEdit->setPlaceholderText(tr("Regex Filter"));
    layout->addWidget(filterEdit);
    connect(filterEdit, &QLineEdit::textChanged, &dlg, [&checkBoxes, &orderCombos](const QString &text){
        QRegularExpression re(text, QRegularExpression::CaseInsensitiveOption);
        for(int i = 0; i < checkBoxes.size(); ++i){
            bool visible = text.isEmpty() || re.match(checkBoxes[i]->text()).hasMatch();
            checkBoxes[i]->parentWidget()->setVisible(visible);
        }
    });
    auto *btnLayout = new QHBoxLayout;
    auto *selectAllBtn = new QPushButton(tr("全选"), &dlg);
    auto *deselectAllBtn = new QPushButton(tr("Deselect All"), &dlg);
    auto *okBtn = new QPushButton(tr("确定"), &dlg);
    btnLayout->addWidget(selectAllBtn);
    btnLayout->addWidget(deselectAllBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    layout->addLayout(btnLayout);
    connect(selectAllBtn, &QPushButton::clicked, &dlg, [&checkBoxes](){
        for(auto *cb : checkBoxes){
            if(cb->parentWidget()->isVisible()){
                cb->setChecked(true);
            }
        }
    });
    connect(deselectAllBtn, &QPushButton::clicked, &dlg, [&checkBoxes](){
        for(auto *cb : checkBoxes){
            if(cb->parentWidget()->isVisible()){
                cb->setChecked(false);
            }
        }
    });
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    if(dlg.exec() != QDialog::Accepted){
        return;
    }
    QStringList selectedCols;
    for(int i = 0; i < checkBoxes.size(); ++i){
        if(checkBoxes[i]->isChecked()){
            QString col = checkBoxes[i]->text();
            QString order = orderCombos[i]->currentText();
            if(!order.isEmpty()){
                col += QStringLiteral(" ") + order;
            }
            selectedCols << col;
        }
    }
    if(!colItem){
        colItem = new QTableWidgetItem();
        pane->indexTable->setItem(row, 1, colItem);
    }
    colItem->setText(selectedCols.join(QStringLiteral(",")));
}

bool QueryForm::ensureIndexChangesHandled(InspectPane *pane)
{
    if(!pane || !pane->indexDirty){
        return true;
    }
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(QStringLiteral("OpenDBKit"));
    msgBox.setText(tr("索引已修改.\n是否需要保存?"));
    auto *saveBtn = msgBox.addButton(tr("保存"), QMessageBox::AcceptRole);
    auto *discardBtn = msgBox.addButton(tr("不保存"), QMessageBox::DestructiveRole);
    msgBox.addButton(tr("取消"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(saveBtn);
    msgBox.exec();
    if(msgBox.clickedButton() == saveBtn){
        saveIndexChanges(pane);
        return true;
    }
    if(msgBox.clickedButton() == discardBtn){
        return true;
    }
    return false;
}

void QueryForm::saveIndexChanges(InspectPane *pane)
{
    if(!pane || pane->indexPendingSql.isEmpty()){
        return;
    }
    const ConnectionInfo info = ConnectionManager::instance()->connection(pane->connName);
    if(info.name.isEmpty()){
        QMessageBox::warning(this, tr("保存索引"), tr("连接不存在。"));
        return;
    }
    const QString connId = QStringLiteral("idx_save_%1").arg(QDateTime::currentMSecsSinceEpoch());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
        db.setHostName(info.host);
        db.setPort(info.port);
        db.setUserName(info.user);
        db.setPassword(info.password);
        db.setDatabaseName(pane->dbName);
        if(!db.open()){
            QMessageBox::warning(this, tr("保存索引"), db.lastError().text());
            QSqlDatabase::removeDatabase(connId);
            return;
        }
        db.transaction();
        QStringList errors;
        for(const QString &sql : pane->indexPendingSql){
            QString cleanSql = sql;
            cleanSql.replace(QStringLiteral("\n"), QStringLiteral(" "));
            if(cleanSql.endsWith(QStringLiteral(";"))){
                cleanSql.chop(1);
            }
            QSqlQuery query(db);
            if(!query.exec(cleanSql)){
                errors << tr("执行失败: %1\n%2").arg(cleanSql, query.lastError().text());
                break;
            }
        }
        if(!errors.isEmpty()){
            db.rollback();
            QMessageBox::warning(this, tr("保存索引"), errors.join(QStringLiteral("\n\n")));
        }else{
            db.commit();
            showStatus(tr("索引已保存。"));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connId);
    populateIndexTable(pane);
}

void QueryForm::updateIndexDirtyState(InspectPane *pane)
{
    if(!pane || !pane->indexTable){
        return;
    }
    pane->indexPendingSql.clear();
    QSet<QString> processedOriginals;
    const QString tableFull = QStringLiteral("%1.%2")
            .arg(escapeIdentifier(pane->dbName), escapeIdentifier(pane->tableName));
    auto buildAddSql = [&](const QString &indexName, const QString &columns, int row) -> QString {
        auto *typeCombo = qobject_cast<QComboBox*>(pane->indexTable->cellWidget(row, 3));
        auto *methodCombo = qobject_cast<QComboBox*>(pane->indexTable->cellWidget(row, 4));
        auto *commentItem = pane->indexTable->item(row, 5);
        const bool unique = typeCombo && typeCombo->currentIndex() == 1;
        const QString method = methodCombo ? methodCombo->currentText() : QStringLiteral("BTREE");
        const QString comment = commentItem ? commentItem->text().trimmed() : QString();
        QStringList escapedCols;
        for(const QString &c : columns.split(QStringLiteral(","), Qt::SkipEmptyParts)){
            escapedCols << escapeIdentifier(c.trimmed().split(QStringLiteral(" ")).first());
        }
        QString sql = QStringLiteral("ALTER TABLE %1\nADD %2INDEX %3(%4) USING %5")
                .arg(tableFull)
                .arg(unique ? QStringLiteral("UNIQUE ") : QString())
                .arg(escapeIdentifier(indexName))
                .arg(escapedCols.join(QStringLiteral(",")))
                .arg(method);
        if(!comment.isEmpty()){
            sql += QStringLiteral(" COMMENT %1").arg(escapeSqlValue(comment));
        }
        return sql + QStringLiteral(";");
    };
    auto buildDropSql = [&](const QString &indexName) -> QString {
        if(indexName.compare(QStringLiteral("PRIMARY"), Qt::CaseInsensitive) == 0){
            return QStringLiteral("ALTER TABLE %1\nDROP PRIMARY KEY;").arg(tableFull);
        }
        return QStringLiteral("ALTER TABLE %1\nDROP INDEX %2;").arg(tableFull, escapeIdentifier(indexName));
    };
    for(int row = 0; row < pane->indexTable->rowCount(); ++row){
        auto *nameItem = pane->indexTable->item(row, 0);
        auto *colsItem = pane->indexTable->item(row, 1);
        const QString name = nameItem ? nameItem->text().trimmed() : QString();
        const QString originalName = nameItem ? nameItem->data(Qt::UserRole).toString() : QString();
        const QString columns = colsItem ? colsItem->text().trimmed() : QString();
        if(name.isEmpty()){
            continue;
        }
        if(!originalName.isEmpty()){
            processedOriginals.insert(originalName);
        }
        const bool isNew = originalName.isEmpty() || !pane->indexOriginalData.contains(originalName);
        if(isNew){
            if(!columns.isEmpty()){
                pane->indexPendingSql << buildAddSql(name, columns, row);
            }
            continue;
        }
        auto *typeCombo = qobject_cast<QComboBox*>(pane->indexTable->cellWidget(row, 3));
        auto *methodCombo = qobject_cast<QComboBox*>(pane->indexTable->cellWidget(row, 4));
        auto *commentItem = pane->indexTable->item(row, 5);
        const QString type = typeCombo ? typeCombo->currentText() : QString();
        const QString method = methodCombo ? methodCombo->currentText() : QString();
        const QString comment = commentItem ? commentItem->text().trimmed() : QString();
        const QStringList &orig = pane->indexOriginalData.value(originalName);
        const bool nameChanged = name != originalName;
        const bool dataChanged = orig.size() >= 4 && (columns != orig[0] || type != orig[1] || method != orig[2] || comment != orig[3]);
        if(nameChanged || dataChanged){
            if(!columns.isEmpty()){
                const QString tempName = QStringLiteral("_tmp_idx_%1").arg(row);
                auto buildTempAddSql = [&]() -> QString {
                    const bool unique = typeCombo && typeCombo->currentIndex() == 1;
                    const QString m = methodCombo ? methodCombo->currentText() : QStringLiteral("BTREE");
                    const QString c = commentItem ? commentItem->text().trimmed() : QString();
                    QStringList escapedCols;
                    for(const QString &col : columns.split(QStringLiteral(","), Qt::SkipEmptyParts)){
                        escapedCols << escapeIdentifier(col.trimmed().split(QStringLiteral(" ")).first());
                    }
                    QString sql = QStringLiteral("ALTER TABLE %1\nADD %2INDEX %3(%4) USING %5")
                            .arg(tableFull)
                            .arg(unique ? QStringLiteral("UNIQUE ") : QString())
                            .arg(escapeIdentifier(tempName))
                            .arg(escapedCols.join(QStringLiteral(",")))
                            .arg(m);
                    if(!c.isEmpty()){
                        sql += QStringLiteral(" COMMENT %1").arg(escapeSqlValue(c));
                    }
                    return sql + QStringLiteral(";");
                };
                pane->indexPendingSql << buildTempAddSql();
                pane->indexPendingSql << buildDropSql(originalName);
                pane->indexPendingSql << QStringLiteral("ALTER TABLE %1\nRENAME INDEX %2 TO %3;")
                        .arg(tableFull, escapeIdentifier(tempName), escapeIdentifier(name));
            }else{
                pane->indexPendingSql << buildDropSql(originalName);
            }
        }
    }
    for(const QString &origName : pane->indexOriginalData.keys()){
        if(!processedOriginals.contains(origName)){
            pane->indexPendingSql << buildDropSql(origName);
        }
    }
    pane->indexDirty = !pane->indexPendingSql.isEmpty();
    if(pane->indexSaveButton){
        pane->indexSaveButton->setEnabled(pane->indexDirty);
    }
    updateSqlPreviewPane(pane, pane->dbName);
}

void QueryForm::populateIndexTable(InspectPane *pane)
{
    if(!pane || !pane->indexTable){
        return;
    }
    const ConnectionInfo info = ConnectionManager::instance()->connection(pane->connName);
    if(info.name.isEmpty()){
        return;
    }
    const QString connId = QStringLiteral("idx_refresh_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    db.setDatabaseName(pane->dbName);
    if(!db.open()){
        QSqlDatabase::removeDatabase(connId);
        return;
    }
    showIndexInfo(pane, db, pane->dbName);
    db.close();
    QSqlDatabase::removeDatabase(connId);
}

void QueryForm::rebuildStructureTable(InspectPane *pane)
{
    if(!pane || !pane->structureTable){
        return;
    }
    pane->structureBlockSignals = true;
    QTableWidget *table = pane->structureTable;
    table->setRowCount(pane->structureWorkingColumns.size());
    auto createTextItem = [](const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        return item;
    };
    auto createCheckItem = [](bool checked) {
        auto *item = new QTableWidgetItem;
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        return item;
    };
    const auto yesText = tr("是");
    const auto noText = tr("否");
    for(int row = 0; row < pane->structureWorkingColumns.size(); ++row){
        const auto &info = pane->structureWorkingColumns.at(row);
        table->setItem(row, 0, createTextItem(info.name));
        table->setItem(row, 1, createTextItem(info.type));
        auto setCheck = [&](int col, bool value) {
            auto *item = createCheckItem(value);
            item->setText(value ? yesText : noText);
            table->setItem(row, col, item);
        };
        setCheck(2, info.unsignedFlag);
        setCheck(3, info.zeroFill);
        setCheck(4, info.notNull);
        setCheck(5, info.key);
        setCheck(6, info.autoIncrement);
        table->setItem(row, 7, createTextItem(info.defaultExpression));
        setCheck(8, info.generated);
        table->setItem(row, 9, createTextItem(info.comment));
    }
    pane->structureBlockSignals = false;
    applyStructureFilter(pane);
    updateStructureButtons(pane);
    updateStructureDirtyState(pane);
}

void QueryForm::applyStructureFilter(InspectPane *pane)
{
    if(!pane || !pane->structureTable){
        return;
    }
    const QString filter = pane->structureFilterEdit ? pane->structureFilterEdit->text().trimmed() : QString();
    QRegularExpression regex;
    const bool useFilter = !filter.isEmpty();
    if(useFilter){
        regex = QRegularExpression(filter, QRegularExpression::CaseInsensitiveOption);
    }
    for(int row = 0; row < pane->structureTable->rowCount(); ++row){
        bool match = !useFilter;
        if(useFilter){
            for(int col = 0; col < pane->structureTable->columnCount(); ++col){
                QTableWidgetItem *item = pane->structureTable->item(row, col);
                if(item && regex.match(item->text()).hasMatch()){
                    match = true;
                    break;
                }
            }
        }
        pane->structureTable->setRowHidden(row, !match);
    }
}

static int findColumnIndexByName(const QList<ResultForm::ColumnInfo> &columns, const QString &name)
{
    for(int i = 0; i < columns.size(); ++i){
        if(columns.at(i).name.compare(name, Qt::CaseInsensitive) == 0){
            return i;
        }
    }
    return -1;
}

static bool columnsEqual(const ResultForm::ColumnInfo &a, const ResultForm::ColumnInfo &b)
{
    return a.name == b.name
            && a.type == b.type
            && a.unsignedFlag == b.unsignedFlag
            && a.zeroFill == b.zeroFill
            && a.notNull == b.notNull
            && a.key == b.key
            && a.autoIncrement == b.autoIncrement
            && a.generated == b.generated
            && a.defaultExpression == b.defaultExpression
            && a.comment == b.comment;
}

QStringList QueryForm::generateStructureSqlStatements(InspectPane *pane) const
{
    if(!pane){
        return {};
    }
    QString dbName = pane->dbName;
    if(dbName.isEmpty()){
        dbName = inspectDb;
    }
    if(dbName.isEmpty()){
        return {};
    }
    const QString tableQualified = qualifiedName(dbName, pane->tableName);
    QStringList statements;
    QSet<QString> visitedOriginalNames;
    for(int i = 0; i < pane->structureWorkingColumns.size(); ++i){
        const auto &col = pane->structureWorkingColumns.at(i);
        // Use originalName to find the original column
        const QString origName = col.originalName.isEmpty() ? col.name : col.originalName;
        const int originalIndex = findColumnIndexByName(pane->structureOriginalColumns, origName);
        QString afterClause;
        if(i == 0){
            afterClause = QStringLiteral(" FIRST");
        }else{
            afterClause = QStringLiteral(" AFTER %1").arg(escapeIdentifier(pane->structureWorkingColumns.at(i - 1).name));
        }
        if(originalIndex < 0){
            // New column
            statements << QStringLiteral("ALTER TABLE %1 ADD COLUMN %2%3;")
                          .arg(tableQualified,
                               buildColumnDefinition(col),
                               afterClause);
        }else{
            visitedOriginalNames.insert(origName.toLower());
            const auto &original = pane->structureOriginalColumns.at(originalIndex);
            const bool nameChanged = col.name != original.name;
            const bool orderChanged = originalIndex != i;
            if(nameChanged || orderChanged || !columnsEqual(original, col)){
                if(nameChanged){
                    // Use CHANGE COLUMN for rename
                    statements << QStringLiteral("ALTER TABLE %1 CHANGE COLUMN %2 %3%4;")
                                  .arg(tableQualified,
                                       escapeIdentifier(original.name),
                                       buildColumnDefinition(col),
                                       afterClause);
                }else{
                    statements << QStringLiteral("ALTER TABLE %1 MODIFY COLUMN %2%3;")
                                  .arg(tableQualified,
                                       buildColumnDefinition(col),
                                       afterClause);
                }
            }
        }
    }
    for(const auto &original : pane->structureOriginalColumns){
        if(!visitedOriginalNames.contains(original.name.toLower())){
            statements << QStringLiteral("ALTER TABLE %1 DROP COLUMN %2;")
                          .arg(tableQualified,
                               escapeIdentifier(original.name));
        }
    }
    return statements;
}

void QueryForm::updateStructureDirtyState(InspectPane *pane)
{
    if(!pane){
        return;
    }
    pane->structurePendingSql = generateStructureSqlStatements(pane);
    pane->structureDirty = !pane->structurePendingSql.isEmpty();
    if(pane->structureSaveButton){
        pane->structureSaveButton->setEnabled(pane->structureDirty);
    }
    updateStructureButtons(pane);
    const QString dbName = pane->dbName.isEmpty() ? inspectDb : pane->dbName;
    updateSqlPreviewPane(pane, dbName);
}

bool QueryForm::ensureStructureChangesHandled(InspectPane *pane, bool allowCancel)
{
    if(!pane || !pane->structureDirty){
        return true;
    }
    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowTitle(QStringLiteral("OpenDBKit"));
    msgBox.setText(tr("表结构已修改.\n是否需要保存?"));
    auto *saveBtn = msgBox.addButton(tr("保存"), QMessageBox::AcceptRole);
    auto *discardBtn = msgBox.addButton(tr("不保存"), QMessageBox::DestructiveRole);
    if(allowCancel){
        msgBox.addButton(tr("取消"), QMessageBox::RejectRole);
    }
    msgBox.setDefaultButton(saveBtn);
    msgBox.exec();
    if(msgBox.clickedButton() == saveBtn){
        return saveStructureChanges(pane);
    }
    if(msgBox.clickedButton() == discardBtn){
        pane->structureWorkingColumns = pane->structureOriginalColumns;
        pane->structurePendingSql.clear();
        pane->structureDirty = false;
        rebuildStructureTable(pane);
        return true;
    }
    return false;
}

bool QueryForm::saveStructureChanges(InspectPane *pane)
{
    if(!pane || pane->structurePendingSql.isEmpty()){
        return true;
    }
    QString error;
    for(const auto &sql : pane->structurePendingSql){
        if(!executeInspectSql(pane->connName, pane->dbName, sql, &error)){
            QMessageBox::warning(this, tr("保存失败"), error);
            showStatus(error, 5000);
            return false;
        }
    }
    showStatus(tr("表结构已保存。"), 4000);
    pane->structureOriginalColumns = pane->structureWorkingColumns;
    pane->structurePendingSql.clear();
    pane->structureDirty = false;
    updateStructureDirtyState(pane);
    return true;
}

void QueryForm::initialiseDataRows(InspectPane *pane,
                                   const ConnectionInfo &info,
                                   const QString &dbName,
                                   const QStringList &headers,
                                   const QList<QVariantList> &rows)
{
    if(!pane || !pane->resultForm){
        return;
    }
    pane->dataHeaders = headers;
    pane->dataHeaderIndex.clear();
    for(int i = 0; i < headers.size(); ++i){
        pane->dataHeaderIndex.insert(headers.at(i).toLower(), i);
    }
    pane->dataRowStates.clear();
    pane->dataDirty = false;
    QString actualDb = dbName.isEmpty() ? info.defaultDb : dbName;
    pane->dataPrimaryKeys = fetchPrimaryKeys(info, actualDb, pane->tableName);
    for(QString &pk : pane->dataPrimaryKeys){
        pk = pk.toLower();
    }
    auto *model = pane->resultForm->sourceModel();
    if(!model){
        updateDataButtons(pane);
        return;
    }
    for(int row = 0; row < model->rowCount(); ++row){
        QStringList rowValues;
        if(row < rows.size()){
            const QVariantList &varRow = rows.at(row);
            for(const QVariant &v : varRow){
                rowValues << (v.isNull() ? QString() : v.toString());
            }
        }else{
            rowValues = pane->resultForm->rowValues(row);
        }
        while(rowValues.size() < headers.size()){
            rowValues << QString();
        }
        RowEditState state;
        state.rowId = generateRowId();
        state.originalValues = rowValues;
        state.currentValues = rowValues;
        pane->dataRowStates.insert(state.rowId, state);
        tagRowWithId(pane, row, state.rowId);
    }
    setupDataConnections(pane);
    updateDataButtons(pane);
    // Disable sorting in edit mode to prevent row jumping when editing
    pane->resultForm->setSortingEnabled(false);
}

void QueryForm::setupDataConnections(InspectPane *pane)
{
    if(!pane || !pane->resultForm){
        return;
    }
    if(auto *model = pane->resultForm->sourceModel()){
        connect(model, &QStandardItemModel::dataChanged, this,
                [this, pane](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
            if(pane->blockDataSignal){
                return;
            }
            for(int row = topLeft.row(); row <= bottomRight.row(); ++row){
                handleDataRowChanged(pane, row);
            }
        }, Qt::UniqueConnection);
    }
    if(auto *sel = pane->resultForm->selectionModel()){
        connect(sel, &QItemSelectionModel::selectionChanged, this,
                [this, pane](const QItemSelection &, const QItemSelection &) {
            updateDataButtons(pane);
        }, Qt::UniqueConnection);
    }
}

void QueryForm::updateDataButtons(InspectPane *pane)
{
    if(!pane){
        return;
    }
    const bool hasSelection = pane->resultForm
            && !pane->resultForm->selectedSourceRows().isEmpty();
    if(pane->duplicateRowButton){
        pane->duplicateRowButton->setEnabled(hasSelection);
    }
    if(pane->deleteRowButton){
        pane->deleteRowButton->setEnabled(hasSelection);
    }
    if(pane->saveRowsButton){
        pane->saveRowsButton->setEnabled(pane->dataDirty);
    }
    if(pane->discardRowsButton){
        pane->discardRowsButton->setEnabled(pane->dataDirty);
    }
}

void QueryForm::markDataDirty(InspectPane *pane)
{
    if(!pane){
        return;
    }
    pane->dataDirty = true;
    updateDataButtons(pane);
}

QString QueryForm::generateRowId() const
{
    return QStringLiteral("row_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
}

void QueryForm::tagRowWithId(InspectPane *pane, int row, const QString &rowId)
{
    if(!pane || !pane->resultForm || row < 0){
        return;
    }
    auto *model = pane->resultForm->sourceModel();
    if(!model){
        return;
    }
    QSignalBlocker blocker(model);
    for(int c = 0; c < model->columnCount(); ++c){
        if(QStandardItem *item = model->item(row, c)){
            item->setEditable(true);
            item->setData(rowId, Qt::UserRole + 2);
        }
    }
}

QString QueryForm::rowIdForSourceRow(InspectPane *pane, int sourceRow) const
{
    if(!pane || !pane->resultForm){
        return {};
    }
    auto *model = pane->resultForm->sourceModel();
    if(!model || model->columnCount() == 0){
        return {};
    }
    if(sourceRow < 0 || sourceRow >= model->rowCount()){
        return {};
    }
    if(QStandardItem *item = model->item(sourceRow, 0)){
        return item->data(Qt::UserRole + 2).toString();
    }
    return {};
}

QStringList QueryForm::currentRowValues(InspectPane *pane, int sourceRow) const
{
    if(!pane || !pane->resultForm){
        return {};
    }
    QStringList values = pane->resultForm->rowValues(sourceRow);
    while(values.size() < pane->dataHeaders.size()){
        values << QString();
    }
    return values;
}

void QueryForm::handleDataRowChanged(InspectPane *pane, int sourceRow)
{
    if(!pane || !pane->resultForm){
        return;
    }
    const QString rowId = rowIdForSourceRow(pane, sourceRow);
    if(rowId.isEmpty()){
        return;
    }
    auto it = pane->dataRowStates.find(rowId);
    if(it == pane->dataRowStates.end()){
        return;
    }
    RowEditState &state = it.value();
    state.currentValues = currentRowValues(pane, sourceRow);
    state.currentNullFlags = pane->resultForm->rowNullFlags(sourceRow);
    while(state.currentNullFlags.size() < pane->dataHeaders.size()){
        state.currentNullFlags << false;
    }
    if(!state.inserted){
        state.updated = (state.currentValues != state.originalValues);
    }
    markDataDirty(pane);
}

void QueryForm::appendDataRow(InspectPane *pane, const QStringList &values, const QVector<bool> &nullFlags)
{
    if(!pane || !pane->resultForm){
        return;
    }
    auto *model = pane->resultForm->sourceModel();
    if(!model){
        return;
    }
    const int columnCount = model->columnCount();
    if(columnCount == 0){
        QMessageBox::information(this, tr("编辑数据"), tr("当前结果没有列，无法编辑。"));
        return;
    }
    QList<QStandardItem*> items;
    items.reserve(columnCount);
    for(int c = 0; c < columnCount; ++c){
        auto *item = new QStandardItem(values.value(c));
        item->setEditable(true);
        // Set NullRole (Qt::UserRole + 3) for NULL values
        bool isNull = (c < nullFlags.size()) ? nullFlags.at(c) : false;
        item->setData(isNull, Qt::UserRole + 3);
        // Copy TypeRole (Qt::UserRole + 4) from first row if exists
        if(model->rowCount() > 0){
            if(auto *firstItem = model->item(0, c)){
                item->setData(firstItem->data(Qt::UserRole + 4), Qt::UserRole + 4);
            }
        }
        items << item;
    }
    pane->blockDataSignal = true;
    model->appendRow(items);
    pane->blockDataSignal = false;
    const int newRow = model->rowCount() - 1;
    const QString rowId = generateRowId();
    tagRowWithId(pane, newRow, rowId);
    RowEditState state;
    state.rowId = rowId;
    state.inserted = true;
    state.currentValues = values;
    while(state.currentValues.size() < columnCount){
        state.currentValues << QString();
    }
    state.currentNullFlags = nullFlags;
    while(state.currentNullFlags.size() < columnCount){
        state.currentNullFlags << false;
    }
    pane->dataRowStates.insert(rowId, state);
    markDataDirty(pane);
    // Scroll to and select the new row
    if(auto *tv = pane->resultForm->tableWidget()){
        QModelIndex idx = model->index(newRow, 0);
        tv->scrollTo(idx);
        tv->selectRow(newRow);
    }
}

void QueryForm::addEmptyDataRow(InspectPane *pane)
{
    appendDataRow(pane, {});
}

void QueryForm::duplicateSelectedRow(InspectPane *pane)
{
    if(!pane || !pane->resultForm){
        return;
    }
    const QList<int> rows = pane->resultForm->selectedSourceRows();
    if(rows.isEmpty()){
        QMessageBox::information(this, tr("复制行"), tr("请先选择要复制的行。"));
        return;
    }
    const int sourceRow = rows.first();
    const QStringList values = currentRowValues(pane, sourceRow);
    const QVector<bool> nullFlags = pane->resultForm->rowNullFlags(sourceRow);
    appendDataRow(pane, values, nullFlags);
}

void QueryForm::copyRowsToClipboard(InspectPane *pane)
{
    if(!pane || !pane->resultForm){
        return;
    }
    const QList<int> rows = pane->resultForm->selectedSourceRows();
    if(rows.isEmpty()){
        return;
    }
    auto *model = pane->resultForm->sourceModel();
    if(!model){
        return;
    }
    const int colCount = model->columnCount();
    QStringList lines;
    for(int row : rows){
        QStringList cells;
        for(int c = 0; c < colCount; ++c){
            QStandardItem *item = model->item(row, c);
            if(item){
                bool isNull = item->data(Qt::UserRole + 3).toBool();
                if(isNull){
                    cells << QStringLiteral("\\N");
                }else{
                    QString val = item->text();
                    val.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
                    val.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
                    cells << val;
                }
            }else{
                cells << QString();
            }
        }
        lines << cells.join(QLatin1Char('\t'));
    }
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void QueryForm::pasteRowsFromClipboard(InspectPane *pane)
{
    if(!pane || !pane->resultForm){
        return;
    }
    auto *model = pane->resultForm->sourceModel();
    if(!model){
        return;
    }
    const int colCount = model->columnCount();
    if(colCount == 0){
        return;
    }
    QString text = QApplication::clipboard()->text();
    if(text.isEmpty()){
        return;
    }
    QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QList<int> selectedRows = pane->resultForm->selectedSourceRows();
    std::sort(selectedRows.begin(), selectedRows.end());
    int lineIdx = 0;
    for(const QString &line : lines){
        QStringList cells = line.split(QLatin1Char('\t'));
        QStringList values;
        QVector<bool> nullFlags;
        for(int c = 0; c < colCount; ++c){
            QString cell = cells.value(c);
            if(cell == QStringLiteral("\\N")){
                values << QString();
                nullFlags << true;
            }else{
                cell.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
                cell.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
                values << cell;
                nullFlags << false;
            }
        }
        if(lineIdx < selectedRows.size()){
            int row = selectedRows.at(lineIdx);
            pane->blockDataSignal = true;
            for(int c = 0; c < colCount; ++c){
                QStandardItem *item = model->item(row, c);
                if(item){
                    item->setText(values.value(c));
                    item->setData(nullFlags.value(c), Qt::UserRole + 3);
                }
            }
            pane->blockDataSignal = false;
            handleDataRowChanged(pane, row);
        }else{
            appendDataRow(pane, values, nullFlags);
        }
        ++lineIdx;
    }
}

void QueryForm::deleteSelectedRows(InspectPane *pane)
{
    if(!pane || !pane->resultForm){
        return;
    }
    const QList<int> rows = pane->resultForm->selectedSourceRows();
    if(rows.isEmpty()){
        QMessageBox::information(this, tr("删除数据"), tr("请先选择要删除的行。"));
        return;
    }
    auto *model = pane->resultForm->sourceModel();
    if(!model){
        return;
    }
    QList<int> sortedRows = rows;
    std::sort(sortedRows.begin(), sortedRows.end(), std::greater<int>());
    bool requiresPrimaryKey = false;
    QVector<int> existingRows;
    QVector<int> insertedRows;
    for(int row : sortedRows){
        const QString rowId = rowIdForSourceRow(pane, row);
        if(rowId.isEmpty()){
            continue;
        }
        const auto it = pane->dataRowStates.constFind(rowId);
        if(it == pane->dataRowStates.cend()){
            continue;
        }
        if(it->inserted){
            insertedRows.append(row);
        }else{
            existingRows.append(row);
            requiresPrimaryKey = true;
        }
    }
    if(requiresPrimaryKey && pane->dataPrimaryKeys.isEmpty()){
        QMessageBox::warning(this,
                             tr("删除数据"),
                             tr("当前表缺少主键，无法删除已存在的行。"));
        return;
    }
    pane->blockDataSignal = true;
    for(int row : insertedRows){
        const QString rowId = rowIdForSourceRow(pane, row);
        pane->dataRowStates.remove(rowId);
        model->removeRow(row);
    }
    for(int row : existingRows){
        const QString rowId = rowIdForSourceRow(pane, row);
        auto it = pane->dataRowStates.find(rowId);
        if(it == pane->dataRowStates.end()){
            continue;
        }
        it.value().deleted = true;
        it.value().updated = false;
        model->removeRow(row);
    }
    pane->blockDataSignal = false;
    if(!insertedRows.isEmpty() || !existingRows.isEmpty()){
        markDataDirty(pane);
    }
}

bool QueryForm::saveDataChanges(InspectPane *pane)
{
    if(!pane){
        return false;
    }
    if(pane->dataRowStates.isEmpty()){
        QMessageBox::information(this, tr("保存数据"), tr("没有需要保存的更改。"));
        return true;
    }
    QStringList statements;
    for(auto it = pane->dataRowStates.cbegin(); it != pane->dataRowStates.cend(); ++it){
        const RowEditState &state = it.value();
        if(state.inserted){
            if(state.deleted){
                continue;
            }
            const QString insertSql = buildInsertSql(pane, state);
            if(!insertSql.isEmpty()){
                statements << insertSql;
            }
            continue;
        }
        if(state.deleted){
            QString error;
            const QString deleteSql = buildDeleteSql(pane, state, &error);
            if(deleteSql.isEmpty()){
                if(!error.isEmpty()){
                    QMessageBox::warning(this, tr("保存数据"), error);
                }
                return false;
            }
            statements << deleteSql;
            continue;
        }
        if(state.updated){
            QString error;
            const QString updateSql = buildUpdateSql(pane, state, &error);
            if(updateSql.isEmpty()){
                if(!error.isEmpty()){
                    QMessageBox::warning(this, tr("保存数据"), error);
                    return false;
                }
                continue;
            }
            statements << updateSql;
        }
    }
    if(statements.isEmpty()){
        QMessageBox::information(this, tr("保存数据"), tr("没有需要保存的更改。"));
        return true;
    }
    for(const QString &sql : statements){
        QString error;
        if(!executeInspectSql(pane->connName, pane->dbName, sql, &error)){
            QMessageBox::warning(this, tr("保存失败"), error);
            showStatus(error, 5000);
            return false;
        }
    }
    showStatus(tr("数据已保存。"), 4000);
    refreshInspectData(pane);
    return true;
}

QString QueryForm::buildRowWhereClause(InspectPane *pane,
                                       const RowEditState &state,
                                       QString *errorMessage) const
{
    if(!pane){
        return {};
    }
    if(pane->dataPrimaryKeys.isEmpty()){
        if(errorMessage){
            *errorMessage = tr("表 \"%1\" 缺少主键，无法定位行。").arg(pane->tableName);
        }
        return {};
    }
    QStringList clauses;
    for(const QString &pk : pane->dataPrimaryKeys){
        const int idx = pane->dataHeaderIndex.value(pk.toLower(), -1);
        if(idx < 0){
            if(errorMessage){
                *errorMessage = tr("无法定位主键列 %1。").arg(pk);
            }
            return {};
        }
        const QString value = state.originalValues.value(idx);
        clauses << QStringLiteral("%1 = %2")
                   .arg(escapeIdentifier(pk),
                        value.isEmpty() ? QStringLiteral("''") : escapeSqlValue(value));
    }
    return clauses.join(QStringLiteral(" AND "));
}

QString QueryForm::buildInsertSql(InspectPane *pane, const RowEditState &state) const
{
    if(!pane || pane->tableName.isEmpty()){
        return {};
    }
    QString dbName = pane->dbName;
    if(dbName.isEmpty()){
        dbName = inspectDb;
    }
    if(dbName.isEmpty()){
        return {};
    }
    QStringList columns;
    QStringList values;
    for(int i = 0; i < pane->dataHeaders.size(); ++i){
        const QString header = pane->dataHeaders.at(i);
        const QString value = state.currentValues.value(i);
        const bool isNull = (i < state.currentNullFlags.size()) && state.currentNullFlags.at(i);
        if(value.isEmpty() && !isNull){
            continue;
        }
        columns << escapeIdentifier(header);
        if(isNull){
            values << QStringLiteral("NULL");
        } else {
            values << escapeSqlValue(value);
        }
    }
    if(columns.isEmpty() || values.isEmpty()){
        return {};
    }
    return QStringLiteral("INSERT INTO %1 (%2) VALUES (%3);")
            .arg(qualifiedName(dbName, pane->tableName),
                 columns.join(QStringLiteral(", ")),
                 values.join(QStringLiteral(", ")));
}

QString QueryForm::buildUpdateSql(InspectPane *pane,
                                  const RowEditState &state,
                                  QString *errorMessage) const
{
    if(!pane || pane->tableName.isEmpty()){
        return {};
    }
    QString dbName = pane->dbName;
    if(dbName.isEmpty()){
        dbName = inspectDb;
    }
    if(dbName.isEmpty()){
        return {};
    }
    QStringList assignments;
    for(int i = 0; i < pane->dataHeaders.size(); ++i){
        const QString newValue = state.currentValues.value(i);
        const QString oldValue = state.originalValues.value(i);
        if(newValue == oldValue){
            continue;
        }
        const bool isNull = (i < state.currentNullFlags.size()) && state.currentNullFlags.at(i);
        QString sqlValue;
        if(isNull){
            sqlValue = QStringLiteral("NULL");
        } else if(newValue.isEmpty()){
            sqlValue = QStringLiteral("''");
        } else {
            sqlValue = escapeSqlValue(newValue);
        }
        assignments << QStringLiteral("%1 = %2")
                       .arg(escapeIdentifier(pane->dataHeaders.at(i)), sqlValue);
    }
    if(assignments.isEmpty()){
        return {};
    }
    QString error;
    const QString whereClause = buildRowWhereClause(pane, state, &error);
    if(whereClause.isEmpty()){
        if(errorMessage){
            *errorMessage = error;
        }
        return {};
    }
    return QStringLiteral("UPDATE %1 SET %2 WHERE %3 LIMIT 1;")
            .arg(qualifiedName(dbName, pane->tableName),
                 assignments.join(QStringLiteral(", ")),
                 whereClause);
}

QString QueryForm::buildDeleteSql(InspectPane *pane,
                                  const RowEditState &state,
                                  QString *errorMessage) const
{
    if(!pane || pane->tableName.isEmpty()){
        return {};
    }
    QString dbName = pane->dbName;
    if(dbName.isEmpty()){
        dbName = inspectDb;
    }
    if(dbName.isEmpty()){
        return {};
    }
    QString error;
    const QString whereClause = buildRowWhereClause(pane, state, &error);
    if(whereClause.isEmpty()){
        if(errorMessage){
            *errorMessage = error;
        }
        return {};
    }
    return QStringLiteral("DELETE FROM %1 WHERE %2 LIMIT 1;")
            .arg(qualifiedName(dbName, pane->tableName), whereClause);
}

QStringList QueryForm::fetchPrimaryKeys(const ConnectionInfo &info,
                                        const QString &dbName,
                                        const QString &tableName) const
{
    QStringList keys;
    if(info.name.isEmpty() || tableName.isEmpty()){
        return keys;
    }
    const QString targetDb = dbName.isEmpty() ? info.defaultDb : dbName;
    if(targetDb.isEmpty()){
        return keys;
    }
    const QString connId = QStringLiteral("pk_%1_%2")
            .arg(info.name,
                 QString::number(QDateTime::currentMSecsSinceEpoch()));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    db.setDatabaseName(targetDb);
    if(!info.charset.isEmpty()){
        db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_CHARSET=%1").arg(info.charset));
    }
    if(!db.open()){
        QSqlDatabase::removeDatabase(connId);
        return keys;
    }
    const QString sql = QStringLiteral("SHOW KEYS FROM %1 WHERE Key_name = 'PRIMARY';")
            .arg(escapeIdentifier(tableName));
    QSqlQuery query(db);
    if(query.exec(sql)){
        while(query.next()){
            keys << query.value(QStringLiteral("Column_name")).toString();
        }
    }
    db.close();
    QSqlDatabase::removeDatabase(connId);
    return keys;
}

void QueryForm::showDataContextMenu(InspectPane *pane, const QPoint &pos)
{
    if(!pane || !pane->resultForm){
        return;
    }
    QTableView *table = pane->resultForm->tableWidget();
    if(!table){
        return;
    }
    const QPoint globalPos = table->viewport()->mapToGlobal(pos);
    QMenu menu(table);
    QAction *addAction = menu.addAction(tr("新增行"));
    QAction *duplicateAction = menu.addAction(tr("复制行"));
    QAction *deleteAction = menu.addAction(tr("删除行"));
    menu.addSeparator();
    QAction *saveAction = menu.addAction(tr("保存更改"));
    QAction *discardAction = menu.addAction(tr("撤销更改"));
    menu.addSeparator();
    QAction *refreshAction = menu.addAction(tr("刷新数据"));

    const bool hasSelection = !pane->resultForm->selectedSourceRows().isEmpty();
    duplicateAction->setEnabled(hasSelection);
    deleteAction->setEnabled(hasSelection);
    saveAction->setEnabled(pane->dataDirty);
    discardAction->setEnabled(pane->dataDirty);

    QAction *selected = menu.exec(globalPos);
    if(!selected){
        return;
    }
    if(selected == addAction){
        addEmptyDataRow(pane);
        return;
    }
    if(selected == duplicateAction){
        duplicateSelectedRow(pane);
        return;
    }
    if(selected == deleteAction){
        deleteSelectedRows(pane);
        return;
    }
    if(selected == saveAction){
        saveDataChanges(pane);
        return;
    }
    if(selected == discardAction){
        refreshInspectData(pane);
        return;
    }
    if(selected == refreshAction){
        refreshInspectData(pane);
    }
}

void QueryForm::updateStructureButtons(InspectPane *pane)
{
    if(!pane){
        return;
    }
    const int row = selectedStructureRow(pane);
    const bool hasSelection = row >= 0;
    const int total = pane->structureWorkingColumns.size();
    if(pane->structureAddButton){
        pane->structureAddButton->setEnabled(true);
    }
    if(pane->structureRemoveButton){
        pane->structureRemoveButton->setEnabled(hasSelection);
    }
    if(pane->structureUpButton){
        pane->structureUpButton->setEnabled(hasSelection && row > 0);
    }
    if(pane->structureDownButton){
        pane->structureDownButton->setEnabled(hasSelection && row >= 0 && row < total - 1);
    }
    if(pane->structureSaveButton){
        pane->structureSaveButton->setEnabled(!pane->structurePendingSql.isEmpty());
    }
    if(pane->structureReloadButton){
        pane->structureReloadButton->setEnabled(true);
    }
    if(pane->structureCloseButton){
        pane->structureCloseButton->setEnabled(true);
    }
}

bool QueryForm::executeInspectSql(const QString &connName, const QString &dbName,
                                  const QString &sql, QString *errorMessage)
{
    ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    if(info.name.isEmpty()){
        if(errorMessage){
            *errorMessage = tr("连接 %1 不存在。").arg(connName);
        }
        return false;
    }
    QString targetDb = dbName.isEmpty() ? info.defaultDb : dbName;
    if(targetDb.isEmpty()){
        if(errorMessage){
            *errorMessage = tr("连接 %1 未配置默认数据库。").arg(info.name);
        }
        return false;
    }
    const QString connId = QStringLiteral("alter_%1_%2")
            .arg(info.name,
                 QString::number(QDateTime::currentMSecsSinceEpoch()));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    db.setDatabaseName(targetDb);
    if(!db.open()){
        if(errorMessage){
            *errorMessage = db.lastError().text();
        }
        QSqlDatabase::removeDatabase(connId);
        return false;
    }
    QSqlQuery query(db);
    const bool ok = query.exec(sql);
    if(!ok && errorMessage){
        *errorMessage = query.lastError().text();
    }
    db.close();
    QSqlDatabase::removeDatabase(connId);
    return ok;
}

QString QueryForm::escapeSqlValue(const QString &value) const
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("'"), QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString QueryForm::buildColumnDefinition(const ResultForm::ColumnInfo &info) const
{
    QString definition = QStringLiteral("%1 %2")
            .arg(escapeIdentifier(info.name), info.type);
    if(info.unsignedFlag){
        definition += QStringLiteral(" UNSIGNED");
    }
    if(info.zeroFill){
        definition += QStringLiteral(" ZEROFILL");
    }
    definition += info.notNull ? QStringLiteral(" NOT NULL") : QStringLiteral(" NULL");

    auto isFunctionLike = [](const QString &expr) -> bool {
        QRegularExpression rx(QStringLiteral("^[A-Za-z_]+\\(.*\\)$"));
        return rx.match(expr).hasMatch();
    };
    auto isNumeric = [](const QString &expr) -> bool {
        QRegularExpression rx(QStringLiteral("^[\\-\\+]?[0-9]+(\\.[0-9]+)?$"));
        return rx.match(expr).hasMatch();
    };

    if(!info.defaultExpression.isEmpty()
            && info.defaultExpression.compare(QStringLiteral("NULL"), Qt::CaseInsensitive) != 0){
        const QString expr = info.defaultExpression.trimmed();
        if(isFunctionLike(expr) || isNumeric(expr)
                || expr.compare(QStringLiteral("CURRENT_TIMESTAMP"), Qt::CaseInsensitive) == 0){
            definition += QStringLiteral(" DEFAULT %1").arg(expr);
        }else{
            definition += QStringLiteral(" DEFAULT %1").arg(escapeSqlValue(expr));
        }
    }else if(!info.notNull){
        definition += QStringLiteral(" DEFAULT NULL");
    }
    if(info.autoIncrement){
        definition += QStringLiteral(" AUTO_INCREMENT");
    }
    if(!info.comment.trimmed().isEmpty()){
        definition += QStringLiteral(" COMMENT %1").arg(escapeSqlValue(info.comment));
    }
    return definition.trimmed();
}

void QueryForm::fillOptionsTab(InspectPane *pane, const QMap<QString, QString> &statusData)
{
    if(!pane){
        return;
    }
    auto setText = [](QLineEdit *edit, const QString &value) {
        if(!edit){
            return;
        }
        if(value.trimmed().isEmpty()){
            edit->clear();
        }else{
            edit->setText(value);
        }
    };
    const QString collation = statusData.value(QStringLiteral("Collation"));
    QString charset;
    if(!collation.isEmpty()){
        const int idx = collation.indexOf(QLatin1Char('_'));
        charset = idx > 0 ? collation.left(idx) : collation;
    }
    setText(pane->optionEngineEdit, statusData.value(QStringLiteral("Engine")));
    setText(pane->optionRowFormatEdit, statusData.value(QStringLiteral("Row_format")));
    setText(pane->optionCharsetEdit, charset);
    setText(pane->optionCollationEdit, collation);
    setText(pane->optionAutoIncrementEdit, statusData.value(QStringLiteral("Auto_increment")));
    setText(pane->optionAvgRowLengthEdit, statusData.value(QStringLiteral("Avg_row_length")));
    setText(pane->optionTableRowsEdit, statusData.value(QStringLiteral("Rows")));
    setText(pane->optionMaxRowCountEdit, statusData.value(QStringLiteral("Max_data_length")));
    setText(pane->optionDataLengthEdit, statusData.value(QStringLiteral("Data_length")));
    setText(pane->optionDataFreeEdit, statusData.value(QStringLiteral("Data_free")));
    setText(pane->optionIndexLengthEdit, statusData.value(QStringLiteral("Index_length")));
    setText(pane->optionCreateTimeEdit, statusData.value(QStringLiteral("Create_time")));
    setText(pane->optionUpdateTimeEdit, statusData.value(QStringLiteral("Update_time")));
}

void QueryForm::showIndexInfo(InspectPane *pane, QSqlDatabase &db, const QString &dbName)
{
    if(!pane || !pane->indexTable){
        return;
    }
    pane->indexBlockSignals = true;
    pane->indexTable->setRowCount(0);
    pane->indexOriginalData.clear();
    pane->indexPendingSql.clear();
    pane->indexDirty = false;
    if(pane->indexSaveButton){
        pane->indexSaveButton->setEnabled(false);
    }
    updateSqlPreviewPane(pane, dbName);
    if(!db.isOpen()){
        pane->indexBlockSignals = false;
        return;
    }
    QSqlQuery query(db);
    const QString sql = QStringLiteral("SHOW INDEX FROM %1 FROM %2")
            .arg(escapeIdentifier(pane->tableName), escapeIdentifier(dbName));
    if(!query.exec(sql)){
        return;
    }
    struct IndexInfo {
        QString name;
        QStringList columns;
        QString type;
        QString method;
        QString comment;
    };
    QMap<QString, IndexInfo> indexMap;
    QStringList order;
    while(query.next()){
        const QString keyName = query.value(QStringLiteral("Key_name")).toString();
        if(!order.contains(keyName)){
            order << keyName;
        }
        IndexInfo &idx = indexMap[keyName];
        idx.name = keyName;
        idx.columns << query.value(QStringLiteral("Column_name")).toString();
        const bool nonUnique = query.value(QStringLiteral("Non_unique")).toInt() == 1;
        if(keyName.compare(QStringLiteral("PRIMARY"), Qt::CaseInsensitive) == 0){
            idx.type = tr("主键");
        }else if(!nonUnique){
            idx.type = tr("唯一索引");
        }else{
            idx.type = tr("普通索引");
        }
        idx.method = query.value(QStringLiteral("Index_type")).toString();
        if(idx.comment.isEmpty()){
            idx.comment = query.value(QStringLiteral("Index_comment")).toString();
        }
    }
    for(const auto &name : order){
        const IndexInfo &idx = indexMap.value(name);
        const int row = pane->indexTable->rowCount();
        pane->indexTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(idx.name);
        nameItem->setData(Qt::UserRole, idx.name);
        pane->indexTable->setItem(row, 0, nameItem);
        const QString colsText = idx.columns.join(QStringLiteral(","));
        pane->indexTable->setItem(row, 1, new QTableWidgetItem(colsText));
        auto *colBtn = new QToolButton(pane->indexTable);
        colBtn->setText(QStringLiteral("..."));
        connect(colBtn, &QToolButton::clicked, this, [this, pane, row]() {
            showIndexColumnDialog(pane, row);
        });
        pane->indexTable->setCellWidget(row, 2, colBtn);
        auto *typeCombo = new QComboBox(pane->indexTable);
        typeCombo->addItems({tr("普通索引"), tr("唯一索引")});
        const QString typeText = (idx.type == tr("唯一索引") || idx.type == tr("主键"))
                ? tr("唯一索引") : tr("普通索引");
        if(typeText == tr("唯一索引")){
            typeCombo->setCurrentIndex(1);
        }
        connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pane]() {
            updateIndexDirtyState(pane);
        });
        pane->indexTable->setCellWidget(row, 3, typeCombo);
        auto *methodCombo = new QComboBox(pane->indexTable);
        methodCombo->addItems({QStringLiteral("BTREE"), QStringLiteral("HASH")});
        if(idx.method == QStringLiteral("HASH")){
            methodCombo->setCurrentIndex(1);
        }
        connect(methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pane]() {
            updateIndexDirtyState(pane);
        });
        pane->indexTable->setCellWidget(row, 4, methodCombo);
        pane->indexTable->setItem(row, 5, new QTableWidgetItem(idx.comment));
        pane->indexOriginalData.insert(idx.name, {colsText, typeText, idx.method, idx.comment});
    }
    pane->indexBlockSignals = false;
}

void QueryForm::showForeignKeys(InspectPane *pane, QSqlDatabase &db, const QString &dbName)
{
    if(!pane || !pane->foreignResult){
        return;
    }
    if(!db.isOpen()){
        pane->foreignResult->showMessage(tr("外键加载失败: 连接未打开。"));
        return;
    }
    const QString sql = QStringLiteral(
                "SELECT CONSTRAINT_NAME, COLUMN_NAME, REFERENCED_TABLE_SCHEMA, "
                "REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME, ORDINAL_POSITION "
                "FROM information_schema.KEY_COLUMN_USAGE "
                "WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ? AND REFERENCED_TABLE_NAME IS NOT NULL "
                "ORDER BY CONSTRAINT_NAME, ORDINAL_POSITION");
    QSqlQuery query(db);
    query.prepare(sql);
    query.addBindValue(dbName);
    query.addBindValue(pane->tableName);
    if(!query.exec()){
        pane->foreignResult->showMessage(tr("加载外键失败: %1").arg(query.lastError().text()));
        return;
    }
    struct ForeignInfo {
        QString name;
        QStringList columns;
        QString refSchema;
        QString refTable;
        QStringList refColumns;
    };
    QMap<QString, ForeignInfo> foreignMap;
    QStringList order;
    while(query.next()){
        const QString constraint = query.value(0).toString();
        if(!order.contains(constraint)){
            order << constraint;
        }
        ForeignInfo &info = foreignMap[constraint];
        info.name = constraint;
        info.columns << query.value(1).toString();
        info.refSchema = query.value(2).toString();
        info.refTable = query.value(3).toString();
        info.refColumns << query.value(4).toString();
    }
    QList<QStringList> rows;
    for(const auto &name : order){
        const ForeignInfo info = foreignMap.value(name);
        rows << QStringList{
            info.name,
            info.columns.join(QStringLiteral(", ")),
            info.refSchema,
            info.refTable,
            info.refColumns.join(QStringLiteral(", "))
        };
    }
    const QStringList headers = {tr("Name"), tr("Columns"), tr("引用数据库"), tr("引用表"), tr("引用列")};
    pane->foreignResult->showRows(headers, rows);
}

void QueryForm::showTriggers(InspectPane *pane, QSqlDatabase &db, const QString &dbName)
{
    if(!pane || !pane->triggerResult){
        return;
    }
    if(!db.isOpen()){
        pane->triggerResult->showMessage(tr("触发器加载失败: 连接未打开。"));
        return;
    }
    const QString sql = QStringLiteral(
                "SELECT TRIGGER_NAME, ACTION_TIMING, EVENT_MANIPULATION, ACTION_STATEMENT "
                "FROM information_schema.TRIGGERS "
                "WHERE EVENT_OBJECT_SCHEMA = ? AND EVENT_OBJECT_TABLE = ? "
                "ORDER BY TRIGGER_NAME");
    QSqlQuery query(db);
    query.prepare(sql);
    query.addBindValue(dbName);
    query.addBindValue(pane->tableName);
    if(!query.exec()){
        pane->triggerResult->showMessage(tr("加载触发器失败: %1").arg(query.lastError().text()));
        return;
    }
    struct TriggerInfo {
        QString name;
        QString timing;
        bool insert = false;
        bool update = false;
        bool deleted = false;
        QString body;
    };
    QMap<QString, TriggerInfo> triggerMap;
    QStringList order;
    while(query.next()){
        const QString name = query.value(0).toString();
        if(!order.contains(name)){
            order << name;
        }
        TriggerInfo &info = triggerMap[name];
        info.name = name;
        info.timing = query.value(1).toString();
        const QString manipulation = query.value(2).toString().toUpper();
        if(manipulation == QStringLiteral("INSERT")){
            info.insert = true;
        }else if(manipulation == QStringLiteral("UPDATE")){
            info.update = true;
        }else if(manipulation == QStringLiteral("DELETE")){
            info.deleted = true;
        }
        info.body = query.value(3).toString();
    }
    const auto yesText = tr("是");
    const auto noText = tr("否");
    QList<QStringList> rows;
    for(const auto &name : order){
        const TriggerInfo info = triggerMap.value(name);
        rows << QStringList{
            info.name,
            info.timing,
            info.insert ? yesText : noText,
            info.update ? yesText : noText,
            info.deleted ? yesText : noText,
            info.body
        };
    }
    const QStringList headers = {tr("Name"), tr("Timing"), tr("Insert"), tr("Update"), tr("Delete"), tr("Trigger body")};
    pane->triggerResult->showRows(headers, rows);
}

void QueryForm::showDdlInfo(InspectPane *pane, QSqlDatabase &db)
{
    if(!pane || !pane->ddlEditor){
        return;
    }
    if(!db.isOpen()){
        pane->ddlEditor->setPlainText(tr("-- 无法加载 DDL: 连接未打开 --"));
        return;
    }
    QSqlQuery query(db);
    const QString sql = QStringLiteral("SHOW CREATE TABLE %1")
            .arg(escapeIdentifier(pane->tableName));
    if(!query.exec(sql) || !query.next()){
        pane->ddlEditor->setPlainText(tr("-- 无法获取 DDL: %1 --").arg(query.lastError().text()));
        return;
    }
    const int ddlIndex = query.record().indexOf(QStringLiteral("Create Table"));
    const QString ddl = ddlIndex >= 0 ? query.value(ddlIndex).toString() : query.value(1).toString();
    pane->ddlEditor->setPlainText(ddl);
}

void QueryForm::updateSqlPreviewPane(InspectPane *pane, const QString &dbName)
{
    if(!pane || !pane->sqlPreviewEditor){
        return;
    }
    const QString qualified = qualifiedName(dbName, pane->tableName);
    QStringList allSql;
    if(!pane->structurePendingSql.isEmpty()){
        allSql << pane->structurePendingSql;
    }
    if(!pane->indexPendingSql.isEmpty()){
        allSql << pane->indexPendingSql;
    }
    if(!allSql.isEmpty()){
        QString preview = tr("-- 即将应用到 %1 的变更：\n\n").arg(qualified);
        preview += allSql.join(QStringLiteral("\n\n"));
        preview += QStringLiteral("\n\n-- 保存后将立即执行。");
        pane->sqlPreviewEditor->setPlainText(preview);
        return;
    }
    pane->sqlPreviewEditor->setPlainText(tr("-- 暂无待应用的结构修改 --"));
}

void QueryForm::updateInspectSortOptions(InspectPane *pane)
{
    if(!pane || !pane->sortCombo || !pane->resultForm){
        return;
    }
    const QString previous = pane->sortCombo->currentText();
    const auto headers = pane->resultForm->headers();
    const bool hasHeaders = !headers.isEmpty();
    pane->sortCombo->blockSignals(true);
    pane->sortCombo->clear();
    for(int i = 0; i < headers.size(); ++i){
        pane->sortCombo->addItem(headers.at(i), i);
    }
    pane->sortCombo->blockSignals(false);
    if(hasHeaders){
        int idx = previous.isEmpty() ? 0 : pane->sortCombo->findText(previous);
        if(idx < 0){
            idx = 0;
        }
        pane->sortCombo->setCurrentIndex(idx);
    }
    pane->sortCombo->setEnabled(hasHeaders);
    pane->sortAscButton->setEnabled(hasHeaders);
    pane->sortDescButton->setEnabled(hasHeaders);
}

void QueryForm::applyInspectSort(InspectPane *pane, Qt::SortOrder order)
{
    if(!pane || !pane->resultForm || !pane->sortCombo){
        return;
    }
    // Disable sorting in data edit mode to prevent row jumping
    if(pane->currentAction == ViewData){
        return;
    }
    const int column = pane->sortCombo->currentData().toInt();
    if(column < 0){
        return;
    }
    pane->resultForm->sortByColumnIndex(column, order);
}

void QueryForm::fetchFirst(InspectPane *pane)
{
    if(!pane || pane->dataOffset == 0){
        return;
    }
    pane->dataOffset = 0;
    refreshInspectData(pane);
}

void QueryForm::fetchNext(InspectPane *pane)
{
    if(!pane || !pane->hasMoreData){
        return;
    }
    pane->dataOffset += pane->dataLimit;
    refreshInspectData(pane);
}

void QueryForm::fetchAll(InspectPane *pane)
{
    if(!pane){
        return;
    }
    pane->dataOffset = 0;
    pane->dataLimit = 10000; // Fetch up to 10000 rows
    refreshInspectData(pane);
    pane->dataLimit = 100; // Reset limit for next fetch
}

void QueryForm::fetchLast(InspectPane *pane)
{
    if(!pane){
        return;
    }
    // Query total count first, then jump to last page
    ConnectionInfo info = ConnectionManager::instance()->connection(pane->connName);
    if(info.name.isEmpty()){
        return;
    }
    QString dbName = pane->dbName.isEmpty() ? info.defaultDb : pane->dbName;
    const QString connId = QStringLiteral("count_%1").arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), connId);
    db.setHostName(info.host);
    db.setPort(info.port);
    db.setUserName(info.user);
    db.setPassword(info.password);
    db.setDatabaseName(dbName);
    if(!db.open()){
        QSqlDatabase::removeDatabase(connId);
        return;
    }
    QSqlQuery query(db);
    QString sql = QStringLiteral("SELECT COUNT(*) FROM %1;").arg(qualifiedName(dbName, pane->tableName));
    if(!query.exec(sql) || !query.next()){
        db.close();
        QSqlDatabase::removeDatabase(connId);
        return;
    }
    int totalRows = query.value(0).toInt();
    db.close();
    QSqlDatabase::removeDatabase(connId);
    if(totalRows <= 0){
        return;
    }
    // Calculate last page offset
    int lastPageOffset = ((totalRows - 1) / pane->dataLimit) * pane->dataLimit;
    pane->dataOffset = lastPageOffset;
    refreshInspectData(pane);
}

void QueryForm::updateFetchButtons(InspectPane *pane)
{
    if(!pane){
        return;
    }
    const bool canGoBack = (pane->dataOffset > 0);
    const bool canGoForward = pane->hasMoreData;
    auto setButtonIcon = [](QToolButton *btn, bool enabled, const QString &enabledIcon, const QString &disabledIcon) {
        if(!btn) return;
        btn->setEnabled(enabled);
        btn->setIcon(QIcon(enabled ? enabledIcon : disabledIcon));
    };
    setButtonIcon(pane->fetchFirstButton, canGoBack,
                  QStringLiteral(":/images/fetch-first.svg"),
                  QStringLiteral(":/images/fetch-first-disabled.svg"));
    setButtonIcon(pane->fetchNextButton, canGoForward,
                  QStringLiteral(":/images/fetch-next.svg"),
                  QStringLiteral(":/images/fetch-next-disabled.svg"));
    setButtonIcon(pane->fetchAllButton, canGoForward,
                  QStringLiteral(":/images/fetch-all.svg"),
                  QStringLiteral(":/images/fetch-all-disabled.svg"));
    setButtonIcon(pane->fetchLastButton, canGoForward,
                  QStringLiteral(":/images/fetch-last.svg"),
                  QStringLiteral(":/images/fetch-last-disabled.svg"));
    // Update page input
    if(pane->pageEdit){
        int currentPage = (pane->dataOffset / pane->dataLimit) + 1;
        pane->pageEdit->setText(QString::number(currentPage));
    }
}

void QueryForm::removeInspectPane(InspectPane *pane)
{
    if(!pane){
        return;
    }
    if(inspectStack && pane->widget){
        inspectStack->removeWidget(pane->widget);
    }
    inspectPanes.removeOne(pane);
    if(inspectTabFlow && pane->tabWidget){
        inspectTabFlow->removeWidget(pane->tabWidget);
    }
    if(pane->tabWidget){
        pane->tabWidget->deleteLater();
    }
    if(pane->widget){
        pane->widget->deleteLater();
    }
    delete pane;
    updateInspectPlaceholder();
}

void QueryForm::showInspectTabContextMenu(InspectPane *pane, const QPoint &globalPos)
{
    if(!pane){
        return;
    }
    QMenu menu(this);
    QAction *closeCurrent = menu.addAction(tr("关闭当前页签"));
    QAction *closeOthers = menu.addAction(tr("关闭其他页签"));
    QAction *closeAll = menu.addAction(tr("关闭所有页签"));
    QAction *chosen = menu.exec(globalPos);
    if(chosen == closeCurrent){
        closeInspectPane(pane);
    }else if(chosen == closeOthers){
        closeOtherInspectTabs(pane);
    }else if(chosen == closeAll){
        closeAllInspectTabs();
    }
}

void QueryForm::closeInspectPane(InspectPane *pane)
{
    if(!pane){
        return;
    }
    if(!ensureStructureChangesHandled(pane)){
        return;
    }
    const bool wasCurrent = inspectStack && pane->widget && inspectStack->currentWidget() == pane->widget;
    const int previousIndex = inspectPanes.indexOf(pane);
    removeInspectPane(pane);
    if(inspectPanes.isEmpty()){
        updateTitleFromEditor();
        return;
    }
    if(wasCurrent){
        const int idx = qBound(0, previousIndex, inspectPanes.count() - 1);
        selectInspectPane(inspectPanes.at(idx));
    }
}

void QueryForm::closeOtherInspectTabs(InspectPane *pane)
{
    if(!pane){
        return;
    }
    const auto currentPanes = inspectPanes;
    for(auto *other : currentPanes){
        if(other == pane){
            continue;
        }
        if(!ensureStructureChangesHandled(other)){
            return;
        }
        removeInspectPane(other);
    }
    selectInspectPane(pane);
}

bool QueryForm::closeAllInspectTabs()
{
    const auto currentPanes = inspectPanes;
    for(auto *pane : currentPanes){
        if(!ensureStructureChangesHandled(pane)){
            return false;
        }
        removeInspectPane(pane);
    }
    if(m_mode == QueryMode){
        updateTitleFromEditor();
    }
    return true;
}

void QueryForm::updateInspectPlaceholder()
{
    if(!inspectPlaceholder){
        return;
    }
    if(m_mode == InspectMode){
        if(inspectTabContainer){
            inspectTabContainer->setVisible(false);
        }
        const bool hasTabs = !inspectPanes.isEmpty();
        inspectPlaceholder->setVisible(!hasTabs);
        if(inspectStack){
            inspectStack->setVisible(hasTabs);
        }
        return;
    }
    const bool hasTabs = !inspectPanes.isEmpty();
    if(inspectTabContainer){
        inspectTabContainer->setVisible(hasTabs);
    }
    inspectPlaceholder->setVisible(!hasTabs);
    if(inspectStack){
        inspectStack->setVisible(hasTabs);
    }
}

QList<ResultForm::ColumnInfo> QueryForm::parseTableStructure(QSqlQuery &query) const
{
    QList<ResultForm::ColumnInfo> columns;
    const auto record = query.record();
    auto indexOfInsensitive = [&record](const QString &fieldName) -> int {
        int idx = record.indexOf(fieldName);
        if(idx >= 0){
            return idx;
        }
        for(int i = 0; i < record.count(); ++i){
            if(record.fieldName(i).compare(fieldName, Qt::CaseInsensitive) == 0){
                return i;
            }
        }
        return -1;
    };
    const int idxField = indexOfInsensitive(QStringLiteral("Field"));
    const int idxType = indexOfInsensitive(QStringLiteral("Type"));
    const int idxNull = indexOfInsensitive(QStringLiteral("Null"));
    const int idxKey = indexOfInsensitive(QStringLiteral("Key"));
    const int idxDefault = indexOfInsensitive(QStringLiteral("Default"));
    const int idxExtra = indexOfInsensitive(QStringLiteral("Extra"));
    const int idxComment = indexOfInsensitive(QStringLiteral("Comment"));

    while(query.next()){
        ResultForm::ColumnInfo info;
        info.name = idxField >= 0 ? query.value(idxField).toString() : QString();
        info.originalName = info.name;  // Track original name for CHANGE COLUMN

        QString typeString = idxType >= 0 ? query.value(idxType).toString() : QString();
        info.unsignedFlag = typeString.contains(QStringLiteral("unsigned"), Qt::CaseInsensitive);
        info.zeroFill = typeString.contains(QStringLiteral("zerofill"), Qt::CaseInsensitive);
        QRegularExpression unsignedRegex(QStringLiteral("\\s+unsigned"), QRegularExpression::CaseInsensitiveOption);
        QRegularExpression zerofillRegex(QStringLiteral("\\s+zerofill"), QRegularExpression::CaseInsensitiveOption);
        typeString.replace(unsignedRegex, QString());
        typeString.replace(zerofillRegex, QString());
        info.type = typeString.trimmed();

        const QString nullValue = idxNull >= 0 ? query.value(idxNull).toString() : QString();
        info.notNull = nullValue.compare(QStringLiteral("NO"), Qt::CaseInsensitive) == 0;

        const QString keyValue = idxKey >= 0 ? query.value(idxKey).toString() : QString();
        info.key = !keyValue.trimmed().isEmpty();

        const QVariant defaultValue = idxDefault >= 0 ? query.value(idxDefault) : QVariant();
        if(defaultValue.isNull()){
            info.defaultExpression = QStringLiteral("NULL");
        }else{
            info.defaultExpression = defaultValue.toString();
        }

        const QString extraValue = idxExtra >= 0 ? query.value(idxExtra).toString() : QString();
        info.autoIncrement = extraValue.contains(QStringLiteral("auto_increment"), Qt::CaseInsensitive);
        info.generated = extraValue.contains(QStringLiteral("generated"), Qt::CaseInsensitive);

        info.comment = idxComment >= 0 ? query.value(idxComment).toString() : QString();

        columns.append(info);
    }
    return columns;
}

void QueryForm::prepareInspectOnlyUi()
{
    if(pageStack && inspectPage){
        pageStack->setCurrentWidget(inspectPage);
    }
    if(queryPage){
        queryPage->setVisible(false);
    }
    if(inspectBackButton){
        inspectBackButton->hide();
    }
    if(inspectCloseButton){
        inspectCloseButton->hide();
    }
    if(inspectTabContainer){
        inspectTabContainer->setVisible(false);
    }
}
