#include "importdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>
#include <atomic>

namespace {

QString uniqueConnectionName(const QString &prefix)
{
    static std::atomic<int> counter {0};
    return QStringLiteral("%1_import_%2").arg(prefix).arg(counter.fetch_add(1));
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

QString quoted(const QString &identifier)
{
    QString escaped = identifier;
    escaped.replace(QLatin1Char('`'), QStringLiteral("``"));
    return QStringLiteral("`%1`").arg(escaped);
}

QString qualifiedTable(const QString &database, const QString &table)
{
    if(database.isEmpty()){
        return quoted(table);
    }
    return QStringLiteral("%1.%2").arg(quoted(database), quoted(table));
}

QChar interpretControl(const QString &text, QChar fallback)
{
    if(text.isEmpty()){
        return fallback;
    }
    if(text == QStringLiteral("\\t")){
        return QLatin1Char('\t');
    }
    if(text == QStringLiteral("\\n")){
        return QLatin1Char('\n');
    }
    if(text == QStringLiteral("\\r")){
        return QLatin1Char('\r');
    }
    return text.at(0);
}

}

ImportDialog::ImportDialog(const ConnectionInfo &info,
                           const QString &database,
                           const QString &table,
                           QWidget *parent)
    : QDialog(parent)
    , m_connection(info)
    , m_databaseName(database)
    , m_tableName(table)
{
    setWindowTitle(tr("Import Data to %1").arg(table));
    resize(800, 560);
    buildUi();
    if(!loadTargetColumns()){
        setRunning(true);
        appendLog(tr("Failed to load column metadata. Please close the dialog."));
    }
}

ImportDialog::~ImportDialog() = default;

void ImportDialog::buildUi()
{
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(createGeneralPage(), tr("General"));
    m_tabs->addTab(createMappingPage(), tr("Mapping"));
    m_tabs->addTab(createLogPage(), tr("Log"));

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ImportDialog::reject);

    m_startButton = buttonBox->addButton(tr("Start Import"), QDialogButtonBox::ActionRole);
    connect(m_startButton, &QPushButton::clicked, this, &ImportDialog::startImport);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttonBox);
    setLayout(layout);
}

QWidget *ImportDialog::createGeneralPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_fileEdit = new QLineEdit(page);
    auto *browseBtn = new QPushButton(tr("Browse..."), page);
    auto *fileLayout = new QHBoxLayout;
    fileLayout->addWidget(m_fileEdit, 1);
    fileLayout->addWidget(browseBtn);
    form->addRow(tr("Source file:"), fileLayout);
    connect(browseBtn, &QPushButton::clicked, this, &ImportDialog::browseFile);
    connect(m_fileEdit, &QLineEdit::textChanged, this, &ImportDialog::reloadPreview);

    m_formatCombo = new QComboBox(page);
    m_formatCombo->addItem(tr("CSV (Comma Separated)"), QStringLiteral("csv"));
    m_formatCombo->addItem(tr("TSV (Tab Separated)"), QStringLiteral("tsv"));
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImportDialog::reloadPreview);
    form->addRow(tr("Format:"), m_formatCombo);

    m_encodingCombo = new QComboBox(page);
    m_encodingCombo->addItems({QStringLiteral("UTF-8"),
                               QStringLiteral("GBK"),
                               QStringLiteral("ISO-8859-1")});
    connect(m_encodingCombo, &QComboBox::currentTextChanged, this, &ImportDialog::reloadPreview);
    form->addRow(tr("Encoding:"), m_encodingCombo);

    m_headerCheck = new QCheckBox(tr("First row contains column names"), page);
    m_headerCheck->setChecked(true);
    connect(m_headerCheck, &QCheckBox::toggled, this, &ImportDialog::reloadPreview);
    form->addRow(QString(), m_headerCheck);

    m_delimiterEdit = new QLineEdit(page);
    m_delimiterEdit->setText(QStringLiteral(","));
    m_delimiterEdit->setMaxLength(4);
    connect(m_delimiterEdit, &QLineEdit::textChanged, this, &ImportDialog::reloadPreview);
    form->addRow(tr("Delimiter:"), m_delimiterEdit);

    m_qualifierEdit = new QLineEdit(page);
    m_qualifierEdit->setText(QStringLiteral("\""));
    m_qualifierEdit->setMaxLength(4);
    connect(m_qualifierEdit, &QLineEdit::textChanged, this, &ImportDialog::reloadPreview);
    form->addRow(tr("Text qualifier:"), m_qualifierEdit);

    m_startRowSpin = new QSpinBox(page);
    m_startRowSpin->setRange(1, 1000000);
    m_startRowSpin->setValue(1);
    form->addRow(tr("Start from row:"), m_startRowSpin);

    m_batchSpin = new QSpinBox(page);
    m_batchSpin->setRange(1, 10000);
    m_batchSpin->setValue(500);
    form->addRow(tr("Commit batch size:"), m_batchSpin);

    m_truncateCheck = new QCheckBox(tr("Truncate table before import"), page);
    form->addRow(QString(), m_truncateCheck);

    m_ignoreErrorsCheck = new QCheckBox(tr("Ignore row errors and continue"), page);
    form->addRow(QString(), m_ignoreErrorsCheck);

    return page;
}

QWidget *ImportDialog::createMappingPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    auto *infoLabel = new QLabel(tr("Select the source column for each target column below."), page);
    layout->addWidget(infoLabel);

    m_mappingTable = new QTableWidget(page);
    m_mappingTable->setColumnCount(3);
    m_mappingTable->setHorizontalHeaderLabels({tr("Target Column"), tr("Type"), tr("Source Field")});
    m_mappingTable->horizontalHeader()->setStretchLastSection(true);
    m_mappingTable->verticalHeader()->setVisible(false);
    m_mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mappingTable->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_mappingTable, 1);

    auto *buttonLayout = new QHBoxLayout;
    auto *autoMapBtn = new QPushButton(tr("Auto Map"), page);
    auto *clearBtn = new QPushButton(tr("Clear Mapping"), page);
    buttonLayout->addWidget(autoMapBtn);
    buttonLayout->addWidget(clearBtn);
    buttonLayout->addStretch();
    layout->addLayout(buttonLayout);

    connect(autoMapBtn, &QPushButton::clicked, this, &ImportDialog::autoMapColumns);
    connect(clearBtn, &QPushButton::clicked, this, &ImportDialog::clearMapping);

    return page;
}

QWidget *ImportDialog::createLogPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    m_logEdit = new QPlainTextEdit(page);
    m_logEdit->setReadOnly(true);
    layout->addWidget(m_logEdit);
    return page;
}

void ImportDialog::appendLog(const QString &message)
{
    if(!m_logEdit){
        return;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, message));
}

void ImportDialog::setRunning(bool running)
{
    if(m_startButton){
        m_startButton->setEnabled(!running);
    }
}

bool ImportDialog::loadTargetColumns()
{
    QString error;
    const QString handle = uniqueConnectionName(QStringLiteral("import"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
    if(!configureDatabase(db, m_connection, m_databaseName, &error)){
        appendLog(tr("Connection failed: %1").arg(error));
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    QSqlQuery query(db);
    const QString sql = QStringLiteral("SHOW FULL COLUMNS FROM %1")
            .arg(qualifiedTable(m_databaseName, m_tableName));
    if(!query.exec(sql)){
        appendLog(tr("Failed to query columns: %1").arg(query.lastError().text()));
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    m_columns.clear();
    while(query.next()){
        TableColumn column;
        column.name = query.value(QStringLiteral("Field")).toString();
        column.type = query.value(QStringLiteral("Type")).toString();
        column.nullable = query.value(QStringLiteral("Null")).toString().compare(QStringLiteral("YES"), Qt::CaseInsensitive) == 0;
        m_columns.append(column);
    }
    db.close();
    QSqlDatabase::removeDatabase(handle);
    rebuildMappingTable();
    return true;
}

void ImportDialog::rebuildMappingTable()
{
    if(!m_mappingTable){
        return;
    }
    m_mappingTable->setRowCount(m_columns.size());
    m_mappingCombos.clear();
    m_mappingCombos.reserve(m_columns.size());
    for(int row = 0; row < m_columns.size(); ++row){
        const auto &col = m_columns.at(row);
        auto *nameItem = new QTableWidgetItem(col.name);
        auto *typeItem = new QTableWidgetItem(col.type);
        m_mappingTable->setItem(row, 0, nameItem);
        m_mappingTable->setItem(row, 1, typeItem);
        auto *combo = new QComboBox(m_mappingTable);
        combo->addItem(tr("<Skip>"), -1);
        for(int i = 0; i < m_sourceHeaders.size(); ++i){
            combo->addItem(m_sourceHeaders.at(i), i);
        }
        m_mappingTable->setCellWidget(row, 2, combo);
        m_mappingCombos.append(combo);
    }
}

void ImportDialog::browseFile()
{
    const QString initial = m_fileEdit && !m_fileEdit->text().isEmpty()
            ? m_fileEdit->text()
            : QDir::homePath();
    const QString filter = tr("Delimited Files (*.csv *.tsv *.txt);;All Files (*.*)");
    const QString selected = QFileDialog::getOpenFileName(this,
                                                          tr("Select file"),
                                                          initial,
                                                          filter);
    if(!selected.isEmpty() && m_fileEdit){
        m_fileEdit->setText(selected);
    }
}

void ImportDialog::reloadPreview()
{
    if(!m_fileEdit){
        return;
    }
    QStringList headers;
    QList<QStringList> samples;
    if(loadPreviewFromFile(&headers, &samples)){
        m_sourceHeaders = headers;
        rebuildMappingTable();
        appendLog(tr("Loaded preview: %1 columns, %2 sample rows.")
                  .arg(headers.size())
                  .arg(samples.size()));
    }
}

bool ImportDialog::loadPreviewFromFile(QStringList *headers, QList<QStringList> *samples)
{
    if(!headers || !samples){
        return false;
    }
    headers->clear();
    samples->clear();
    const QString path = m_fileEdit ? m_fileEdit->text().trimmed() : QString();
    if(path.isEmpty()){
        return false;
    }
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        appendLog(tr("Cannot open %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    QTextStream stream(&file);
    const QString codecName = m_encodingCombo ? m_encodingCombo->currentText() : QStringLiteral("UTF-8");
    stream.setCodec(codecName.toUtf8().constData());

    int rowIndex = 0;
    while(!stream.atEnd() && samples->size() < 20){
        const QString line = stream.readLine();
        ++rowIndex;
        if(rowIndex == 1 && m_headerCheck && m_headerCheck->isChecked()){
            *headers = parseLine(line);
            if(headers->isEmpty()){
                appendLog(tr("Header row seems empty."));
            }
            continue;
        }
        samples->append(parseLine(line));
    }
    file.close();

    if(headers->isEmpty()){
        const int columns = samples->isEmpty() ? 0 : samples->first().size();
        for(int i = 0; i < columns; ++i){
            headers->append(tr("Column #%1").arg(i + 1));
        }
    }
    if(headers->isEmpty()){
        appendLog(tr("Unable to detect columns from file."));
    }
    return true;
}

QStringList ImportDialog::parseLine(const QString &line) const
{
    QStringList fields;
    const QString delimiter = m_delimiterEdit ? m_delimiterEdit->text() : QStringLiteral(",");
    const QString qualifier = m_qualifierEdit ? m_qualifierEdit->text() : QStringLiteral("\"");
    const QChar delim = interpretControl(delimiter, QLatin1Char(','));
    const QChar quote = qualifier.isEmpty() ? QChar() : interpretControl(qualifier, qualifier.at(0));
    QString current;
    bool inQuotes = false;
    for(int i = 0; i < line.size(); ++i){
        const QChar ch = line.at(i);
        if(quote.isNull()){
            if(ch == delim){
                fields << current;
                current.clear();
            }else{
                current += ch;
            }
            continue;
        }
        if(inQuotes){
            if(ch == quote){
                if(i + 1 < line.size() && line.at(i + 1) == quote){
                    current += quote;
                    ++i;
                }else{
                    inQuotes = false;
                }
            }else{
                current += ch;
            }
        }else{
            if(ch == quote){
                inQuotes = true;
            }else if(ch == delim){
                fields << current;
                current.clear();
            }else{
                current += ch;
            }
        }
    }
    fields << current;
    return fields;
}

void ImportDialog::autoMapColumns()
{
    if(m_mappingCombos.isEmpty() || m_sourceHeaders.isEmpty()){
        return;
    }
    for(int i = 0; i < m_mappingCombos.size(); ++i){
        QComboBox *combo = m_mappingCombos.at(i);
        if(!combo){
            continue;
        }
        const QString targetName = m_columns.value(i).name;
        const int idx = m_sourceHeaders.indexOf(targetName);
        combo->setCurrentIndex(idx >= 0 ? idx + 1 : 0);
    }
}

void ImportDialog::clearMapping()
{
    for(QComboBox *combo : std::as_const(m_mappingCombos)){
        if(combo){
            combo->setCurrentIndex(0);
        }
    }
}

ImportOptions ImportDialog::gatherOptions() const
{
    ImportOptions opt;
    opt.filePath = m_fileEdit ? m_fileEdit->text().trimmed() : QString();
    opt.format = m_formatCombo ? m_formatCombo->currentData().toString() : QStringLiteral("csv");
    opt.encoding = m_encodingCombo ? m_encodingCombo->currentText() : QStringLiteral("UTF-8");
    opt.delimiter = m_delimiterEdit ? m_delimiterEdit->text() : QStringLiteral(",");
    opt.qualifier = m_qualifierEdit ? m_qualifierEdit->text() : QStringLiteral("\"");
    opt.hasHeader = m_headerCheck && m_headerCheck->isChecked();
    opt.startRow = m_startRowSpin ? m_startRowSpin->value() : 1;
    opt.batchSize = m_batchSpin ? m_batchSpin->value() : 500;
    opt.truncateBefore = m_truncateCheck && m_truncateCheck->isChecked();
    opt.ignoreErrors = m_ignoreErrorsCheck && m_ignoreErrorsCheck->isChecked();
    if(opt.delimiter.isEmpty()){
        opt.delimiter = QStringLiteral(",");
    }
    return opt;
}

QVector<int> ImportDialog::currentMapping() const
{
    QVector<int> mapping;
    mapping.reserve(m_mappingCombos.size());
    for(QComboBox *combo : m_mappingCombos){
        if(!combo){
            mapping << -1;
            continue;
        }
        mapping << combo->currentData().toInt();
    }
    return mapping;
}

bool ImportDialog::truncateTarget(QSqlDatabase &db, QString *errorMessage) const
{
    QSqlQuery query(db);
    const QString sql = QStringLiteral("TRUNCATE TABLE %1").arg(qualifiedTable(m_databaseName, m_tableName));
    if(!query.exec(sql)){
        if(errorMessage){
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

void ImportDialog::startImport()
{
    ImportOptions options = gatherOptions();
    if(options.filePath.isEmpty()){
        appendLog(tr("Please choose a source file."));
        return;
    }
    QFileInfo info(options.filePath);
    if(!info.exists()){
        appendLog(tr("File %1 not found.").arg(QDir::toNativeSeparators(options.filePath)));
        return;
    }
    const QVector<int> mapping = currentMapping();
    bool hasMapping = false;
    for(int idx : mapping){
        if(idx >= 0){
            hasMapping = true;
            break;
        }
    }
    if(!hasMapping){
        appendLog(tr("Please map at least one column."));
        return;
    }
    if(options.format != QStringLiteral("csv") && options.format != QStringLiteral("tsv")){
        appendLog(tr("Only delimited (CSV/TSV) files are supported at this time."));
        return;
    }
    setRunning(true);
    if(runImport(options)){
        appendLog(tr("Import finished."));
    }
    setRunning(false);
}

bool ImportDialog::runImport(const ImportOptions &options)
{
    QString error;
    const QString handle = uniqueConnectionName(QStringLiteral("import"));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), handle);
    if(!configureDatabase(db, m_connection, m_databaseName, &error)){
        appendLog(tr("Connection failed: %1").arg(error));
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    if(options.truncateBefore){
        if(!truncateTarget(db, &error)){
            appendLog(tr("Failed to truncate table: %1").arg(error));
            db.close();
            QSqlDatabase::removeDatabase(handle);
            return false;
        }
    }

    QVector<int> mapping = currentMapping();
    QStringList columnNames;
    QStringList quotedColumns;
    QVector<int> sourceIndexes;
    for(int i = 0; i < mapping.size(); ++i){
        const int src = mapping.at(i);
        if(src < 0){
            continue;
        }
        const QString colName = m_columns.value(i).name;
        columnNames << colName;
        quotedColumns << quoted(colName);
        sourceIndexes << src;
    }
    if(columnNames.isEmpty()){
        appendLog(tr("No columns selected."));
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    QString placeholderSegment;
    if(!columnNames.isEmpty()){
        QStringList placeholders;
        placeholders.reserve(columnNames.size());
        for(int i = 0; i < columnNames.size(); ++i){
            placeholders << QStringLiteral("?");
        }
        placeholderSegment = placeholders.join(QStringLiteral(", "));
    }
    const QString sql = QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
            .arg(qualifiedTable(m_databaseName, m_tableName),
                 quotedColumns.join(QStringLiteral(", ")),
                 placeholderSegment);
    QSqlQuery query(db);
    if(!query.prepare(sql)){
        appendLog(tr("Failed to prepare INSERT: %1").arg(query.lastError().text()));
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }

    QFile file(options.filePath);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        appendLog(tr("Cannot open %1").arg(QDir::toNativeSeparators(options.filePath)));
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec(options.encoding.toUtf8().constData());

    int lineNumber = 0;
    int importedRows = 0;
    int batchCount = 0;
    const int startRow = qMax(1, options.startRow);

    if(!db.transaction()){
        appendLog(tr("Unable to start transaction: %1").arg(db.lastError().text()));
        file.close();
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    while(!stream.atEnd()){
        const QString rawLine = stream.readLine();
        ++lineNumber;
        if(options.hasHeader && lineNumber == 1){
            continue;
        }
        if(lineNumber < startRow){
            continue;
        }
        const QStringList cells = parseLine(rawLine);
        for(int col = 0; col < sourceIndexes.size(); ++col){
            const int srcIdx = sourceIndexes.at(col);
            const QString value = (srcIdx >= 0 && srcIdx < cells.size()) ? cells.at(srcIdx) : QString();
            if(value.isEmpty()){
                query.bindValue(col, QVariant());
            }else{
                query.bindValue(col, value);
            }
        }
        if(!query.exec()){
            appendLog(tr("Line %1 failed: %2").arg(lineNumber).arg(query.lastError().text()));
            if(!options.ignoreErrors){
                db.rollback();
                file.close();
                db.close();
                QSqlDatabase::removeDatabase(handle);
                return false;
            }
            continue;
        }
        ++importedRows;
        ++batchCount;
        if(batchCount >= options.batchSize){
            if(!db.commit()){
                appendLog(tr("Failed to commit batch: %1").arg(db.lastError().text()));
                file.close();
                db.close();
                QSqlDatabase::removeDatabase(handle);
                return false;
            }
            if(!db.transaction()){
                appendLog(tr("Unable to start transaction: %1").arg(db.lastError().text()));
                file.close();
                db.close();
                QSqlDatabase::removeDatabase(handle);
                return false;
            }
            batchCount = 0;
        }
    }
    if(!db.commit()){
        appendLog(tr("Failed to commit transaction: %1").arg(db.lastError().text()));
        file.close();
        db.close();
        QSqlDatabase::removeDatabase(handle);
        return false;
    }
    file.close();
    appendLog(tr("Imported %1 rows.").arg(importedRows));
    db.close();
    QSqlDatabase::removeDatabase(handle);
    return true;
}
