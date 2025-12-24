#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QtPlugin>

Q_IMPORT_PLUGIN(QMYSQLDriverPlugin)

int main(int argc, char *argv[])
{

    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);

    QCoreApplication::setApplicationName("OpenDBKit");
    QCoreApplication::setApplicationVersion(VERSION_STR);
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::applicationName());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "The file to open.");
    parser.process(app);

    if(!parser.positionalArguments().isEmpty()){
        QString filename = parser.positionalArguments().first();
        QObject::connect(MainWindow::instance(), &MainWindow::inited, [=]() {
            MainWindow::instance()->loadFile(filename);
        });
    }
    MainWindow::instance()->show();

    return app.exec();
}
