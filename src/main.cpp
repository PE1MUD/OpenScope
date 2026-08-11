#include "util/CpuFeatures.h"
#include <QApplication>
#include <QMessageBox>
#include "MainWindow.h"
#include "DeckLinkProbe.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    if (!CpuFeatures::supportsAvx2Fma())
    {
        QMessageBox::critical(
            nullptr,
            "Unsupported CPU",
            "OpenScope requires a CPU with AVX2 and FMA support.");

        return 1;
    }
    MainWindow window;
    window.show();

    deckLinkProbe(window.videoEngine());

    return app.exec();
}