#include "util/CpuFeatures.h"
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include "MainWindow.h"
#include "DeckLinkProbe.h"

int main(int argc, char* argv[])
{
    Q_INIT_RESOURCE(OpenScope);

    QApplication app(argc, argv);

    const QIcon openScopeIcon(
        QStringLiteral(":/branding/OpenScopeLogo.png"));

    app.setWindowIcon(openScopeIcon);
    if (!CpuFeatures::supportsAvx2Fma())
    {
        QMessageBox::critical(
            nullptr,
            "Unsupported CPU",
            "OpenScope requires a CPU with AVX2 and FMA support.");

        return 1;
    }
    MainWindow window;
    window.setWindowIcon(openScopeIcon);
    window.show();

    window.setBlackmagicDeviceName(
        deckLinkProbe(window.videoEngine()));

    return app.exec();
}