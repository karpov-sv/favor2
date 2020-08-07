#-------------------------------------------------
#
# Project created by QtCreator 2012-08-29T10:21:20
#
#-------------------------------------------------

QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = gui-hw
TEMPLATE = app

CONFIG += debug

SOURCES += main.cpp \
        mainwindow.cpp \
        utils.c \
        command.c

HEADERS  += mainwindow.h \
         utils.h \
         command.h

FORMS    += mainwindow.ui
