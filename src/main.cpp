#include <QApplication>
#include <QFile>
#include <QFont>
#include <QIcon>
#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");

    app.setApplicationName("Support IL");
    app.setOrganizationName("SupportIL");
    app.setWindowIcon(QIcon(":/ico.ico"));

    QFont font("Segoe UI", 10);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    QFile f(":/style.qss");
    if (f.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(f.readAll()));
    }

    MainWindow w;
    w.show();
    return app.exec();
}
