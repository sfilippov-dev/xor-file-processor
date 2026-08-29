#include <QApplication>
#include "MainWindow.h"
#include "FileProcessor.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("XOR File Processor"));

    // ProcessTask передаётся между потоками через очередь сигналов —
    // тип нужно зарегистрировать в системе метатипов Qt.
    qRegisterMetaType<ProcessTask>("ProcessTask");

    MainWindow window;
    window.show();
    return app.exec();
}
