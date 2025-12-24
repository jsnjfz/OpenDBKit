#ifndef CONTENTWIDGET_H
#define CONTENTWIDGET_H

#include "leftwidgetform.h"
#include "queryform.h"

#include <QSplitter>
#include <QTabWidget>
#include <QWidget>

class ContentWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ContentWidget(QWidget *parent = nullptr);

    QueryForm *addQueryTab(const QString &connName = QString(), const QString &dbName = QString());
    bool closeTab(int index);
    QueryForm *currentQueryForm() const;
    LeftWidgetForm *leftPane() const;
    QTabWidget *rightTabs() const;

signals:
    void currentQueryChanged(QueryForm *form);

private:
    void connectQueryForm(QueryForm *form);
    void updateTabTitle(QWidget *widget, const QString &title, bool modified);
    void handleTableAction(const QString &connName,
                           const QString &dbName,
                           const QString &tableName,
                           MyTreeWidget::TableAction action);
    void showTabContextMenu(const QPoint &pos);

    LeftWidgetForm *leftWidget = nullptr;
    QSplitter *splitter = nullptr;
    QTabWidget *tabWidget = nullptr;
};

#endif // CONTENTWIDGET_H
