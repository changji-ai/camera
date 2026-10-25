#include <QApplication>
#include <cstdio>

#include "MainWindow.h"
#include "YoloDetector.h"

int main(int argc, char *argv[])
{
    // Fast path for CI verification: print version without needing a display.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--version") == 0 || qstrcmp(argv[i], "-v") == 0) {
            std::printf("camera %s\n", CAMERA_VERSION);
            return 0;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("camera"));
    QApplication::setOrganizationName(QStringLiteral("changji-ai"));
    QApplication::setApplicationVersion(QString::fromLatin1(CAMERA_VERSION));
    qRegisterMetaType<std::vector<Detection>>();

    MainWindow w;
    w.show();
    return app.exec();
}
