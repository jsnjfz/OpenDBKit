#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "contentwidget.h"
#include "languagemanager.h"

#include <QAction>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSessionManager>
#include <QToolBar>

class MainWindow : public QMainWindow
{
    Q_OBJECT
private:
    MainWindow();

public:
    static MainWindow *instance();

    ContentWidget *getContent() const;
    QueryForm *loadFile(const QString &fileName, const QByteArray &codec = QByteArray());
    void setCurrentTitle(const QString &fileName);
    bool save();

    void setStatus(const QString &text, int timeout = 0);
    void clearStatusLabels();

    QAction *saveAct = nullptr;
    QLabel statusLabel;
    QLabel sqlLabel;
    QLabel codecLabel;
    QLabel resultInfoLabel;

    QString defaultLanguage = QStringLiteral("zh");
    QString defaultFontFamily;
    int defaultFontSize = 12;

    bool defaultUseCodeCompletion = true;
    bool defaultShowLineNumber = true;
    bool defaultUseSyntaxHighlighting = true;
    bool defaultUseCurrLineHL = true;

    void loadQSS();
    QRect getGuiFontRect(const QString &text, const QWidget *wid = nullptr) const;
    QRect getCharGuiFontRect() const;

signals:
    void inited();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newFile();
    void open();
    bool saveAs();
    void about();
    void adjustInterfaceFont();
#ifndef QT_NO_SESSIONMANAGER
    void commitData(QSessionManager &);
#endif

private:
    void createActions();
    void updateActionTexts();
    void setupLanguageMenu();
    void updateLanguageSelection(LanguageManager::Language lang);
    void persistLanguageSetting(LanguageManager::Language lang) const;
    void loadLanguageSetting();
    void createStatusBar();
    void readSettings();
    void writeSettings();
    void loadAppearanceSettings();
    void applyAppFont(const QString &family, int size);
    void persistAppearanceSettings() const;
    bool maybeSave();
    bool saveFile(const QString &fileName, const QByteArray &codec = QByteArray());
    QString strippedName(const QString &fullFileName) const;

    ContentWidget *content = nullptr;
    QToolBar *fileToolBar = nullptr;
    QMenu *fileMenu = nullptr;
    QMenu *connMenu = nullptr;
    QMenu *viewMenu = nullptr;
    QMenu *toolsMenu = nullptr;
    QMenu *languageMenu = nullptr;
    QMenu *helpMenu = nullptr;

    QAction *newAct = nullptr;
    QAction *openAct = nullptr;
    QAction *saveAsAct = nullptr;
    QAction *exitAct = nullptr;
    QAction *newConnAct = nullptr;
    QAction *fontAct = nullptr;
    QAction *dataSyncAct = nullptr;
    QAction *syncToolAct = nullptr;
    QAction *aboutAct = nullptr;
    QAction *languageChineseAct = nullptr;
    QAction *languageEnglishAct = nullptr;
};

#endif // MAINWINDOW_H
