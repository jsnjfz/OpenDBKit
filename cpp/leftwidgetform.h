#ifndef LEFTWIDGETFORM_H
#define LEFTWIDGETFORM_H

#include "languagemanager.h"
#include "mytreewidget.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

class LeftWidgetForm : public QWidget
{
    Q_OBJECT
public:
    explicit LeftWidgetForm(QWidget *parent = nullptr);

    MyTreeWidget *tree() const;
    QLineEdit *filterEdit() const;

signals:
    void openQueryRequested(const QString &connName, const QString &dbName);
    void tableActionRequested(const QString &connName,
                              const QString &dbName,
                              const QString &tableName,
                              MyTreeWidget::TableAction action);

private slots:
    void applyFilter(const QString &text);
    void onNewConnectionClicked();
    void onRefreshClicked();
    void editConnection(const QString &connName);
    void deleteConnection(const QString &connName);
    void testConnection(const QString &connName);
    void openDataSync(const QString &connName,
                      const QString &dbName,
                      const QString &tableName);
    void openImportDialog(const QString &connName,
                          const QString &dbName,
                          const QString &tableName);

private:
    bool filterItem(QTreeWidgetItem *item, const QString &text);

    QLineEdit *filterLine = nullptr;
    MyTreeWidget *treeWidget = nullptr;
    QPushButton *newButton = nullptr;
    QPushButton *refreshButton = nullptr;
    QLabel *titleLabel = nullptr;

    void retranslateUi();
};

#endif // LEFTWIDGETFORM_H
