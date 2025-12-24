#include "leftwidgetform.h"
#include "conndialog.h"
#include "connectionmanager.h"
#include "datasyncdialog.h"
#include "importdialog.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

LeftWidgetForm::LeftWidgetForm(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("leftPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    titleLabel = new QLabel(this);
    titleLabel->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(titleLabel);

    auto *filterLayout = new QHBoxLayout;
    filterLayout->setSpacing(6);
    filterLine = new QLineEdit(this);
    filterLine->setClearButtonEnabled(true);
    newButton = new QPushButton(this);
    newButton->setIcon(QIcon(QStringLiteral(":/images/new.svg")));
    newButton->setToolTip(tr("New Connection"));
    refreshButton = new QPushButton(this);
    refreshButton->setIcon(QIcon(QStringLiteral(":/images/refresh.svg")));
    refreshButton->setToolTip(tr("Refresh"));
    filterLayout->addWidget(filterLine, 1);
    filterLayout->addWidget(newButton);
    filterLayout->addWidget(refreshButton);
    layout->addLayout(filterLayout);

    treeWidget = new MyTreeWidget(this);
    layout->addWidget(treeWidget, 1);

    connect(filterLine, &QLineEdit::textChanged, this, &LeftWidgetForm::applyFilter);
    connect(treeWidget, &MyTreeWidget::openQueryRequested, this, &LeftWidgetForm::openQueryRequested);
    connect(treeWidget, &MyTreeWidget::tableActionRequested, this, &LeftWidgetForm::tableActionRequested);
    connect(treeWidget, &MyTreeWidget::connectionEditRequested, this, &LeftWidgetForm::editConnection);
    connect(treeWidget, &MyTreeWidget::connectionDeleteRequested, this, &LeftWidgetForm::deleteConnection);
    connect(treeWidget, &MyTreeWidget::connectionTestRequested, this, &LeftWidgetForm::testConnection);
    connect(treeWidget, &MyTreeWidget::dataSyncRequested, this, &LeftWidgetForm::openDataSync);
    connect(treeWidget, &MyTreeWidget::dataImportRequested, this, &LeftWidgetForm::openImportDialog);
    connect(newButton, &QPushButton::clicked, this, &LeftWidgetForm::onNewConnectionClicked);
    connect(refreshButton, &QPushButton::clicked, this, &LeftWidgetForm::onRefreshClicked);
    connect(LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });
    retranslateUi();
}

void LeftWidgetForm::retranslateUi()
{
    if(titleLabel){
        titleLabel->setText(trLang(QStringLiteral("连接列表"), QStringLiteral("Connections")));
    }
    if(filterLine){
        filterLine->setPlaceholderText(trLang(QStringLiteral("正则过滤"), QStringLiteral("Regex Filter")));
    }
    if(newButton){
        newButton->setText(trLang(QStringLiteral("新建"), QStringLiteral("New")));
    }
    if(refreshButton){
        refreshButton->setText(trLang(QStringLiteral("刷新"), QStringLiteral("Refresh")));
    }
}

MyTreeWidget *LeftWidgetForm::tree() const
{
    return treeWidget;
}

QLineEdit *LeftWidgetForm::filterEdit() const
{
    return filterLine;
}

void LeftWidgetForm::applyFilter(const QString &text)
{
    for(int i = 0; i < treeWidget->topLevelItemCount(); ++i){
        auto *item = treeWidget->topLevelItem(i);
        filterItem(item, text);
    }
}

bool LeftWidgetForm::filterItem(QTreeWidgetItem *item, const QString &text)
{
    bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
    for(int i = 0; i < item->childCount(); ++i){
        match = filterItem(item->child(i), text) || match;
    }
    item->setHidden(!match);
    return match;
}

void LeftWidgetForm::onNewConnectionClicked()
{
    ConnDialog dlg(this);
    dlg.exec();
}

void LeftWidgetForm::onRefreshClicked()
{
    treeWidget->refreshConnections();
}

void LeftWidgetForm::editConnection(const QString &connName)
{
    ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    if(info.name.isEmpty()){
        QMessageBox::warning(this,
                             trLang(QStringLiteral("编辑连接"), QStringLiteral("Edit Connection")),
                             trLang(QStringLiteral("连接 %1 不存在，可能已被删除。"),
                                    QStringLiteral("Connection %1 does not exist, it may have been removed.")).arg(connName));
        treeWidget->refreshConnections();
        return;
    }
    ConnDialog dlg(this, info);
    if(dlg.exec() == QDialog::Accepted){
        treeWidget->refreshConnections();
    }
}

void LeftWidgetForm::deleteConnection(const QString &connName)
{
    ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    if(info.name.isEmpty()){
        QMessageBox::information(this,
                                 trLang(QStringLiteral("删除连接"), QStringLiteral("Delete Connection")),
                                 trLang(QStringLiteral("连接 %1 已不存在。"), QStringLiteral("Connection %1 no longer exists.")).arg(connName));
        treeWidget->refreshConnections();
        return;
    }
    const auto reply = QMessageBox::question(this,
                                             trLang(QStringLiteral("删除连接"), QStringLiteral("Delete Connection")),
                                             trLang(QStringLiteral("确定要删除连接 \"%1\" 吗？"),
                                                    QStringLiteral("Are you sure you want to delete connection \"%1\"?")).arg(connName),
                                             QMessageBox::Yes | QMessageBox::No,
                                             QMessageBox::No);
    if(reply != QMessageBox::Yes){
        return;
    }
    if(ConnectionManager::instance()->removeConnection(connName)){
        treeWidget->refreshConnections();
    }else{
        QMessageBox::warning(this,
                             trLang(QStringLiteral("删除连接"), QStringLiteral("Delete Connection")),
                             trLang(QStringLiteral("无法删除连接 %1。"),
                                    QStringLiteral("Unable to delete connection %1.")).arg(connName));
    }
}

void LeftWidgetForm::testConnection(const QString &connName)
{
    ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    if(info.name.isEmpty()){
        QMessageBox::information(this,
                                 trLang(QStringLiteral("测试连接"), QStringLiteral("Test Connection")),
                                 trLang(QStringLiteral("连接 %1 已不存在。"), QStringLiteral("Connection %1 no longer exists.")).arg(connName));
        treeWidget->refreshConnections();
        return;
    }
    QString error;
    if(ConnectionManager::instance()->testConnection(info, &error)){
        QMessageBox::information(this,
                                 trLang(QStringLiteral("测试连接"), QStringLiteral("Test Connection")),
                                 trLang(QStringLiteral("连接 %1 成功。"), QStringLiteral("Connection %1 succeeded.")).arg(connName));
    }else{
        QMessageBox::warning(this,
                             trLang(QStringLiteral("测试连接"), QStringLiteral("Test Connection")),
                             trLang(QStringLiteral("连接 %1 失败：%2"),
                                    QStringLiteral("Connection %1 failed: %2")).arg(connName, error));
    }
}

void LeftWidgetForm::openDataSync(const QString &connName,
                                  const QString &dbName,
                                  const QString &tableName)
{
    DataSyncDialog dlg(this);
    if(!connName.isEmpty()){
        dlg.setSourceContext(connName, dbName);
        dlg.setTargetContext(connName, dbName);
    }
    if(!tableName.isEmpty()){
        dlg.setInitialTableHint(tableName);
    }
    dlg.exec();
}

void LeftWidgetForm::openImportDialog(const QString &connName,
                                      const QString &dbName,
                                      const QString &tableName)
{
    ConnectionInfo info = ConnectionManager::instance()->connection(connName);
    if(info.name.isEmpty()){
        QMessageBox::information(this,
                                 tr("导入数据"),
                                 tr("连接 %1 已不存在。").arg(connName));
        treeWidget->refreshConnections();
        return;
    }
    ImportDialog dlg(info, dbName, tableName, this);
    dlg.exec();
}
