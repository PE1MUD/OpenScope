#include <QApplication>

#include "MainWindow.h"
#include "DeckLinkProbe.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    deckLinkProbe(window.videoWidget());

    return app.exec();
}