QT += widgets
CONFIG += c++17

TARGET = xor-file-processor
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/FileProcessor.cpp

HEADERS += \
    src/MainWindow.h \
    src/FileProcessor.h
