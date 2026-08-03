#include <QApplication>
#include <QCoreApplication>
#include <QMainWindow>

int main(int argc, char* argv[])
{

    QApplication application(argc, argv);

    QMainWindow window;
    window.setWindowTitle("OpenScope");
    window.resize(1280, 720);
    window.show();

    return application.exec();
}