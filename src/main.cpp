#include <QApplication>
#include <QFile>
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFile f(":/style.qss");
    if (f.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(f.readAll()));
    MainWindow w;
    w.show();
    return app.exec();
}