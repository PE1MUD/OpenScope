#include <QApplication>
#include "MainWindow.h"
#include "DeckLinkProbe.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    deckLinkProbe();

    MainWindow window;
    window.show();
    return app.exec();
}