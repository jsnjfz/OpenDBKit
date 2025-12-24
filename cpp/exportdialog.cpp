#include "exportdialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

ExportDialog::ExportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export Data"));
    resize(640, 520);
    buildUi();
}

void ExportDialog::buildUi()
{
    tabs = new QTabWidget(this);
    tabs->addTab(createGeneralPage(), tr("General"));
    tabs->addTab(createFieldsPage(), tr("Fields"));
    tabs->addTab(createAdvancedPage(), tr("Advanced"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if(pathEdit && pathEdit->text().trimmed().isEmpty()){
            QMessageBox::warning(this, tr("Export"), tr("Please select a destination file."));
            tabs->setCurrentIndex(0);
            pathEdit->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &ExportDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
    setLayout(layout);
}

QWidget *ExportDialog::createGeneralPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    pathEdit = new QLineEdit(page);
    auto *browseBtn = new QPushButton(tr("Browse..."), page);
    auto *pathLayout = new QHBoxLayout;
    pathLayout->addWidget(pathEdit, 1);
    pathLayout->addWidget(browseBtn);
    form->addRow(tr("Destination:"), pathLayout);
    connect(browseBtn, &QPushButton::clicked, this, &ExportDialog::browseFile);

    formatCombo = new QComboBox(page);
    formatCombo->addItem(tr("CSV (Comma Separated)"), QStringLiteral("csv"));
    formatCombo->addItem(tr("TSV (Tab Separated)"), QStringLiteral("tsv"));
    formatCombo->addItem(tr("Excel Workbook (*.xlsx)"), QStringLiteral("xlsx"));
    formatCombo->addItem(tr("Custom Delimited"), QStringLiteral("custom"));
    connect(formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExportDialog::onFormatChanged);
    form->addRow(tr("Format:"), formatCombo);

    headersCheck = new QCheckBox(tr("Include header row"), page);
    headersCheck->setChecked(true);
    form->addRow(QString(), headersCheck);

    rowLimitSpin = new QSpinBox(page);
    rowLimitSpin->setRange(0, 100000000);
    rowLimitSpin->setSpecialValueText(tr("Unlimited"));
    rowLimitSpin->setValue(0);
    form->addRow(tr("Row limit:"), rowLimitSpin);

    return page;
}

QWidget *ExportDialog::createFieldsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    fieldList = new QListWidget(page);
    fieldList->setSelectionMode(QAbstractItemView::SingleSelection);
    fieldList->setAlternatingRowColors(true);
    layout->addWidget(fieldList, 1);

    auto *buttonLayout = new QHBoxLayout;
    auto *selectAllBtn = new QPushButton(tr("Select All"), page);
    auto *clearBtn = new QPushButton(tr("Clear"), page);
    auto *moveUpBtn = new QPushButton(tr("Move Up"), page);
    auto *moveDownBtn = new QPushButton(tr("Move Down"), page);
    buttonLayout->addWidget(selectAllBtn);
    buttonLayout->addWidget(clearBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(moveUpBtn);
    buttonLayout->addWidget(moveDownBtn);
    layout->addLayout(buttonLayout);

    connect(selectAllBtn, &QPushButton::clicked, this, &ExportDialog::selectAllColumns);
    connect(clearBtn, &QPushButton::clicked, this, &ExportDialog::clearColumnSelection);
    connect(moveUpBtn, &QPushButton::clicked, this, &ExportDialog::moveColumnUp);
    connect(moveDownBtn, &QPushButton::clicked, this, &ExportDialog::moveColumnDown);

    return page;
}

QWidget *ExportDialog::createAdvancedPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    delimiterEdit = new QLineEdit(page);
    delimiterEdit->setText(QStringLiteral(","));
    form->addRow(tr("Delimiter:"), delimiterEdit);

    qualifierEdit = new QLineEdit(page);
    qualifierEdit->setText(QStringLiteral("\""));
    qualifierEdit->setMaxLength(4);
    form->addRow(tr("Text qualifier:"), qualifierEdit);

    escapeCheck = new QCheckBox(tr("Escape repeated qualifiers"), page);
    escapeCheck->setChecked(true);
    form->addRow(QString(), escapeCheck);

    nullEdit = new QLineEdit(page);
    nullEdit->setPlaceholderText(tr("<NULL>"));
    form->addRow(tr("NULL placeholder:"), nullEdit);

    encodingCombo = new QComboBox(page);
    encodingCombo->addItems({QStringLiteral("UTF-8"), QStringLiteral("GBK"), QStringLiteral("ISO-8859-1")});
    form->addRow(tr("Encoding:"), encodingCombo);

    lineEndingCombo = new QComboBox(page);
    lineEndingCombo->addItem(tr("Windows (CRLF)"), QStringLiteral("CRLF"));
    lineEndingCombo->addItem(tr("Unix (LF)"), QStringLiteral("LF"));
    form->addRow(tr("Line ending:"), lineEndingCombo);

    return page;
}

void ExportDialog::setColumns(const QStringList &columns)
{
    if(!fieldList){
        return;
    }
    fieldList->clear();
    for(const QString &col : columns){
        auto *item = new QListWidgetItem(col, fieldList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        item->setCheckState(Qt::Checked);
    }
}

void ExportDialog::setInitialPath(const QString &path)
{
    if(pathEdit){
        pathEdit->setText(path);
    }
}

void ExportDialog::setDefaultFormat(const QString &formatId)
{
    if(!formatCombo){
        return;
    }
    const int index = formatCombo->findData(formatId);
    if(index >= 0){
        formatCombo->setCurrentIndex(index);
    }
}

ExportOptions ExportDialog::options() const
{
    ExportOptions opts;
    opts.filePath = pathEdit ? pathEdit->text().trimmed() : QString();
    opts.format = formatCombo ? formatCombo->currentData().toString() : QStringLiteral("csv");
    opts.includeHeaders = headersCheck ? headersCheck->isChecked() : true;
    opts.rowLimit = rowLimitSpin ? rowLimitSpin->value() : 0;
    if(fieldList){
        for(int i = 0; i < fieldList->count(); ++i){
            const QListWidgetItem *item = fieldList->item(i);
            if(item->checkState() == Qt::Checked){
                opts.selectedColumns << item->text();
            }
        }
    }
    if(delimiterEdit){
        opts.delimiter = delimiterEdit->text();
    }
    if(qualifierEdit){
        opts.textQualifier = qualifierEdit->text();
    }
    if(escapeCheck){
        opts.escapeEmbedded = escapeCheck->isChecked();
    }
    if(nullEdit){
        opts.nullRepresentation = nullEdit->text();
    }
    if(encodingCombo){
        opts.encoding = encodingCombo->currentText();
    }
    if(lineEndingCombo){
        opts.lineEnding = lineEndingCombo->currentData().toString();
    }
    if(opts.delimiter.isEmpty()){
        opts.delimiter = QStringLiteral(",");
    }
    if(opts.textQualifier.isEmpty()){
        opts.textQualifier = QStringLiteral("\"");
    }
    if(opts.selectedColumns.isEmpty() && fieldList){
        // fallback to all columns
        for(int i = 0; i < fieldList->count(); ++i){
            opts.selectedColumns << fieldList->item(i)->text();
        }
    }
    return opts;
}

void ExportDialog::browseFile()
{
    QString initial = pathEdit && !pathEdit->text().isEmpty()
            ? pathEdit->text()
            : QDir::homePath();
    QString filter = tr("CSV Files (*.csv);;All Files (*.*)");
    if(formatCombo){
        const QString formatId = formatCombo->currentData().toString();
        if(formatId == QStringLiteral("xlsx")){
            filter = tr("Excel Workbook (*.xlsx);;All Files (*.*)");
            if(initial.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)){
                initial.chop(4);
                initial.append(QStringLiteral(".xlsx"));
            }
        }else if(formatId == QStringLiteral("tsv")){
            filter = tr("TSV Files (*.tsv);;All Files (*.*)");
        }
    }
    const QString selected = QFileDialog::getSaveFileName(this, tr("Export"),
                                                          initial,
                                                          filter);
    if(!selected.isEmpty() && pathEdit){
        pathEdit->setText(selected);
    }
}

void ExportDialog::selectAllColumns()
{
    if(!fieldList){
        return;
    }
    for(int i = 0; i < fieldList->count(); ++i){
        fieldList->item(i)->setCheckState(Qt::Checked);
    }
}

void ExportDialog::clearColumnSelection()
{
    if(!fieldList){
        return;
    }
    for(int i = 0; i < fieldList->count(); ++i){
        fieldList->item(i)->setCheckState(Qt::Unchecked);
    }
}

void ExportDialog::moveColumnUp()
{
    if(!fieldList){
        return;
    }
    QListWidgetItem *current = fieldList->currentItem();
    if(!current){
        return;
    }
    int row = fieldList->row(current);
    if(row <= 0){
        return;
    }
    QListWidgetItem *take = fieldList->takeItem(row);
    fieldList->insertItem(row - 1, take);
    fieldList->setCurrentItem(take);
}

void ExportDialog::moveColumnDown()
{
    if(!fieldList){
        return;
    }
    QListWidgetItem *current = fieldList->currentItem();
    if(!current){
        return;
    }
    int row = fieldList->row(current);
    if(row < 0 || row >= fieldList->count() - 1){
        return;
    }
    QListWidgetItem *take = fieldList->takeItem(row);
    fieldList->insertItem(row + 1, take);
    fieldList->setCurrentItem(take);
}

void ExportDialog::onFormatChanged(int index)
{
    Q_UNUSED(index);
    updateFieldsFromFormat();
}

void ExportDialog::updateFieldsFromFormat()
{
    if(!formatCombo || !delimiterEdit || !qualifierEdit){
        return;
    }
    const QString mode = formatCombo->currentData().toString();
    if(mode == QStringLiteral("csv")){
        delimiterEdit->setText(QStringLiteral(","));
        qualifierEdit->setText(QStringLiteral("\""));
    }else if(mode == QStringLiteral("tsv")){
        delimiterEdit->setText(QStringLiteral("\t"));
        qualifierEdit->setText(QStringLiteral("\""));
    }else if(mode == QStringLiteral("xlsx")){
        delimiterEdit->setText(QStringLiteral(","));
        qualifierEdit->setText(QStringLiteral("\""));
    }
    const bool allowCustom = (mode == QStringLiteral("custom"));
    const bool allowDelimited = (mode != QStringLiteral("xlsx"));
    delimiterEdit->setEnabled(allowCustom);
    qualifierEdit->setEnabled(allowDelimited);
    escapeCheck->setEnabled(allowDelimited);
}
