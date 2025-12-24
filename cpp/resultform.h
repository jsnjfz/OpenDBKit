#ifndef RESULTFORM_H
#define RESULTFORM_H

#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>
#include <QStringList>
#include <QItemSelectionModel>
#include <QList>
#include <QVariant>

class QModelIndex;
class ResultFilterProxy;
class QStandardItemModel;
struct ExportOptions;

class ResultForm : public QWidget
{
    Q_OBJECT
public:
    struct ColumnInfo
    {
        QString name;
        QString originalName;  // Track original name for CHANGE COLUMN
        QString type;
        bool unsignedFlag = false;
        bool zeroFill = false;
        bool notNull = false;
        bool key = false;
        bool autoIncrement = false;
        bool generated = false;
        QString defaultExpression;
        QString comment;
    };

    explicit ResultForm(QWidget *parent = nullptr);

    void showRows(const QStringList &headers,
                  const QList<QStringList> &rows,
                  qint64 elapsedMs = -1,
                  const QString &note = QString(),
                  bool editable = false);
    void showRows(const QStringList &headers,
                  const QList<QVariantList> &rows,
                  qint64 elapsedMs = -1,
                  const QString &note = QString(),
                  bool editable = false,
                  const QVector<int> &columnTypes = QVector<int>());
    void showTableStructure(const QList<ColumnInfo> &columns, qint64 elapsedMs = -1);
    void showAffectRows(int affectedRows, qint64 elapsedMs);
    void showMessage(const QString &text);
    void reset();
    void setFilterText(const QString &text);
    QString currentFilter() const;
    QStringList headers() const;
    void sortByColumn(const QString &headerName, Qt::SortOrder order);
    void sortByColumnIndex(int column, Qt::SortOrder order);
    void setToolbarVisible(bool visible);
    void setSelectionBehavior(QAbstractItemView::SelectionBehavior behavior);
    void setSelectionMode(QAbstractItemView::SelectionMode mode);
    void setSortingEnabled(bool enabled);
    int currentSourceRow() const;
    QItemSelectionModel *selectionModel() const;
    QList<int> selectedSourceRows() const;
    QStringList rowValues(int sourceRow) const;
    QVector<bool> rowNullFlags(int sourceRow) const;
    QTableView *tableWidget() const { return tableView; }
    QStandardItemModel *sourceModel() const { return model; }

signals:
    void summaryChanged(const QString &summary);

private:
    enum class DisplayMode {
        Message,
        Data,
        Structure
    };

    QWidget *createToolbar();
    void updateSummaryLabel(const QString &text);
    QString selectedCellsAsTsv() const;
    QString selectedRowsAsTsv() const;
    QString itemTextForExport(const QModelIndex &index) const;
    QStringList visibleHeaders() const;
    bool writeDelimitedFile(const ExportOptions &opts);
    bool writeXlsxFile(const ExportOptions &opts);
    QString formatCell(const QString &value, const ExportOptions &opts) const;
    void copySelectedCells();
    void copySelectedRows();
    void applyFilter();
    void rememberSummary(const QString &summaryBase);
    void rememberHeaders(const QStringList &headers);
    void rebuildSummaryWithFilter();
    int columnIndexByName(const QString &headerName) const;
    void exportData();
    void autoFitColumns();

    QTableView *tableView = nullptr;
    QStandardItemModel *model = nullptr;
    ResultFilterProxy *proxy = nullptr;
    QLabel *messageLabel = nullptr;
    QLabel *summaryLabel = nullptr;
    QWidget *toolbarWidget = nullptr;
    QStackedLayout *stack = nullptr;
    QString lastExportDir;
    DisplayMode mode = DisplayMode::Message;
    QString filterText;
    QString summaryBase;
    QStringList lastHeaders;
};

#endif // RESULTFORM_H
