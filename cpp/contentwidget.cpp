#include "contentwidget.h"
#include "mainwindow.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QTabBar>

ContentWidget::ContentWidget(QWidget *parent) : QWidget(parent)
{
    splitter = new QSplitter(Qt::Horizontal, this);
    leftWidget = new LeftWidgetForm(splitter);
    tabWidget = new QTabWidget(splitter);
    tabWidget->setTabsClosable(true);
    tabWidget->setMovable(true);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    splitter->addWidget(leftWidget);
    splitter->addWidget(tabWidget);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 760});

    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &ContentWidget::closeTab);
    connect(tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        emit currentQueryChanged(qobject_cast<QueryForm *>(tabWidget->widget(index)));
    });
    tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabWidget->tabBar(), &QTabBar::customContextMenuRequested, this, &ContentWidget::showTabContextMenu);

    connect(leftWidget, &LeftWidgetForm::openQueryRequested, this, [this](const QString &conn, const QString &db) {
        addQueryTab(conn, db);
    });
    connect(leftWidget, &LeftWidgetForm::tableActionRequested, this,
            [this](const QString &conn, const QString &db, const QString &table, MyTreeWidget::TableAction action) {
        handleTableAction(conn, db, table, action);
    });

    addQueryTab();
}

QueryForm *ContentWidget::addQueryTab(const QString &connName, const QString &dbName)
{
    auto *form = new QueryForm(tabWidget);
    connectQueryForm(form);
    int index = tabWidget->addTab(form, tr("Untitled"));
    tabWidget->setCurrentIndex(index);
    form->setConnection(connName, dbName);
    return form;
}

bool ContentWidget::closeTab(int index)
{
    QWidget *widget = tabWidget->widget(index);
    if(!widget){
        return false;
    }
    auto *form = qobject_cast<QueryForm *>(widget);
    if(form && form->isModified()){
        auto ret = QMessageBox::warning(this, tr("Close Tab"),
                                        tr("The document has been modified. Close anyway?"),
                                        QMessageBox::Yes | QMessageBox::No);
        if(ret == QMessageBox::No){
            return false;
        }
    }
    tabWidget->removeTab(index);
    widget->deleteLater();
    return true;
}

QueryForm *ContentWidget::currentQueryForm() const
{
    return qobject_cast<QueryForm *>(tabWidget->currentWidget());
}

LeftWidgetForm *ContentWidget::leftPane() const
{
    return leftWidget;
}

QTabWidget *ContentWidget::rightTabs() const
{
    return tabWidget;
}

void ContentWidget::connectQueryForm(QueryForm *form)
{
    connect(form, &QueryForm::titleChanged, this, [this, form](const QString &title) {
        updateTabTitle(form, title, form->isModified());
    });
    connect(form, &QueryForm::modifiedStateChanged, this, [this, form](bool modified) {
        updateTabTitle(form, form->title(), modified);
        if(form == currentQueryForm()){
            emit currentQueryChanged(form);
        }
    });
    connect(form, &QueryForm::requestStatusMessage, this, [form](const QString &text, int timeout) {
        Q_UNUSED(form);
        MainWindow::instance()->setStatus(text, timeout);
    });
}

void ContentWidget::updateTabTitle(QWidget *widget, const QString &title, bool modified)
{
    for(int i = 0; i < tabWidget->count(); ++i){
        if(tabWidget->widget(i) == widget){
            QString text = title.isEmpty() ? tr("Untitled") : title;
            if(modified){
                text = QStringLiteral("* %1").arg(text);
            }
            tabWidget->setTabText(i, text);
            tabWidget->setTabToolTip(i, title);
            break;
        }
    }
}

void ContentWidget::handleTableAction(const QString &connName,
                                      const QString &dbName,
                                      const QString &tableName,
                                      MyTreeWidget::TableAction action)
{
    if(connName.isEmpty() || tableName.isEmpty()){
        MainWindow::instance()->setStatus(tr("请选择有效的表。"), 4000);
        return;
    }
    QueryForm *target = nullptr;
    for(int i = 0; i < tabWidget->count(); ++i){
        auto *candidate = qobject_cast<QueryForm *>(tabWidget->widget(i));
        if(candidate && candidate->isInspectTab()
                && candidate->inspectConnectionName() == connName
                && candidate->inspectTableName() == tableName){
            const QString normalizedDb = dbName.isEmpty() ? candidate->inspectDatabaseName() : dbName;
            if(candidate->inspectDatabaseName() == normalizedDb){
                target = candidate;
                break;
            }
        }
    }
    if(!target){
        target = new QueryForm(tabWidget, QueryForm::InspectMode);
        connectQueryForm(target);
        const QString tabTitle = QStringLiteral("%1@%2").arg(tableName, dbName);
        int index = tabWidget->addTab(target, tabTitle);
        tabWidget->setCurrentIndex(index);
    }else{
        tabWidget->setCurrentWidget(target);
    }
    if(!target){
        return;
    }
    QueryForm::TableAction queryAction = action == MyTreeWidget::ViewTableStructure
            ? QueryForm::ViewStructure
            : QueryForm::ViewData;
    target->openInspectTab(connName, dbName, tableName, queryAction);
}

void ContentWidget::showTabContextMenu(const QPoint &pos)
{
    int tabIndex = tabWidget->tabBar()->tabAt(pos);
    if(tabIndex < 0){
        return;
    }

    QMenu menu(this);
    QAction *closeAction = menu.addAction(tr("关闭"));
    QAction *closeOthersAction = menu.addAction(tr("关闭所有其他窗口"));
    QAction *closeRightAction = menu.addAction(tr("关闭所有右侧窗口"));
    QAction *closeAllAction = menu.addAction(tr("关闭全部窗口"));

    QAction *selected = menu.exec(tabWidget->tabBar()->mapToGlobal(pos));
    if(!selected){
        return;
    }

    if(selected == closeAction){
        closeTab(tabIndex);
    }else if(selected == closeOthersAction){
        for(int i = tabWidget->count() - 1; i >= 0; --i){
            if(i != tabIndex){
                closeTab(i);
            }
        }
    }else if(selected == closeRightAction){
        for(int i = tabWidget->count() - 1; i > tabIndex; --i){
            closeTab(i);
        }
    }else if(selected == closeAllAction){
        for(int i = tabWidget->count() - 1; i >= 0; --i){
            closeTab(i);
        }
    }
}
