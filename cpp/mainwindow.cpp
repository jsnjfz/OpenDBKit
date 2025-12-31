#include "mainwindow.h"
#include "contentwidget.h"
#include "conndialog.h"
#include "datasyncdialog.h"
#include "myedit.h"
#include "queryform.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QtGlobal>

namespace {
constexpr char kSettingsGeometry[] = "MainWindow/geometry";
constexpr char kSettingsState[] = "MainWindow/state";
constexpr char kSettingsLastDir[] = "MainWindow/lastDir";
constexpr char kSettingsAppearance[] = "Appearance";
constexpr char kSettingsFontFamily[] = "fontFamily";
constexpr char kSettingsFontSize[] = "fontSize";
constexpr char kSettingsLanguage[] = "language";
}

MainWindow::MainWindow()
{
    loadLanguageSetting();

    QFont baseFont = font();
    this->defaultFontFamily = baseFont.family();
    this->defaultFontSize = 11;

    loadQSS();

    content = new ContentWidget(this);
    setCentralWidget(content);
    loadAppearanceSettings();

    createActions();
    auto *langMgr = LanguageManager::instance();
    updateLanguageSelection(langMgr->language());
    connect(langMgr, &LanguageManager::languageChanged, this, [this](LanguageManager::Language lang) {
        updateActionTexts();
        updateLanguageSelection(lang);
        persistLanguageSetting(lang);
    });
    createStatusBar();
    readSettings();

    connect(content, &ContentWidget::currentQueryChanged, this, [this](QueryForm *form) {
        bool modified = form && form->isModified();
        saveAct->setEnabled(modified);
        if(form){
            setCurrentTitle(form->title());
        }else{
            setCurrentTitle(QString{});
        }
    });

    emit inited();
}

MainWindow *MainWindow::instance()
{
    static MainWindow *ins = new MainWindow();
    return ins;
}

ContentWidget *MainWindow::getContent() const
{
    return content;
}

QueryForm *MainWindow::loadFile(const QString &fileName, const QByteArray &codec)
{
    QFileInfo info(fileName);
    if(!info.exists()){
        QMessageBox::warning(this,
                             trLang(QStringLiteral("打开文件"), QStringLiteral("Open File")),
                             trLang(QStringLiteral("文件 %1 不存在。"), QStringLiteral("File %1 not found.")).arg(fileName));
        return nullptr;
    }
    QueryForm *form = content->addQueryTab();
    if(!form->loadFromFile(fileName, codec)){
        QMessageBox::warning(this,
                             trLang(QStringLiteral("打开文件"), QStringLiteral("Open File")),
                             trLang(QStringLiteral("无法打开 %1"), QStringLiteral("Unable to open %1")).arg(fileName));
        return nullptr;
    }
    return form;
}

void MainWindow::setCurrentTitle(const QString &fileName)
{
    if(fileName.isEmpty()){
        setWindowTitle(QStringLiteral("OpenDBKit"));
    }else{
        setWindowTitle(QStringLiteral("OpenDBKit - %1").arg(fileName));
    }
}

bool MainWindow::save()
{
    QueryForm *form = content->currentQueryForm();
    if(!form){
        return false;
    }
    if(form->filePath().isEmpty()){
        return saveAs();
    }
    return form->saveToFile(form->filePath(), form->codecName().toUtf8());
}

void MainWindow::setStatus(const QString &text, int timeout)
{
    Q_UNUSED(timeout)
    resultInfoLabel.setText(text);
}

void MainWindow::clearStatusLabels()
{
    statusLabel.clear();
    statusLabel.hide();
    sqlLabel.clear();
    sqlLabel.hide();
    codecLabel.clear();
    codecLabel.hide();
    resultInfoLabel.clear();
}

void MainWindow::loadQSS()
{
    QFile file(QStringLiteral(":/qss/mainwindow.qss"));
    if(file.open(QIODevice::ReadOnly)){
        QString base = QString::fromUtf8(file.readAll());
        setStyleSheet(base);
    }
}

QRect MainWindow::getGuiFontRect(const QString &text, const QWidget *wid) const
{
    if(wid){
        return wid->fontMetrics().boundingRect(text);
    }
    return fontMetrics().boundingRect(text);
}

QRect MainWindow::getCharGuiFontRect() const
{
    return getGuiFontRect(QStringLiteral("M"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(maybeSave()){
        writeSettings();
        event->accept();
    }else{
        event->ignore();
    }
}

void MainWindow::newFile()
{
    content->addQueryTab();
}

void MainWindow::open()
{
    QSettings settings;
    const QString lastDir = settings.value(kSettingsLastDir, QDir::homePath()).toString();
    const QString file = QFileDialog::getOpenFileName(this,
                                                      trLang(QStringLiteral("打开文件"), QStringLiteral("Open File")),
                                                      lastDir,
                                                      trLang(QStringLiteral("SQL 文件 (*.sql *.txt);;所有文件 (*.*)"),
                                                             QStringLiteral("SQL Files (*.sql *.txt);;All Files (*.*)")));
    if(file.isEmpty()){
        return;
    }
    settings.setValue(kSettingsLastDir, QFileInfo(file).absolutePath());
    loadFile(file);
}

bool MainWindow::saveAs()
{
    QueryForm *form = content->currentQueryForm();
    if(!form){
        return false;
    }
    QSettings settings;
    const QString lastDir = settings.value(kSettingsLastDir, QDir::homePath()).toString();
    const QString file = QFileDialog::getSaveFileName(this,
                                                      trLang(QStringLiteral("保存文件"), QStringLiteral("Save File")),
                                                      lastDir,
                                                      trLang(QStringLiteral("SQL 文件 (*.sql);;所有文件 (*.*)"),
                                                             QStringLiteral("SQL Files (*.sql);;All Files (*.*)")));
    if(file.isEmpty()){
        return false;
    }
    settings.setValue(kSettingsLastDir, QFileInfo(file).absolutePath());
    if(form->saveToFile(file)){
        setCurrentTitle(file);
        return true;
    }
    return false;
}

void MainWindow::about()
{
    QMessageBox::about(this,
                       trLang(QStringLiteral("关于 OpenDBKit"), QStringLiteral("About OpenDBKit")),
                       trLang(QStringLiteral("<b>OpenDBKit</b> v%1<br/><br/>"
                                             "开源跨平台数据库管理工具<br/><br/>"
                                             "当前支持: MySQL<br/><br/>"
                                             "功能: SQL编辑器 • 数据浏览 • 表设计器<br/>"
                                             "导入导出 • 结构同步<br/><br/>"
                                             "<a href='https://github.com/jsnjfz/OpenDBKit'>GitHub</a> | GPLv3 License<br/>"
                                             "© 2025 OpenDBKit Contributors"),
                              QStringLiteral("<b>OpenDBKit</b> v%1<br/><br/>"
                                             "Open Source Cross-Platform Database Manager<br/><br/>"
                                             "Currently supports: MySQL<br/><br/>"
                                             "Features: SQL Editor • Data Browser • Table Designer<br/>"
                                             "Import/Export • Structure Sync<br/><br/>"
                                             "<a href='https://github.com/jsnjfz/OpenDBKit'>GitHub</a> | GPLv3 License<br/>"
                                             "© 2025 OpenDBKit Contributors")).arg(VERSION_STR));
}

#ifndef QT_NO_SESSIONMANAGER
void MainWindow::commitData(QSessionManager &manager)
{
    if(manager.allowsInteraction()){
        if(!maybeSave()){
            manager.cancel();
        }
    }else{
        save();
    }
}
#endif

void MainWindow::createActions()
{
    fileToolBar = addToolBar(QString());
    fileToolBar->setMovable(false);
    fileToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    fileToolBar->setIconSize(QSize(20, 20));

    fileMenu = menuBar()->addMenu(QString());
    connMenu = menuBar()->addMenu(QString());
    viewMenu = menuBar()->addMenu(QString());
    toolsMenu = menuBar()->addMenu(QString());
    helpMenu = menuBar()->addMenu(QString());

    newAct = new QAction(QIcon(QStringLiteral(":/images/new.svg")), QString(), this);
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MainWindow::newFile);
    fileMenu->addAction(newAct);

    openAct = new QAction(QIcon(QStringLiteral(":/images/open.svg")), QString(), this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::open);
    fileMenu->addAction(openAct);
    fileToolBar->addAction(openAct);

    saveAct = new QAction(QIcon(QStringLiteral(":/images/save.svg")), QString(), this);
    saveAct->setShortcut(QKeySequence::Save);
    saveAct->setEnabled(false);
    connect(saveAct, &QAction::triggered, this, &MainWindow::save);
    fileMenu->addAction(saveAct);
    fileToolBar->addAction(saveAct);

    fileToolBar->addSeparator();

    syncToolAct = new QAction(QIcon(QStringLiteral(":/images/sync.svg")), QString(), this);
    connect(syncToolAct, &QAction::triggered, this, [this]() {
        DataSyncDialog dlg(this);
        dlg.exec();
    });
    fileToolBar->addAction(syncToolAct);

    saveAsAct = new QAction(this);
    connect(saveAsAct, &QAction::triggered, this, &MainWindow::saveAs);
    fileMenu->addAction(saveAsAct);

    fileMenu->addSeparator();

    exitAct = new QAction(this);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAct);

    newConnAct = new QAction(QIcon(QStringLiteral(":/images/new.svg")), QString(), this);
    connect(newConnAct, &QAction::triggered, this, [this]() {
        ConnDialog dlg(this);
        dlg.exec();
    });
    connMenu->addAction(newConnAct);
    fileToolBar->addAction(newConnAct);

    fontAct = new QAction(this);
    connect(fontAct, &QAction::triggered, this, &MainWindow::adjustInterfaceFont);
    viewMenu->addAction(fontAct);

    setupLanguageMenu();

    dataSyncAct = new QAction(this);
    connect(dataSyncAct, &QAction::triggered, this, [this]() {
        DataSyncDialog dlg(this);
        dlg.exec();
    });
    toolsMenu->addAction(dataSyncAct);

    aboutAct = new QAction(this);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::about);
    helpMenu->addAction(aboutAct);

    updateActionTexts();
}

void MainWindow::setupLanguageMenu()
{
    if(!viewMenu){
        return;
    }
    if(languageMenu){
        viewMenu->removeAction(languageMenu->menuAction());
        delete languageMenu;
        languageMenu = nullptr;
    }
    languageMenu = viewMenu->addMenu(QString());

    auto *group = new QActionGroup(this);
    languageChineseAct = new QAction(this);
    languageChineseAct->setCheckable(true);
    languageEnglishAct = new QAction(this);
    languageEnglishAct->setCheckable(true);
    group->addAction(languageChineseAct);
    group->addAction(languageEnglishAct);
    group->setExclusive(true);

    languageMenu->addAction(languageChineseAct);
    languageMenu->addAction(languageEnglishAct);

    connect(languageChineseAct, &QAction::triggered, this, []() {
        LanguageManager::instance()->setLanguage(LanguageManager::Language::Chinese);
    });
    connect(languageEnglishAct, &QAction::triggered, this, []() {
        LanguageManager::instance()->setLanguage(LanguageManager::Language::English);
    });
}

void MainWindow::updateActionTexts()
{
    if(fileToolBar){
        fileToolBar->setWindowTitle(trLang(QStringLiteral("文件工具栏"), QStringLiteral("File")));
    }
    if(fileMenu){
        fileMenu->setTitle(trLang(QStringLiteral("文件(&F)"), QStringLiteral("&File")));
    }
    if(connMenu){
        connMenu->setTitle(trLang(QStringLiteral("连接(&C)"), QStringLiteral("&Connections")));
    }
    if(viewMenu){
        viewMenu->setTitle(trLang(QStringLiteral("视图(&V)"), QStringLiteral("&View")));
    }
    if(languageMenu){
        languageMenu->setTitle(trLang(QStringLiteral("语言"), QStringLiteral("Language")));
    }
    if(toolsMenu){
        toolsMenu->setTitle(trLang(QStringLiteral("工具(&T)"), QStringLiteral("&Tools")));
    }
    if(helpMenu){
        helpMenu->setTitle(trLang(QStringLiteral("帮助(&H)"), QStringLiteral("&Help")));
    }

    if(newAct){
        newAct->setText(trLang(QStringLiteral("新建查询"), QStringLiteral("New Query")));
        newAct->setToolTip(trLang(QStringLiteral("新建查询窗口 (Ctrl+N)"), QStringLiteral("New Query Window (Ctrl+N)")));
    }
    if(openAct){
        openAct->setText(trLang(QStringLiteral("打开..."), QStringLiteral("Open...")));
        openAct->setToolTip(trLang(QStringLiteral("打开SQL文件 (Ctrl+O)"), QStringLiteral("Open SQL File (Ctrl+O)")));
    }
    if(saveAct){
        saveAct->setText(trLang(QStringLiteral("保存"), QStringLiteral("Save")));
        saveAct->setToolTip(trLang(QStringLiteral("保存文件 (Ctrl+S)"), QStringLiteral("Save File (Ctrl+S)")));
    }
    if(saveAsAct){
        saveAsAct->setText(trLang(QStringLiteral("另存为..."), QStringLiteral("Save As...")));
    }
    if(exitAct){
        exitAct->setText(trLang(QStringLiteral("退出"), QStringLiteral("Exit")));
    }
    if(newConnAct){
        newConnAct->setText(trLang(QStringLiteral("新建 MySQL 连接"), QStringLiteral("New MySQL Connection")));
        newConnAct->setToolTip(trLang(QStringLiteral("新建 MySQL 连接"), QStringLiteral("New MySQL Connection")));
    }
    if(fontAct){
        fontAct->setText(trLang(QStringLiteral("界面字体..."), QStringLiteral("Interface Font...")));
    }
    if(languageChineseAct){
        languageChineseAct->setText(trLang(QStringLiteral("中文"), QStringLiteral("Chinese")));
    }
    if(languageEnglishAct){
        languageEnglishAct->setText(trLang(QStringLiteral("英文"), QStringLiteral("English")));
    }
    if(dataSyncAct){
        dataSyncAct->setText(trLang(QStringLiteral("数据同步..."), QStringLiteral("Data Synchronization...")));
    }
    if(syncToolAct){
        syncToolAct->setText(trLang(QStringLiteral("数据同步"), QStringLiteral("Data Sync")));
        syncToolAct->setToolTip(trLang(QStringLiteral("数据同步工具"), QStringLiteral("Data Synchronization Tool")));
    }
    if(aboutAct){
        aboutAct->setText(trLang(QStringLiteral("关于"), QStringLiteral("About")));
    }
}

void MainWindow::updateLanguageSelection(LanguageManager::Language lang)
{
    if(languageChineseAct){
        languageChineseAct->setChecked(lang == LanguageManager::Language::Chinese);
    }
    if(languageEnglishAct){
        languageEnglishAct->setChecked(lang == LanguageManager::Language::English);
    }
}

void MainWindow::persistLanguageSetting(LanguageManager::Language lang) const
{
    QSettings settings;
    settings.beginGroup(QString::fromUtf8(kSettingsAppearance));
    const QString code = lang == LanguageManager::Language::Chinese
            ? QStringLiteral("zh")
            : QStringLiteral("en");
    settings.setValue(QString::fromUtf8(kSettingsLanguage), code);
    settings.endGroup();
}

void MainWindow::loadLanguageSetting()
{
    QSettings settings;
    settings.beginGroup(QString::fromUtf8(kSettingsAppearance));
    const QString code = settings.value(QString::fromUtf8(kSettingsLanguage), defaultLanguage).toString();
    settings.endGroup();
    const auto lang = LanguageManager::languageFromCode(code);
    LanguageManager::instance()->initialize(lang);
}

void MainWindow::createStatusBar()
{
    statusBar()->addPermanentWidget(&statusLabel);
    statusBar()->addPermanentWidget(&sqlLabel);
    statusBar()->addPermanentWidget(&codecLabel);
    statusBar()->addPermanentWidget(&resultInfoLabel);
    resultInfoLabel.setMinimumWidth(250);
    resultInfoLabel.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    resultInfoLabel.setStyleSheet(QStringLiteral("QLabel { font-size: 12pt; padding: 2px 12px; }"));
    clearStatusLabels();
}

void MainWindow::readSettings()
{
    QSettings settings;
    const QByteArray geometry = settings.value(kSettingsGeometry).toByteArray();
    if(geometry.isEmpty()){
        const QRect screenRect = QGuiApplication::primaryScreen()->availableGeometry();
        const int w = screenRect.width() * 9 / 10;
        const int h = screenRect.height() * 9 / 10;
        const int x = (screenRect.width() - w) / 2 + screenRect.x();
        const int y = (screenRect.height() - h) / 2 + screenRect.y();
        setGeometry(x, y, w, h);
    }else{
        restoreGeometry(geometry);
    }
    restoreState(settings.value(kSettingsState).toByteArray());
}

void MainWindow::writeSettings()
{
    QSettings settings;
    settings.setValue(kSettingsGeometry, saveGeometry());
    settings.setValue(kSettingsState, saveState());
}

bool MainWindow::maybeSave()
{
    QueryForm *form = content->currentQueryForm();
    if(!form || !form->isModified()){
        return true;
    }
    auto ret = QMessageBox::warning(this,
                                    trLang(QStringLiteral("OpenDBKit"), QStringLiteral("OpenDBKit")),
                                    trLang(QStringLiteral("文档已修改。\n是否保存更改？"),
                                           QStringLiteral("The document has been modified.\n"
                                                          "Do you want to save your changes?")),
                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if(ret == QMessageBox::Save){
        return save();
    }
    if(ret == QMessageBox::Cancel){
        return false;
    }
    return true;
}

bool MainWindow::saveFile(const QString &fileName, const QByteArray &codec)
{
    QueryForm *form = content->currentQueryForm();
    if(!form){
        return false;
    }
    if(form->saveToFile(fileName, codec)){
        setCurrentTitle(fileName);
        return true;
    }
    return false;
}

QString MainWindow::strippedName(const QString &fullFileName) const
{
    return QFileInfo(fullFileName).fileName();
}

void MainWindow::loadAppearanceSettings()
{
    QSettings settings;
    settings.beginGroup(QString::fromUtf8(kSettingsAppearance));
    const QString family = settings.value(QString::fromUtf8(kSettingsFontFamily), defaultFontFamily).toString();
    const int size = settings.value(QString::fromUtf8(kSettingsFontSize), defaultFontSize).toInt();
    settings.endGroup();
    applyAppFont(family, size);
}

void MainWindow::applyAppFont(const QString &family, int size)
{
    const int candidate = size > 0 ? size : defaultFontSize;
    defaultFontFamily = family;
    defaultFontSize = qBound(9, candidate, 28);

    QFont newFont = QApplication::font();
    if(!defaultFontFamily.isEmpty()){
        newFont.setFamily(defaultFontFamily);
    }
    newFont.setPointSize(defaultFontSize);
    QApplication::setFont(newFont);
    setFont(newFont);

    const auto widgets = findChildren<QWidget *>();
    for(QWidget *widget : widgets){
        if(widget){
            widget->setFont(newFont);
        }
    }
}

void MainWindow::persistAppearanceSettings() const
{
    QSettings settings;
    settings.beginGroup(QString::fromUtf8(kSettingsAppearance));
    settings.setValue(QString::fromUtf8(kSettingsFontFamily), defaultFontFamily);
    settings.setValue(QString::fromUtf8(kSettingsFontSize), defaultFontSize);
    settings.endGroup();
}

void MainWindow::adjustInterfaceFont()
{
    QFont previewFont;
    if(!defaultFontFamily.isEmpty()){
        previewFont.setFamily(defaultFontFamily);
    }else{
        previewFont = font();
    }
    previewFont.setPointSize(defaultFontSize);

    bool accepted = false;
    const QFont next = QFontDialog::getFont(&accepted,
                                            previewFont,
                                            this,
                                            trLang(QStringLiteral("选择界面字体"), QStringLiteral("Select Interface Font")));
    if(!accepted){
        return;
    }
    applyAppFont(next.family(), next.pointSize());
    persistAppearanceSettings();
}
