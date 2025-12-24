#include "conndialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QStringList commonCharsets()
{
    return {
        QStringLiteral("utf8mb4"),
        QStringLiteral("utf8"),
        QStringLiteral("latin1"),
        QStringLiteral("gbk"),
        QStringLiteral("ascii")
    };
}
}

ConnDialog::ConnDialog(QWidget *parent, const ConnectionInfo &info)
    : QDialog(parent)
    , m_originalInfo(info)
{
    setWindowTitle(tr("New Data Source - MySQL"));
    setModal(true);
    buildUi();
    if(!info.name.isEmpty()){
        fillFromInfo(info);
    }else{
        m_properties = ConnectionManager::defaultMysqlProperties();
        rebuildPropertyTable();
        updateSshFields();
        updateConnectionModeFields();
    }
}

void ConnDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    tabWidget = new QTabWidget(this);
    tabWidget->addTab(createGeneralTab(), tr("常规"));
    tabWidget->addTab(createAdvancedTab(), tr("高级"));
    tabWidget->addTab(createSshTab(), tr("SSH"));
    tabWidget->addTab(createPropertiesTab(), tr("属性"));
    tabWidget->addTab(createStartupTab(), tr("启动脚本"));
    tabWidget->addTab(createDriversTab(), tr("驱动"));
    layout->addWidget(tabWidget, 1);

    auto *buttonBox = new QDialogButtonBox(this);
    auto *testBtn = buttonBox->addButton(tr("测试"), QDialogButtonBox::ActionRole);
    auto *saveBtn = buttonBox->addButton(tr("确定"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(tr("取消"), QDialogButtonBox::RejectRole);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ConnDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &ConnDialog::onSaveConnection);
    connect(testBtn, &QPushButton::clicked, this, &ConnDialog::onTestConnection);
    layout->addWidget(buttonBox);
}

QWidget *ConnDialog::createGeneralTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    nameEdit = new QLineEdit(tab);
    form->addRow(tr("数据源名称:"), nameEdit);

    driverCombo = new QComboBox(tab);
    driverCombo->addItems(driverChoices());
    form->addRow(tr("驱动:"), driverCombo);

    auto *modeLayout = new QHBoxLayout;
    hostRadio = new QRadioButton(tr("Host"), tab);
    urlRadio = new QRadioButton(tr("URL"), tab);
    hostRadio->setChecked(true);
    modeLayout->addWidget(hostRadio);
    modeLayout->addSpacing(12);
    modeLayout->addWidget(urlRadio);
    form->addRow(tr("连接方式:"), modeLayout);

    hostEdit = new QLineEdit(tab);
    hostEdit->setPlaceholderText(QStringLiteral("localhost"));
    hostEdit->setText(QStringLiteral("localhost"));
    portSpin = new QSpinBox(tab);
    portSpin->setRange(1, 65535);
    portSpin->setValue(3306);
    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(hostEdit, 1);
    hostRow->addWidget(new QLabel(tr("Port:"), tab));
    hostRow->addWidget(portSpin);
    form->addRow(tr("Host:"), hostRow);

    urlEdit = new QLineEdit(tab);
    urlEdit->setPlaceholderText(QStringLiteral("jdbc:mysql://localhost:3306/db"));
    form->addRow(tr("URL:"), urlEdit);

    userEdit = new QLineEdit(tab);
    userEdit->setText(QStringLiteral("root"));
    form->addRow(tr("User Name:"), userEdit);

    auto *passwordRow = new QHBoxLayout;
    passwordEdit = new QLineEdit(tab);
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setText(QStringLiteral("123456"));
    savePasswordCheck = new QCheckBox(tr("保存密码"), tab);
    savePasswordCheck->setChecked(true);
    passwordRow->addWidget(passwordEdit, 1);
    passwordRow->addWidget(savePasswordCheck);
    form->addRow(tr("Password:"), passwordRow);

    databaseEdit = new QLineEdit(tab);
    form->addRow(tr("Database:"), databaseEdit);

    prodCheck = new QCheckBox(tr("生产环境"), tab);
    form->addRow(QString(), prodCheck);

    layout->addLayout(form);

    connect(hostRadio, &QRadioButton::toggled, this, &ConnDialog::updateConnectionModeFields);
    connect(urlRadio, &QRadioButton::toggled, this, &ConnDialog::updateConnectionModeFields);

    return tab;
}

QWidget *ConnDialog::createAdvancedTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QFormLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);

    autoSubmitCheck = new QCheckBox(tr("设置在查询编辑器中自动提交"), tab);
    autoSubmitCheck->setChecked(true);
    layout->addRow(QString(), autoSubmitCheck);

    charsetCombo = new QComboBox(tab);
    charsetCombo->addItems(commonCharsets());
    layout->addRow(tr("编码:"), charsetCombo);

    serverTimezoneEdit = new QLineEdit(tab);
    layout->addRow(tr("ServerTimezone:"), serverTimezoneEdit);

    auto *clientRow = new QHBoxLayout;
    localClientEdit = new QLineEdit(tab);
    auto *browseBtn = new QToolButton(tab);
    browseBtn->setText(tr("浏览"));
    clientRow->addWidget(localClientEdit, 1);
    clientRow->addWidget(browseBtn);
    layout->addRow(tr("Local Client:"), clientRow);

    connect(browseBtn, &QToolButton::clicked, this, &ConnDialog::browseLocalClient);

    return tab;
}

QWidget *ConnDialog::createSshTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QFormLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(8);

    sshEnableCheck = new QCheckBox(tr("使用 SSH tunnel"), tab);
    layout->addRow(QString(), sshEnableCheck);

    sshHostEdit = new QLineEdit(tab);
    layout->addRow(tr("Host:"), sshHostEdit);

    sshPortSpin = new QSpinBox(tab);
    sshPortSpin->setRange(1, 65535);
    sshPortSpin->setValue(22);
    layout->addRow(tr("Port:"), sshPortSpin);

    sshUserEdit = new QLineEdit(tab);
    layout->addRow(tr("User Name:"), sshUserEdit);

    sshPasswordEdit = new QLineEdit(tab);
    sshPasswordEdit->setEchoMode(QLineEdit::Password);
    sshSavePasswordCheck = new QCheckBox(tr("保存密码"), tab);
    auto *pwdRow = new QHBoxLayout;
    pwdRow->addWidget(sshPasswordEdit, 1);
    pwdRow->addWidget(sshSavePasswordCheck);
    layout->addRow(tr("Password:"), pwdRow);

    connect(sshEnableCheck, &QCheckBox::toggled, this, &ConnDialog::updateSshFields);

    return tab;
}

QWidget *ConnDialog::createPropertiesTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);

    auto *toolbar = new QHBoxLayout;
    propertyFilterEdit = new QLineEdit(tab);
    propertyFilterEdit->setPlaceholderText(tr("Regex Filter"));
    auto *addBtn = new QToolButton(tab);
    addBtn->setText(tr("+"));
    auto *removeBtn = new QToolButton(tab);
    removeBtn->setText(tr("-"));
    auto *resetBtn = new QPushButton(tab);
    resetBtn->setText(tr("恢复默认"));
    toolbar->addWidget(propertyFilterEdit, 1);
    toolbar->addWidget(addBtn);
    toolbar->addWidget(removeBtn);
    toolbar->addWidget(resetBtn);
    layout->addLayout(toolbar);

    propertyTable = new QTableWidget(0, 2, tab);
    propertyTable->setHorizontalHeaderLabels({tr("Name"), tr("Value")});
    propertyTable->horizontalHeader()->setStretchLastSection(true);
    propertyTable->verticalHeader()->setVisible(false);
    propertyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    propertyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    propertyTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    layout->addWidget(propertyTable, 1);

    connect(propertyFilterEdit, &QLineEdit::textChanged, this, &ConnDialog::applyPropertyFilter);
    connect(addBtn, &QToolButton::clicked, this, &ConnDialog::addPropertyRow);
    connect(removeBtn, &QToolButton::clicked, this, &ConnDialog::removeSelectedProperty);
    connect(resetBtn, &QPushButton::clicked, this, &ConnDialog::resetProperties);
    connect(propertyTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if(m_blockPropertySignals || !item){
            return;
        }
        const int row = item->row();
        const int col = item->column();
        if(row < 0){
            return;
        }
        while(m_properties.size() <= row){
            m_properties.append(ConnectionProperty());
        }
        if(col == 0){
            m_properties[row].name = item->text();
        }else if(col == 1){
            m_properties[row].value = item->text();
        }
    });

    return tab;
}

QWidget *ConnDialog::createStartupTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);
    startupScriptEdit = new QPlainTextEdit(tab);
    startupScriptEdit->setPlaceholderText(tr("-- 在连接建立后执行的脚本"));
    layout->addWidget(startupScriptEdit, 1);
    return tab;
}

QWidget *ConnDialog::createDriversTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(6);

    auto *toolbar = new QHBoxLayout;
    auto *downloadBtn = new QPushButton(tr("Download"), tab);
    auto *addBtn = new QPushButton(tr("Add JDBC Driver"), tab);
    auto *deleteBtn = new QPushButton(tr("Delete"), tab);
    auto *editBtn = new QPushButton(tr("Edit"), tab);
    auto *refreshBtn = new QPushButton(tr("刷新"), tab);
    auto *browseBtn = new QPushButton(tr("Browse"), tab);
    toolbar->addWidget(downloadBtn);
    toolbar->addWidget(addBtn);
    toolbar->addWidget(deleteBtn);
    toolbar->addWidget(editBtn);
    toolbar->addWidget(refreshBtn);
    toolbar->addWidget(browseBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    driverTable = new QTableWidget(0, 3, tab);
    driverTable->setHorizontalHeaderLabels({tr("Type"), tr("File"), tr("Driver Class")});
    driverTable->horizontalHeader()->setStretchLastSection(true);
    driverTable->verticalHeader()->setVisible(false);
    driverTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    driverTable->setSelectionMode(QAbstractItemView::SingleSelection);
    driverTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(driverTable, 1);

    connect(downloadBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("Driver Download"), tr("驱动下载功能将在后续版本提供。"));
    });
    connect(addBtn, &QPushButton::clicked, this, &ConnDialog::addDriverEntry);
    connect(deleteBtn, &QPushButton::clicked, this, &ConnDialog::deleteDriverEntry);
    connect(editBtn, &QPushButton::clicked, this, &ConnDialog::editDriverEntry);
    connect(refreshBtn, &QPushButton::clicked, this, &ConnDialog::refreshDriverEntries);
    connect(browseBtn, &QPushButton::clicked, this, &ConnDialog::browseDriverFile);

    return tab;
}

QStringList ConnDialog::driverChoices() const
{
    return {
        QStringLiteral("Default"),
        QStringLiteral("MariaDB"),
        QStringLiteral("MySQL JDBC")
    };
}

void ConnDialog::fillFromInfo(const ConnectionInfo &info)
{
    nameEdit->setText(info.name);
    hostEdit->setText(info.host);
    portSpin->setValue(info.port);
    userEdit->setText(info.user);
    passwordEdit->setText(info.password);
    savePasswordCheck->setChecked(info.savePassword);
    databaseEdit->setText(info.defaultDb);
    prodCheck->setChecked(info.production);
    driverCombo->setCurrentText(info.driver.isEmpty() ? QStringLiteral("Default") : info.driver);
    urlEdit->setText(info.url);
    if(info.useUrl){
        urlRadio->setChecked(true);
    }else{
        hostRadio->setChecked(true);
    }
    charsetCombo->setCurrentText(info.charset.isEmpty() ? QStringLiteral("utf8mb4") : info.charset);
    serverTimezoneEdit->setText(info.serverTimeZone);
    localClientEdit->setText(info.localClient);
    autoSubmitCheck->setChecked(info.autoSubmit);
    sshEnableCheck->setChecked(info.ssh.enabled);
    sshHostEdit->setText(info.ssh.host);
    sshPortSpin->setValue(info.ssh.port);
    sshUserEdit->setText(info.ssh.user);
    sshPasswordEdit->setText(info.ssh.password);
    sshSavePasswordCheck->setChecked(info.ssh.savePassword);
    startupScriptEdit->setPlainText(info.startupScript);

    m_properties = info.properties.isEmpty()
            ? ConnectionManager::defaultMysqlProperties()
            : info.properties;
    rebuildPropertyTable();

    m_drivers = info.jdbcDrivers;
    rebuildDriverTable();
    updateSshFields();
    updateConnectionModeFields();
}

ConnectionInfo ConnDialog::gatherInfo() const
{
    ConnectionInfo info = m_originalInfo;
    info.name = nameEdit->text().trimmed();
    info.driver = driverCombo->currentText();
    info.useUrl = urlRadio->isChecked();
    info.host = hostEdit->text().trimmed();
    info.port = quint16(portSpin->value());
    info.url = urlEdit->text().trimmed();
    info.user = userEdit->text().trimmed();
    info.password = passwordEdit->text();
    info.savePassword = savePasswordCheck->isChecked();
    if(!info.savePassword){
        info.password.clear();
    }
    info.defaultDb = databaseEdit->text().trimmed();
    info.production = prodCheck->isChecked();
    info.charset = charsetCombo->currentText();
    info.serverTimeZone = serverTimezoneEdit->text().trimmed();
    info.localClient = localClientEdit->text().trimmed();
    info.autoSubmit = autoSubmitCheck->isChecked();
    info.ssh.enabled = sshEnableCheck->isChecked();
    info.ssh.host = sshHostEdit->text().trimmed();
    info.ssh.port = quint16(sshPortSpin->value());
    info.ssh.user = sshUserEdit->text().trimmed();
    info.ssh.password = sshPasswordEdit->text();
    info.ssh.savePassword = sshSavePasswordCheck->isChecked();
    if(!info.ssh.savePassword){
        info.ssh.password.clear();
    }
    const auto propsFromTable = collectPropertiesFromTable();
    info.properties = propsFromTable;
    info.startupScript = startupScriptEdit->toPlainText();
    info.jdbcDrivers = m_drivers;
    if(info.useUrl){
        parseUrlIntoInfo(info);
    }
    return info;
}

void ConnDialog::onTestConnection()
{
    ConnectionInfo info = gatherInfo();
    if(info.name.isEmpty()){
        QMessageBox::warning(this, tr("Validation"), tr("请先填写连接名称。"));
        return;
    }
    QString error;
    if(ConnectionManager::instance()->testConnection(info, &error)){
        QMessageBox::information(this, tr("Connection Test"), tr("Connection successful."));
    }else{
        QMessageBox::warning(this, tr("Connection Test"), tr("Failed to connect: %1").arg(error));
    }
}

void ConnDialog::onSaveConnection()
{
    ConnectionInfo info = gatherInfo();
    if(info.name.isEmpty()){
        QMessageBox::warning(this, tr("Validation"), tr("Connection name is required."));
        return;
    }
    if(info.host.isEmpty()){
        info.host = QStringLiteral("localhost");
    }
    ConnectionManager::instance()->saveConnection(info);
    accept();
}

void ConnDialog::rebuildPropertyTable()
{
    if(!propertyTable){
        return;
    }
    m_blockPropertySignals = true;
    propertyTable->setRowCount(m_properties.size());
    for(int row = 0; row < m_properties.size(); ++row){
        const auto &prop = m_properties.at(row);
        auto *nameItem = new QTableWidgetItem(prop.name);
        auto *valueItem = new QTableWidgetItem(prop.value);
        propertyTable->setItem(row, 0, nameItem);
        propertyTable->setItem(row, 1, valueItem);
    }
    m_blockPropertySignals = false;
    applyPropertyFilter(propertyFilterEdit ? propertyFilterEdit->text() : QString());
}

void ConnDialog::rebuildDriverTable()
{
    if(!driverTable){
        return;
    }
    driverTable->setRowCount(m_drivers.size());
    for(int row = 0; row < m_drivers.size(); ++row){
        const auto &entry = m_drivers.at(row);
        driverTable->setItem(row, 0, new QTableWidgetItem(entry.type));
        driverTable->setItem(row, 1, new QTableWidgetItem(entry.file));
        driverTable->setItem(row, 2, new QTableWidgetItem(entry.driverClass));
    }
}

void ConnDialog::applyPropertyFilter(const QString &text)
{
    if(!propertyTable){
        return;
    }
    const bool useFilter = !text.trimmed().isEmpty();
    QRegularExpression regex;
    if(useFilter){
        regex = QRegularExpression(text, QRegularExpression::CaseInsensitiveOption);
    }
    for(int row = 0; row < propertyTable->rowCount(); ++row){
        bool match = !useFilter;
        if(useFilter){
            for(int col = 0; col < propertyTable->columnCount(); ++col){
                const auto *item = propertyTable->item(row, col);
                if(item && regex.match(item->text()).hasMatch()){
                    match = true;
                    break;
                }
            }
        }
        propertyTable->setRowHidden(row, !match);
    }
}

QList<ConnectionProperty> ConnDialog::collectPropertiesFromTable() const
{
    QList<ConnectionProperty> props;
    if(!propertyTable){
        return props;
    }
    for(int row = 0; row < propertyTable->rowCount(); ++row){
        const auto *nameItem = propertyTable->item(row, 0);
        const auto *valueItem = propertyTable->item(row, 1);
        const QString name = nameItem ? nameItem->text().trimmed() : QString();
        if(name.isEmpty()){
            continue;
        }
        ConnectionProperty prop;
        prop.name = name;
        prop.value = valueItem ? valueItem->text() : QString();
        props.append(prop);
    }
    return props;
}

void ConnDialog::addPropertyRow()
{
    m_properties.append(ConnectionProperty{QStringLiteral("property_%1").arg(m_properties.size() + 1), QString()});
    rebuildPropertyTable();
}

void ConnDialog::removeSelectedProperty()
{
    if(!propertyTable){
        return;
    }
    const int row = propertyTable->currentRow();
    if(row < 0 || row >= m_properties.size()){
        return;
    }
    m_properties.removeAt(row);
    rebuildPropertyTable();
}

void ConnDialog::resetProperties()
{
    m_properties = ConnectionManager::defaultMysqlProperties();
    rebuildPropertyTable();
}

void ConnDialog::updateSshFields()
{
    const bool enabled = sshEnableCheck && sshEnableCheck->isChecked();
    if(sshHostEdit){
        sshHostEdit->setEnabled(enabled);
    }
    if(sshPortSpin){
        sshPortSpin->setEnabled(enabled);
    }
    if(sshUserEdit){
        sshUserEdit->setEnabled(enabled);
    }
    if(sshPasswordEdit){
        sshPasswordEdit->setEnabled(enabled);
    }
    if(sshSavePasswordCheck){
        sshSavePasswordCheck->setEnabled(enabled);
    }
}

void ConnDialog::updateConnectionModeFields()
{
    const bool useHost = hostRadio && hostRadio->isChecked();
    if(hostEdit){
        hostEdit->setEnabled(useHost);
    }
    if(portSpin){
        portSpin->setEnabled(useHost);
    }
    if(urlEdit){
        urlEdit->setEnabled(!useHost);
    }
}

void ConnDialog::addDriverEntry()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("选择 JDBC 驱动"),
                                                    QString(),
                                                    tr("Jar Files (*.jar);;All Files (*.*)"));
    if(fileName.isEmpty()){
        return;
    }
    bool ok = false;
    const QString type = QInputDialog::getText(this, tr("Driver Type"),
                                               tr("请输入类型描述"),
                                               QLineEdit::Normal,
                                               QStringLiteral("MySQL"), &ok);
    if(!ok){
        return;
    }
    const QString driverClass = QInputDialog::getText(this,
                                                      tr("Driver Class"),
                                                      tr("请输入驱动类名"),
                                                      QLineEdit::Normal,
                                                      QStringLiteral("com.mysql.cj.jdbc.Driver"),
                                                      &ok);
    if(!ok){
        return;
    }
    m_drivers.append(JdbcDriverEntry{type, fileName, driverClass});
    rebuildDriverTable();
}

void ConnDialog::deleteDriverEntry()
{
    if(!driverTable){
        return;
    }
    const int row = driverTable->currentRow();
    if(row < 0 || row >= m_drivers.size()){
        return;
    }
    m_drivers.removeAt(row);
    rebuildDriverTable();
}

void ConnDialog::editDriverEntry()
{
    if(!driverTable){
        return;
    }
    const int row = driverTable->currentRow();
    if(row < 0 || row >= m_drivers.size()){
        return;
    }
    bool ok = false;
    auto entry = m_drivers.at(row);
    const QString type = QInputDialog::getText(this,
                                               tr("Driver Type"),
                                               tr("请输入类型描述"),
                                               QLineEdit::Normal,
                                               entry.type,
                                               &ok);
    if(!ok){
        return;
    }
    const QString driverClass = QInputDialog::getText(this,
                                                      tr("Driver Class"),
                                                      tr("请输入驱动类名"),
                                                      QLineEdit::Normal,
                                                      entry.driverClass,
                                                      &ok);
    if(!ok){
        return;
    }
    entry.type = type;
    entry.driverClass = driverClass;
    m_drivers[row] = entry;
    rebuildDriverTable();
}

void ConnDialog::browseDriverFile()
{
    if(!driverTable){
        return;
    }
    const int row = driverTable->currentRow();
    if(row < 0 || row >= m_drivers.size()){
        return;
    }
    const QString file = m_drivers.at(row).file;
    if(file.isEmpty()){
        return;
    }
    QFileInfo info(file);
    if(!info.exists()){
        QMessageBox::warning(this, tr("驱动"), tr("文件不存在: %1").arg(QDir::toNativeSeparators(file)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void ConnDialog::refreshDriverEntries()
{
    rebuildDriverTable();
}

void ConnDialog::browseLocalClient()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("选择 Local Client"),
                                                    QString(),
                                                    tr("Executables (*.exe);;All Files (*.*)"));
    if(fileName.isEmpty()){
        return;
    }
    localClientEdit->setText(fileName);
}

void ConnDialog::parseUrlIntoInfo(ConnectionInfo &info) const
{
    if(info.url.isEmpty()){
        return;
    }
    QString urlString = info.url.trimmed();
    QString normalized = urlString;
    if(normalized.startsWith(QStringLiteral("jdbc:"), Qt::CaseInsensitive)){
        normalized = normalized.mid(5);
    }
    if(!normalized.contains(QStringLiteral("://"))){
        normalized.prepend(QStringLiteral("mysql://"));
    }
    QUrl url(normalized);
    if(!url.host().isEmpty()){
        info.host = url.host();
    }
    if(url.port() > 0){
        info.port = quint16(url.port());
    }
    if(!url.path().isEmpty()){
        QString path = url.path();
        if(path.startsWith(QLatin1Char('/'))){
            path.remove(0, 1);
        }
        if(!path.isEmpty()){
            info.defaultDb = path;
        }
    }
}
