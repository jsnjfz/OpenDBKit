#-------------------------------------------------
#
# Project created by QtCreator 2019-07-29T11:12:16
#
#-------------------------------------------------

QT       += core gui printsupport network sql sql-private core-private gui-private svg

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = opendbkit
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++17

!win32{
    QMAKE_CXXFLAGS += -Wno-narrowing
}

CONFIG(release, debug|release) {
}

VERSION = 0.1.0
DEFINES += VERSION_STR=\\\"$$VERSION\\\"

QMAKE_TARGET_PRODUCT = OpenDBKit
QMAKE_TARGET_COMPANY = OpenDBKit

win32{
    RC_ICONS =
}
macx{
    ICON = $$PWD/images/icon/opendbkit.icns
}

CONFIG -= debug_and_release

TRANSLATIONS += language/en.ts \
                language/zh_cn.ts

#include(cryptopp.pri)

SOURCES += \
        connectionmanager.cpp \
        conndialog.cpp \
        contentwidget.cpp \
        datasyncdialog.cpp \
        tabledesignerdialog.cpp \
        importdialog.cpp \
        exportdialog.cpp \
        flowlayout.cpp \
        languagemanager.cpp \
        leftwidgetform.cpp \
        main.cpp \
        mainwindow.cpp \
        myedit.cpp \
        mytreewidget.cpp \
        queryform.cpp \
        resultform.cpp \
        $$PWD/plugins/sqldrivers/mysql/mysql_plugin_main.cpp \
        $$PWD/plugins/sqldrivers/mysql/qsql_mysql.cpp

HEADERS += \
        connectionmanager.h \
        conndialog.h \
        contentwidget.h \
        datasyncdialog.h \
        tabledesignerdialog.h \
        importdialog.h \
        exportdialog.h \
        flowlayout.h \
        languagemanager.h \
        leftwidgetform.h \
        mainwindow.h \
        myedit.h \
        mytreewidget.h \
        queryform.h \
        resultform.h \
        $$PWD/plugins/sqldrivers/mysql/qsql_mysql_p.h

RESOURCES = resources.qrc

#INCLUDEPATH += $$(BOOST_HOME)
#!build_pass:message($$(BOOST_HOME))

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
        $$PWD/plugins/sqldrivers/mysql/mysql.json \
        $$PWD/plugins/sqldrivers/mysql/LICENSE.LGPL3

INCLUDEPATH += $$PWD/../third_party/mysql57/include
LIBS += -L$$PWD/../third_party/mysql57 -lmysql

DEFINES += QT_STATICPLUGIN QT_PLUGIN

FORMS +=
