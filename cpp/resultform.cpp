#include "resultform.h"
#include "exportdialog.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDebug>
#include <QPainter>
#include <QStandardPaths>
#include <QAbstractItemView>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLineEdit>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QTimeEdit>
#include <QMap>
#include <QSortFilterProxyModel>
#include <QSet>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QItemSelection>
#include <QStyledItemDelegate>
#include <QTextStream>
#include <QXmlStreamWriter>
#include <private/qzipwriter_p.h>
#include <algorithm>

// Role to mark NULL values (UserRole+2 is used for rowId in queryform.cpp)
static const int NullRole = Qt::UserRole + 3;
// Role to store column type (QVariant::Type)
static const int TypeRole = Qt::UserRole + 4;

// Custom delegate to display NULL values with special style
class NullAwareDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        int sourceRow = index.row();
        if(auto *proxy = qobject_cast<const QSortFilterProxyModel*>(index.model())){
            sourceRow = proxy->mapToSource(index).row();
        }
        if(sourceRow % 3 == 2 && !(opt.state & QStyle::State_Selected)){
            opt.backgroundBrush = QColor(240, 248, 255);
        }
        bool isNull = false;
        if(auto *proxy = qobject_cast<const QSortFilterProxyModel*>(index.model())){
            QModelIndex sourceIndex = proxy->mapToSource(index);
            if(auto *srcModel = qobject_cast<const QStandardItemModel*>(proxy->sourceModel())){
                if(auto *item = srcModel->itemFromIndex(sourceIndex)){
                    QVariant nullData = item->data(NullRole);
                    isNull = nullData.isValid() && nullData.toBool();
                }
            }
        } else if(auto *stdModel = qobject_cast<const QStandardItemModel*>(index.model())){
            if(auto *item = stdModel->itemFromIndex(index)){
                QVariant nullData = item->data(NullRole);
                isNull = nullData.isValid() && nullData.toBool();
            }
        }

        if(isNull){
            opt.text = QStringLiteral("NULL");
            opt.font.setItalic(true);
            opt.palette.setColor(QPalette::Text, QColor(160, 160, 160));
        }
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        // Check column type from TypeRole
        int colType = QVariant::Invalid;
        if(auto *proxy = qobject_cast<const QSortFilterProxyModel*>(index.model())){
            QModelIndex sourceIndex = proxy->mapToSource(index);
            if(auto *srcModel = qobject_cast<const QStandardItemModel*>(proxy->sourceModel())){
                if(auto *item = srcModel->itemFromIndex(sourceIndex)){
                    colType = item->data(TypeRole).toInt();
                }
            }
        } else if(auto *stdModel = qobject_cast<const QStandardItemModel*>(index.model())){
            if(auto *item = stdModel->itemFromIndex(index)){
                colType = item->data(TypeRole).toInt();
            }
        }

        // Return date/datetime editor for date/datetime types
        if(colType == QVariant::Date){
            auto *dateEdit = new QDateEdit(parent);
            dateEdit->setCalendarPopup(true);
            dateEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
            return dateEdit;
        }
        if(colType == QVariant::DateTime){
            auto *dateTimeEdit = new QDateTimeEdit(parent);
            dateTimeEdit->setCalendarPopup(true);
            dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            return dateTimeEdit;
        }
        if(colType == QVariant::Time){
            auto *timeEdit = new QTimeEdit(parent);
            timeEdit->setDisplayFormat(QStringLiteral("HH:mm:ss"));
            return timeEdit;
        }

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

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if(auto *dateEdit = qobject_cast<QDateEdit*>(editor)){
            QDate date = QDate::fromString(index.data().toString(), QStringLiteral("yyyy-MM-dd"));
            if(!date.isValid()) date = QDate::currentDate();
            dateEdit->setDate(date);
            return;
        }
        if(auto *dateTimeEdit = qobject_cast<QDateTimeEdit*>(editor)){
            QDateTime dt = QDateTime::fromString(index.data().toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if(!dt.isValid()) dt = QDateTime::currentDateTime();
            dateTimeEdit->setDateTime(dt);
            return;
        }
        if(auto *timeEdit = qobject_cast<QTimeEdit*>(editor)){
            QTime time = QTime::fromString(index.data().toString(), QStringLiteral("HH:mm:ss"));
            if(!time.isValid()) time = QTime::currentTime();
            timeEdit->setTime(time);
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if(auto *dateEdit = qobject_cast<QDateEdit*>(editor)){
            model->setData(index, dateEdit->date().toString(QStringLiteral("yyyy-MM-dd")));
            return;
        }
        if(auto *dateTimeEdit = qobject_cast<QDateTimeEdit*>(editor)){
            model->setData(index, dateTimeEdit->dateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
            return;
        }
        if(auto *timeEdit = qobject_cast<QTimeEdit*>(editor)){
            model->setData(index, timeEdit->time().toString(QStringLiteral("HH:mm:ss")));
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }
};

namespace {

QString excelColumnName(int index)
{
    QString name;
    int col = index;
    while(col >= 0){
        QChar ch = QChar('A' + (col % 26));
        name.prepend(ch);
        col = (col / 26) - 1;
    }
    return name;
}

bool isNumeric(const QString &value, QString *normalized)
{
    bool ok = false;
    const double num = value.toDouble(&ok);
    if(!ok){
        return false;
    }
    if(normalized){
        *normalized = QString::number(num, 'g', 15);
    }
    return true;
}

}

class ResultFilterProxy : public QSortFilterProxyModel
{
public:
    explicit ResultFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setDynamicSortFilter(true);
    }

    void setFilterNeedle(const QString &text)
    {
        const QString trimmed = text.trimmed();
        if(needle == trimmed){
            return;
        }
        needle = trimmed;
        invalidateFilter();
    }

    QString filterNeedle() const
    {
        return needle;
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        if(needle.isEmpty()){
            return true;
        }
        const auto *src = sourceModel();
        if(!src){
            return true;
        }
        for(int c = 0; c < src->columnCount(sourceParent); ++c){
            const QModelIndex idx = src->index(sourceRow, c, sourceParent);
            if(!idx.isValid()){
                continue;
            }
            const QString text = src->data(idx, Qt::DisplayRole).toString();
            if(text.contains(needle, Qt::CaseInsensitive)){
                return true;
            }
            const QVariant boolData = src->data(idx, Qt::UserRole + 1);
            if(boolData.isValid()){
                const QString boolText = boolData.toBool() ? QStringLiteral("1") : QStringLiteral("0");
                if(boolText.contains(needle, Qt::CaseInsensitive)){
                    return true;
                }
            }
        }
        return false;
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const QString lValue = sourceModel()->data(left, Qt::DisplayRole).toString();
        const QString rValue = sourceModel()->data(right, Qt::DisplayRole).toString();
        QString lNumeric;
        QString rNumeric;
        const bool lIsNumeric = isNumeric(lValue, &lNumeric);
        const bool rIsNumeric = isNumeric(rValue, &rNumeric);
        if(lIsNumeric && rIsNumeric){
            bool okL = false;
            bool okR = false;
            const double ln = lNumeric.toDouble(&okL);
            const double rn = rNumeric.toDouble(&okR);
            if(okL && okR){
                return ln < rn;
            }
        }
        return QString::localeAwareCompare(lValue, rValue) < 0;
    }

private:
    QString needle;
};

namespace {

QStandardItem *createTextItem(const QString &text, bool editable = false)
{
    auto *item = new QStandardItem(text);
    item->setEditable(editable);
    return item;
}

QStandardItem *createFlagItem(bool checked, const QString &text)
{
    auto *item = createTextItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setData(checked, Qt::UserRole + 1);
    return item;
}

}

ResultForm::ResultForm(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    toolbarWidget = createToolbar();
    layout->addWidget(toolbarWidget);

    stack = new QStackedLayout;
    layout->addLayout(stack);

    tableView = new QTableView(this);
    tableView->setItemDelegate(new NullAwareDelegate(tableView));
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableView->setAlternatingRowColors(true);
    tableView->setWordWrap(false);
    tableView->setMouseTracking(true);
    tableView->setFrameShape(QFrame::NoFrame);
    tableView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    tableView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tableView->setStyleSheet(QStringLiteral(
        "QTableView {"
        "  background: #fdfdfd;"
        "  gridline-color: #e5e9f2;"
        "  alternate-background-color: #f6f9ff;"
        "}"
        "QTableView::item:selected {"
        "  background: #d0e8ff;"
        "}"
        "QTableView::item:selected:!active {"
        "  background: #e0e8f0;"
        "}"
    ));
    auto *hHeader = tableView->horizontalHeader();
    hHeader->setStretchLastSection(true);
    hHeader->setSectionsClickable(true);
    hHeader->setSortIndicatorShown(true);
    hHeader->setHighlightSections(false);
    hHeader->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hHeader->setMinimumHeight(36);
    auto *vHeader = tableView->verticalHeader();
    vHeader->setDefaultSectionSize(32);
    vHeader->setVisible(true);
    vHeader->setDefaultAlignment(Qt::AlignCenter);
    vHeader->setMinimumWidth(40);
    vHeader->setSectionsClickable(true);
    vHeader->setHighlightSections(true);
    tableView->setSortingEnabled(true);
    tableView->setShowGrid(true);

    model = new QStandardItemModel(this);
    proxy = new ResultFilterProxy(this);
    proxy->setSourceModel(model);
    tableView->setModel(proxy);

    messageLabel = new QLabel(tr("Ready."), this);
    messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    stack->addWidget(tableView);
    stack->addWidget(messageLabel);

    stack->setCurrentWidget(messageLabel);
    summaryBase = tr("Ready.");

    // 鼠标按下时开始选择
    connect(vHeader, &QHeaderView::sectionPressed, this, [this](int logicalIndex) {
        if(!tableView || !tableView->model()){
            return;
        }
        auto *selModel = tableView->selectionModel();
        if(!selModel){
            return;
        }
        const int columnCount = tableView->model()->columnCount();
        if(columnCount <= 0){
            return;
        }

        const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
        QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows;
        if(mods & Qt::ControlModifier){
            flags = QItemSelectionModel::Toggle | QItemSelectionModel::Rows;
        }
        if(mods & Qt::ShiftModifier){
            flags = QItemSelectionModel::Select | QItemSelectionModel::Rows;
        }

        QModelIndex index = tableView->model()->index(logicalIndex, 0);
        selModel->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        selModel->select(index, flags);
    });

    // 拖动时扩展选择
    connect(vHeader, &QHeaderView::sectionEntered, this, [this](int logicalIndex) {
        if(!tableView || !tableView->model()){
            return;
        }
        // 只在鼠标左键按下状态下处理
        if(!(QGuiApplication::mouseButtons() & Qt::LeftButton)){
            return;
        }
        auto *selModel = tableView->selectionModel();
        if(!selModel){
            return;
        }
        const int columnCount = tableView->model()->columnCount();
        if(columnCount <= 0){
            return;
        }

        QModelIndex index = tableView->model()->index(logicalIndex, 0);
        selModel->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    });
}

QWidget *ResultForm::createToolbar()
{
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(8);

    summaryLabel = new QLabel(tr("Ready."), this);
    summaryLabel->setObjectName(QStringLiteral("summaryLabel"));
    summaryLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    toolbar->addStretch();
    toolbar->addWidget(summaryLabel, 1);

    auto *wrapper = new QWidget(this);
    wrapper->setLayout(toolbar);
    return wrapper;
}

void ResultForm::showRows(const QStringList &headers,
                          const QList<QStringList> &rows,
                          qint64 elapsedMs,
                          const QString &note,
                          bool editable)
{
    if(!model || !tableView){
        return;
    }
    const bool sortingEnabled = tableView->isSortingEnabled();
    tableView->setSortingEnabled(false);
    tableView->setEditTriggers(editable
                               ? QAbstractItemView::DoubleClicked
                                 | QAbstractItemView::SelectedClicked
                                 | QAbstractItemView::EditKeyPressed
                               : QAbstractItemView::NoEditTriggers);
    model->clear();
    model->setHorizontalHeaderLabels(headers);
    for(const auto &row : rows){
        QList<QStandardItem*> items;
        items.reserve(headers.size());
        for(int c = 0; c < headers.size(); ++c){
            auto *item = createTextItem(row.value(c), editable);
            item->setData(false, NullRole);
            items << item;
        }
        model->appendRow(items);
    }
    tableView->setSortingEnabled(sortingEnabled);
    stack->setCurrentWidget(tableView);
    mode = DisplayMode::Data;
    QString summary = tr("Rows: %1%2")
            .arg(rows.count())
            .arg(elapsedMs >= 0 ? tr("  Time: %1 ms").arg(elapsedMs) : QString());
    if(!note.trimmed().isEmpty()){
        summary += tr("  %1").arg(note.trimmed());
    }
    rememberHeaders(headers);
    rememberSummary(summary.trimmed());
    applyFilter();
    autoFitColumns();
}

void ResultForm::showRows(const QStringList &headers,
                          const QList<QVariantList> &rows,
                          qint64 elapsedMs,
                          const QString &note,
                          bool editable,
                          const QVector<int> &columnTypes)
{
    if(!model || !tableView){
        return;
    }
    const bool sortingEnabled = tableView->isSortingEnabled();
    tableView->setSortingEnabled(false);
    tableView->setEditTriggers(editable
                               ? QAbstractItemView::DoubleClicked
                                 | QAbstractItemView::SelectedClicked
                                 | QAbstractItemView::EditKeyPressed
                               : QAbstractItemView::NoEditTriggers);
    model->clear();
    model->setHorizontalHeaderLabels(headers);
    const int colCount = headers.size();
    for(const auto &row : rows){
        QList<QStandardItem*> items;
        items.reserve(colCount);
        for(int c = 0; c < colCount; ++c){
            const QVariant &val = row.value(c);
            auto *item = createTextItem(val.toString(), editable);
            item->setData(val.isNull(), NullRole);
            if(c < columnTypes.size()){
                item->setData(columnTypes.at(c), TypeRole);
            }
            items << item;
        }
        model->appendRow(items);
    }
    tableView->setSortingEnabled(sortingEnabled);
    stack->setCurrentWidget(tableView);
    mode = DisplayMode::Data;
    QString summary = tr("Rows: %1%2")
            .arg(rows.count())
            .arg(elapsedMs >= 0 ? tr("  Time: %1 ms").arg(elapsedMs) : QString());
    if(!note.trimmed().isEmpty()){
        summary += tr("  %1").arg(note.trimmed());
    }
    rememberHeaders(headers);
    rememberSummary(summary.trimmed());
    applyFilter();
    autoFitColumns();
}

void ResultForm::showTableStructure(const QList<ColumnInfo> &columns, qint64 elapsedMs)
{
    if(!model || !tableView){
        return;
    }
    const bool sortingEnabled = tableView->isSortingEnabled();
    tableView->setSortingEnabled(false);
    model->clear();
    const QStringList headers = {
        tr("Name"),
        tr("Type"),
        tr("Unsigned"),
        tr("Zerofill"),
        tr("Not Null"),
        tr("Key"),
        tr("Auto Inc"),
        tr("Default/Expr"),
        tr("Generated"),
        tr("Comment")
    };
    model->setHorizontalHeaderLabels(headers);
    const QString yesText = tr("是");
    const QString noText = tr("否");
    for(const auto &col : columns){
        QList<QStandardItem*> items;
        items.reserve(headers.size());
        items << createTextItem(col.name);
        items << createTextItem(col.type);
        items << createFlagItem(col.unsignedFlag, col.unsignedFlag ? yesText : noText);
        items << createFlagItem(col.zeroFill, col.zeroFill ? yesText : noText);
        items << createFlagItem(col.notNull, col.notNull ? yesText : noText);
        items << createFlagItem(col.key, col.key ? yesText : noText);
        items << createFlagItem(col.autoIncrement, col.autoIncrement ? yesText : noText);
        items << createTextItem(col.defaultExpression);
        items << createFlagItem(col.generated, col.generated ? yesText : noText);
        items << createTextItem(col.comment);
        model->appendRow(items);
    }
    tableView->setSortingEnabled(sortingEnabled);
    stack->setCurrentWidget(tableView);
    mode = DisplayMode::Structure;
    QString summary = tr("Columns: %1%2")
            .arg(columns.count())
            .arg(elapsedMs >= 0 ? tr("  Time: %1 ms").arg(elapsedMs) : QString());
    rememberHeaders(headers);
    rememberSummary(summary.trimmed());
    applyFilter();
    autoFitColumns();
}

void ResultForm::showAffectRows(int affectedRows, qint64 elapsedMs)
{
    const QString message = tr("Affected rows: %1%2")
            .arg(affectedRows)
            .arg(elapsedMs >= 0 ? tr("  Time: %1 ms").arg(elapsedMs) : QString());
    showMessage(message);
}

void ResultForm::showMessage(const QString &text)
{
    messageLabel->setText(text);
    stack->setCurrentWidget(messageLabel);
    mode = DisplayMode::Message;
    rememberHeaders({});
    rememberSummary(text);
    updateSummaryLabel(text);
}

void ResultForm::reset()
{
    if(model){
        model->clear();
    }
    if(tableView && tableView->selectionModel()){
        tableView->selectionModel()->clear();
    }
    messageLabel->setText(tr("Ready."));
    stack->setCurrentWidget(messageLabel);
    mode = DisplayMode::Message;
    rememberHeaders({});
    rememberSummary(tr("Ready."));
    updateSummaryLabel(summaryBase);
}

void ResultForm::updateSummaryLabel(const QString &text)
{
    if(summaryLabel){
        summaryLabel->setText(text);
    }
    emit summaryChanged(text);
}

QString ResultForm::selectedRowsAsTsv() const
{
    if(!tableView || !model || model->rowCount() == 0){
        return {};
    }
    QStringList chunks;
    const auto *selModel = tableView->selectionModel();
    if(!selModel){
        return {};
    }
    QModelIndexList selected = selModel->selectedRows();
    std::sort(selected.begin(), selected.end(), [](const QModelIndex &a, const QModelIndex &b) {
        if(a.row() == b.row()){
            return a.column() < b.column();
        }
        return a.row() < b.row();
    });
    if(selected.isEmpty() && proxy){
        for(int r = 0; r < proxy->rowCount(); ++r){
            selected << proxy->index(r, 0);
        }
    }
    if(selected.isEmpty()){
        return {};
    }
    QStringList header;
    for(int c = 0; c < model->columnCount(); ++c){
        header << model->headerData(c, Qt::Horizontal).toString();
    }
    chunks << header.join('\t');
    QAbstractItemModel *viewModel = tableView->model();
    if(!viewModel){
        return {};
    }
    for(const QModelIndex &idx : selected){
        QStringList row;
        for(int c = 0; c < model->columnCount(); ++c){
            const QModelIndex cell = viewModel->index(idx.row(), c);
            row << itemTextForExport(cell);
        }
        chunks << row.join('\t');
    }
    return chunks.join(QLatin1Char('\n'));
}

QString ResultForm::selectedCellsAsTsv() const
{
    if(!tableView){
        return {};
    }
    const auto *selModel = tableView->selectionModel();
    if(!selModel){
        return {};
    }
    QModelIndexList indexes = selModel->selectedIndexes();
    if(indexes.isEmpty()){
        return {};
    }

    QList<QModelIndex> sorted = indexes;
    std::sort(sorted.begin(), sorted.end(), [](const QModelIndex &a, const QModelIndex &b) {
        if(a.row() == b.row()){
            return a.column() < b.column();
        }
        return a.row() < b.row();
    });

    QMap<int, QMap<int, QString>> rows;
    for(const QModelIndex &index : sorted){
        rows[index.row()][index.column()] = itemTextForExport(index);
    }

    QStringList serialized;
    for(auto it = rows.cbegin(); it != rows.cend(); ++it){
        QStringList cols;
        const auto columnMap = it.value();
        for(auto colIt = columnMap.cbegin(); colIt != columnMap.cend(); ++colIt){
            cols << colIt.value();
        }
        serialized << cols.join('\t');
    }
    return serialized.join(QLatin1Char('\n'));
}

QString ResultForm::itemTextForExport(const QModelIndex &index) const
{
    if(!index.isValid()){
        return {};
    }
    // Check if this cell is NULL
    if(proxy && model){
        QModelIndex srcIndex = proxy->mapToSource(index);
        if(auto *item = model->itemFromIndex(srcIndex)){
            QVariant nullData = item->data(NullRole);
            if(nullData.isValid() && nullData.toBool()){
                return QStringLiteral("NULL");
            }
        }
    }
    const QVariant boolData = index.data(Qt::UserRole + 1);
    if(boolData.isValid()){
        return boolData.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    }
    return index.data(Qt::DisplayRole).toString();
}

QStringList ResultForm::visibleHeaders() const
{
    QStringList headers;
    if(!model){
        return headers;
    }
    for(int c = 0; c < model->columnCount(); ++c){
        headers << model->headerData(c, Qt::Horizontal).toString();
    }
    return headers;
}

QString ResultForm::formatCell(const QString &value, const ExportOptions &opts) const
{
    QString cell = value;
    if(cell.isEmpty() && !opts.nullRepresentation.isEmpty()){
        cell = opts.nullRepresentation;
    }
    if(opts.textQualifier.isEmpty()){
        return cell;
    }
    QString copy = cell;
    if(opts.escapeEmbedded){
        copy.replace(opts.textQualifier, opts.textQualifier + opts.textQualifier);
    }
    return opts.textQualifier + copy + opts.textQualifier;
}

bool ResultForm::writeDelimitedFile(const ExportOptions &opts)
{
    QFile file(opts.filePath);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)){
        updateSummaryLabel(tr("Cannot open %1").arg(QDir::toNativeSeparators(opts.filePath)));
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec(opts.encoding.toUtf8().constData());
    const QString newline = opts.lineEnding.compare(QStringLiteral("LF"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("\n")
            : QStringLiteral("\r\n");

    const QStringList headers = visibleHeaders();
    if(headers.isEmpty()){
        updateSummaryLabel(tr("No data to export."));
        return false;
    }

    QList<int> columns;
    if(opts.selectedColumns.isEmpty()){
        for(int i = 0; i < headers.size(); ++i){
            columns << i;
        }
    }else{
        for(const QString &name : opts.selectedColumns){
            const int index = headers.indexOf(name);
            if(index >= 0){
                columns << index;
            }
        }
    }
    if(columns.isEmpty()){
        updateSummaryLabel(tr("No columns selected."));
        return false;
    }

    if(opts.includeHeaders){
        for(int i = 0; i < columns.size(); ++i){
            stream << formatCell(headers.value(columns.at(i)), opts);
            if(i != columns.size() - 1){
                stream << opts.delimiter;
            }
        }
        stream << newline;
    }

    const QAbstractItemModel *viewModel = proxy
            ? static_cast<QAbstractItemModel *>(proxy)
            : static_cast<QAbstractItemModel *>(model);
    if(!viewModel){
        return false;
    }
    const int rowCount = viewModel->rowCount();
    int exportedRows = 0;
    for(int r = 0; r < rowCount; ++r){
        if(opts.rowLimit > 0 && exportedRows >= opts.rowLimit){
            break;
        }
        for(int ci = 0; ci < columns.size(); ++ci){
            const int column = columns.at(ci);
            const QString raw = itemTextForExport(viewModel->index(r, column));
            stream << formatCell(raw, opts);
            if(ci != columns.size() - 1){
                stream << opts.delimiter;
            }
        }
        stream << newline;
        ++exportedRows;
    }
    stream.flush();
    return true;
}

bool ResultForm::writeXlsxFile(const ExportOptions &opts)
{
    const QStringList headers = visibleHeaders();
    if(headers.isEmpty()){
        updateSummaryLabel(tr("No data to export."));
        return false;
    }
    QList<int> columns;
    if(opts.selectedColumns.isEmpty()){
        for(int i = 0; i < headers.size(); ++i){
            columns << i;
        }
    }else{
        for(const QString &name : opts.selectedColumns){
            const int index = headers.indexOf(name);
            if(index >= 0){
                columns << index;
            }
        }
    }
    if(columns.isEmpty()){
        updateSummaryLabel(tr("No columns selected."));
        return false;
    }
    const QAbstractItemModel *viewModel = proxy
            ? static_cast<QAbstractItemModel *>(proxy)
            : static_cast<QAbstractItemModel *>(model);
    if(!viewModel){
        return false;
    }

    QByteArray sheetXml;
    QXmlStreamWriter writer(&sheetXml);
    writer.setAutoFormatting(false);
    writer.writeStartDocument(QStringLiteral("1.0"));
    writer.writeStartElement(QStringLiteral("worksheet"));
    writer.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
                          QStringLiteral("r"));
    writer.writeStartElement(QStringLiteral("sheetData"));

    int rowIndex = 1;
    auto writeRow = [&](const QStringList &values, bool isHeader) {
        writer.writeStartElement(QStringLiteral("row"));
        writer.writeAttribute(QStringLiteral("r"), QString::number(rowIndex));
        for(int ci = 0; ci < values.size(); ++ci){
            QString cellValue = values.at(ci);
            if(cellValue.isEmpty() && !opts.nullRepresentation.isEmpty()){
                cellValue = opts.nullRepresentation;
            }
            if(cellValue.isEmpty()){
                continue;
            }
            const QString columnName = excelColumnName(columns.at(ci));
            const QString ref = columnName + QString::number(rowIndex);
            QString numericText;
            const bool numeric = !isHeader && isNumeric(cellValue, &numericText);
            writer.writeStartElement(QStringLiteral("c"));
            writer.writeAttribute(QStringLiteral("r"), ref);
            if(numeric){
                writer.writeAttribute(QStringLiteral("t"), QStringLiteral("n"));
                writer.writeTextElement(QStringLiteral("v"), numericText);
            }else{
                writer.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                writer.writeStartElement(QStringLiteral("is"));
                writer.writeTextElement(QStringLiteral("t"), cellValue);
                writer.writeEndElement(); // is
            }
            writer.writeEndElement(); // c
        }
        writer.writeEndElement(); // row
        ++rowIndex;
    };

    if(opts.includeHeaders){
        QStringList headerValues;
        for(int column : columns){
            headerValues << headers.value(column);
        }
        writeRow(headerValues, true);
    }

    const int rowCount = viewModel->rowCount();
    int exportedRows = 0;
    for(int r = 0; r < rowCount; ++r){
        if(opts.rowLimit > 0 && exportedRows >= opts.rowLimit){
            break;
        }
        QStringList rowValues;
        for(int column : columns){
            rowValues << itemTextForExport(viewModel->index(r, column));
        }
        writeRow(rowValues, false);
        ++exportedRows;
    }

    writer.writeEndElement(); // sheetData
    writer.writeEndElement(); // worksheet
    writer.writeEndDocument();

    static const QByteArray kContentTypesXml(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml" ContentType="application/xml"/>
  <Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
  <Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
  <Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>
</Types>)");
    static const QByteArray kRootRelsXml(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>)");
    static const QByteArray kWorkbookXml(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <sheets>
    <sheet name="Sheet1" sheetId="1" r:id="rId1"/>
  </sheets>
</workbook>)");
    static const QByteArray kWorkbookRelsXml(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
  <Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>)");
    static const QByteArray kStylesXml(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <fonts count="1">
    <font>
      <sz val="11"/>
      <color theme="1"/>
      <name val="Calibri"/>
      <family val="2"/>
    </font>
  </fonts>
  <fills count="1">
    <fill>
      <patternFill patternType="none"/>
    </fill>
  </fills>
  <borders count="1">
    <border>
      <left/>
      <right/>
      <top/>
      <bottom/>
      <diagonal/>
    </border>
  </borders>
  <cellStyleXfs count="1">
    <xf numFmtId="0" fontId="0" fillId="0" borderId="0"/>
  </cellStyleXfs>
  <cellXfs count="1">
    <xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>
  </cellXfs>
  <cellStyles count="1">
    <cellStyle name="Normal" xfId="0" builtinId="0"/>
  </cellStyles>
</styleSheet>)");

    QZipWriter zip(opts.filePath);
    if(zip.status() != QZipWriter::NoError){
        updateSummaryLabel(tr("Cannot open %1").arg(QDir::toNativeSeparators(opts.filePath)));
        return false;
    }
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    zip.addFile(QStringLiteral("[Content_Types].xml"), kContentTypesXml);
    zip.addFile(QStringLiteral("_rels/.rels"), kRootRelsXml);
    zip.addFile(QStringLiteral("xl/workbook.xml"), kWorkbookXml);
    zip.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), kWorkbookRelsXml);
    zip.addFile(QStringLiteral("xl/styles.xml"), kStylesXml);
    zip.addFile(QStringLiteral("xl/worksheets/sheet1.xml"), sheetXml);
    zip.close();
    if(zip.status() != QZipWriter::NoError){
        updateSummaryLabel(tr("Failed to write %1").arg(QDir::toNativeSeparators(opts.filePath)));
        return false;
    }
    return true;
}

void ResultForm::copySelectedCells()
{
    const QString serialized = selectedCellsAsTsv();
    if(serialized.isEmpty()){
        updateSummaryLabel(tr("No cells selected to copy."));
        return;
    }
    QGuiApplication::clipboard()->setText(serialized);
    const auto *selModel = tableView ? tableView->selectionModel() : nullptr;
    const QModelIndexList indexes = selModel ? selModel->selectedIndexes() : QModelIndexList();
    QSet<int> rowSet;
    for(const QModelIndex &idx : indexes){
        rowSet.insert(idx.row());
    }
    const int rowCount = rowSet.isEmpty() ? 0 : rowSet.size();
    updateSummaryLabel(tr("Copied %1 row(s) of cells.").arg(rowCount));
}

void ResultForm::copySelectedRows()
{
    const QString serialized = selectedRowsAsTsv();
    if(serialized.isEmpty()){
        updateSummaryLabel(tr("No rows selected to copy."));
        return;
    }
    QGuiApplication::clipboard()->setText(serialized);
    int rows = 0;
    if(tableView && tableView->selectionModel()){
        const auto selection = tableView->selectionModel()->selectedRows();
        rows = selection.isEmpty()
                ? (proxy ? proxy->rowCount() : 0)
                : selection.count();
    }
    updateSummaryLabel(tr("Copied %1 row(s).").arg(rows));
}

void ResultForm::exportData()
{
    if(!model || model->rowCount() == 0){
        updateSummaryLabel(tr("No data to export."));
        return;
    }
    ExportDialog dlg(this);
    dlg.setColumns(visibleHeaders());
    QString baseDir = lastExportDir;
    if(baseDir.isEmpty()){
        baseDir = QDir::homePath();
    }
    const QString defaultPath = QDir(baseDir).filePath(QStringLiteral("result.csv"));
    dlg.setInitialPath(defaultPath);
    dlg.setDefaultFormat(QStringLiteral("csv"));
    if(dlg.exec() != QDialog::Accepted){
        return;
    }
    ExportOptions opts = dlg.options();
    if(opts.filePath.isEmpty()){
        return;
    }
    QFileInfo info(opts.filePath);
    QString ext;
    if(opts.format == QStringLiteral("tsv")){
        ext = QStringLiteral("tsv");
    }else if(opts.format == QStringLiteral("xlsx")){
        ext = QStringLiteral("xlsx");
    }else{
        ext = QStringLiteral("csv");
    }
    if(info.suffix().isEmpty()){
        opts.filePath = opts.filePath + QLatin1Char('.') + ext;
    }
    bool ok = false;
    if(opts.format == QStringLiteral("xlsx")){
        ok = writeXlsxFile(opts);
    }else{
        ok = writeDelimitedFile(opts);
    }
    if(!ok){
        return;
    }
    lastExportDir = QFileInfo(opts.filePath).absolutePath();
    updateSummaryLabel(tr("Exported to %1").arg(QDir::toNativeSeparators(opts.filePath)));
}

void ResultForm::setFilterText(const QString &text)
{
    if(filterText == text){
        applyFilter();
        return;
    }
    filterText = text;
    applyFilter();
}

QString ResultForm::currentFilter() const
{
    return filterText;
}

QStringList ResultForm::headers() const
{
    return lastHeaders;
}

QStringList ResultForm::rowValues(int sourceRow) const
{
    QStringList values;
    if(!model || sourceRow < 0 || sourceRow >= model->rowCount()){
        return values;
    }
    for(int c = 0; c < model->columnCount(); ++c){
        QStandardItem *item = model->item(sourceRow, c);
        values << (item ? item->text() : QString());
    }
    return values;
}

QVector<bool> ResultForm::rowNullFlags(int sourceRow) const
{
    QVector<bool> flags;
    if(!model || sourceRow < 0 || sourceRow >= model->rowCount()){
        return flags;
    }
    for(int c = 0; c < model->columnCount(); ++c){
        QStandardItem *item = model->item(sourceRow, c);
        if(item){
            QVariant nullData = item->data(NullRole);
            flags << (nullData.isValid() && nullData.toBool());
        } else {
            flags << false;
        }
    }
    return flags;
}

QList<int> ResultForm::selectedSourceRows() const
{
    QList<int> rows;
    if(!tableView || !proxy){
        return rows;
    }
    const auto selection = tableView->selectionModel()
            ? tableView->selectionModel()->selectedRows()
            : QModelIndexList();
    for(const QModelIndex &proxyIndex : selection){
        QModelIndex sourceIndex = proxy->mapToSource(proxyIndex);
        if(sourceIndex.isValid()){
            rows << sourceIndex.row();
        }
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void ResultForm::sortByColumn(const QString &headerName, Qt::SortOrder order)
{
    sortByColumnIndex(columnIndexByName(headerName), order);
}

void ResultForm::sortByColumnIndex(int column, Qt::SortOrder order)
{
    if(!tableView || column < 0){
        return;
    }
    tableView->setSortingEnabled(true);
    tableView->sortByColumn(column, order);
    tableView->horizontalHeader()->setSortIndicator(column, order);
}

void ResultForm::applyFilter()
{
    if(mode == DisplayMode::Message){
        updateSummaryLabel(summaryBase);
        return;
    }
    if(proxy){
        proxy->setFilterNeedle(filterText);
    }
    rebuildSummaryWithFilter();
}

void ResultForm::rememberSummary(const QString &summary)
{
    summaryBase = summary;
}

void ResultForm::rememberHeaders(const QStringList &headers)
{
    lastHeaders = headers;
}

void ResultForm::autoFitColumns()
{
    if(!tableView || !model){
        return;
    }
    auto *header = tableView->horizontalHeader();
    const QAbstractItemModel *viewModel = tableView->model();
    if(!header || !viewModel){
        return;
    }
    const int columnCount = model->columnCount();
    const int rowCount = viewModel->rowCount();
    const QFontMetrics dataFm(tableView->font());
    const QFontMetrics headerFm(header->font());
    const int padding = 30;
    for(int c = 0; c < columnCount; ++c){
        QString headerText = model->headerData(c, Qt::Horizontal).toString();
        int maxWidth = headerFm.horizontalAdvance(headerText) + padding;
        for(int r = 0; r < rowCount; ++r){
            const QString text = viewModel->index(r, c).data(Qt::DisplayRole).toString();
            maxWidth = std::max(maxWidth, dataFm.horizontalAdvance(text) + padding);
        }
        maxWidth = qBound(60, maxWidth, 800);
        tableView->setColumnWidth(c, maxWidth);
    }
    header->setMinimumSectionSize(60);
    header->setStretchLastSection(false);
}

void ResultForm::rebuildSummaryWithFilter()
{
    QString summary = summaryBase;
    if(mode != DisplayMode::Message){
        const QString needle = filterText.trimmed();
        if(!needle.isEmpty() && proxy){
            summary += tr("  筛选“%1”匹配%2行").arg(needle).arg(proxy->rowCount());
        }
    }
    updateSummaryLabel(summary.trimmed());
}

int ResultForm::columnIndexByName(const QString &headerName) const
{
    if(!model){
        return -1;
    }
    for(int i = 0; i < model->columnCount(); ++i){
        const QString text = model->headerData(i, Qt::Horizontal).toString();
        if(text == headerName){
            return i;
        }
    }
    return headerName.isEmpty() ? 0 : -1;
}

void ResultForm::setToolbarVisible(bool visible)
{
    if(toolbarWidget){
        toolbarWidget->setVisible(visible);
    }
}

void ResultForm::setSelectionBehavior(QAbstractItemView::SelectionBehavior behavior)
{
    if(tableView){
        tableView->setSelectionBehavior(behavior);
    }
}

void ResultForm::setSelectionMode(QAbstractItemView::SelectionMode mode)
{
    if(tableView){
        tableView->setSelectionMode(mode);
    }
}

void ResultForm::setSortingEnabled(bool enabled)
{
    if(tableView){
        tableView->setSortingEnabled(enabled);
    }
}

int ResultForm::currentSourceRow() const
{
    if(!tableView || !proxy || !model){
        return -1;
    }
    QModelIndex idx = tableView->currentIndex();
    if(!idx.isValid()){
        const auto rows = tableView->selectionModel()
                ? tableView->selectionModel()->selectedRows()
                : QModelIndexList();
        if(!rows.isEmpty()){
            idx = rows.first();
        }
    }
    if(!idx.isValid()){
        return -1;
    }
    const QModelIndex source = proxy->mapToSource(idx);
    return source.isValid() ? source.row() : -1;
}

QItemSelectionModel *ResultForm::selectionModel() const
{
    return tableView ? tableView->selectionModel() : nullptr;
}
