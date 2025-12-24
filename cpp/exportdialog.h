#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>
#include <QStringList>

class QTabWidget;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QListWidget;

struct ExportOptions {
    QString filePath;
    QString format;        // csv/tsv/custom
    bool includeHeaders = true;
    int rowLimit = 0;      // 0 -> unlimited
    QStringList selectedColumns;
    QString delimiter = QStringLiteral(",");
    QString textQualifier = QStringLiteral("\"");
    bool escapeEmbedded = true;
    QString nullRepresentation;
    QString encoding = QStringLiteral("UTF-8");
    QString lineEnding = QStringLiteral("CRLF");
};

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportDialog(QWidget *parent = nullptr);

    void setColumns(const QStringList &columns);
    void setInitialPath(const QString &path);
    void setDefaultFormat(const QString &formatId);
    ExportOptions options() const;

private slots:
    void browseFile();
    void selectAllColumns();
    void clearColumnSelection();
    void moveColumnUp();
    void moveColumnDown();
    void onFormatChanged(int index);

private:
    void buildUi();
    QWidget *createGeneralPage();
    QWidget *createFieldsPage();
    QWidget *createAdvancedPage();
    void updateFieldsFromFormat();

    QTabWidget *tabs = nullptr;

    // General tab widgets
    QLineEdit *pathEdit = nullptr;
    QComboBox *formatCombo = nullptr;
    QCheckBox *headersCheck = nullptr;
    QSpinBox *rowLimitSpin = nullptr;

    // Fields tab
    QListWidget *fieldList = nullptr;

    // Advanced tab
    QLineEdit *delimiterEdit = nullptr;
    QLineEdit *qualifierEdit = nullptr;
    QCheckBox *escapeCheck = nullptr;
    QLineEdit *nullEdit = nullptr;
    QComboBox *encodingCombo = nullptr;
    QComboBox *lineEndingCombo = nullptr;
};

#endif // EXPORTDIALOG_H
