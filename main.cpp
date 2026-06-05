#include <QApplication>
#include <QByteArray>
#include <QMessageBox>

#include "src/DebugLogger.h"
#include "src/MainWindow.h"

int main(int argc, char** argv)
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    // OpenCascade Xw_Window needs a real X11 window handle.
    // Force Qt to use the XCB backend instead of Wayland to avoid:
    // X Error: BadWindow / X_GetWindowAttributes.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    }
#endif

    QApplication app(argc, argv);
    DebugLogger::initialize(app);

    try {
        DebugLogger::logInfo("Creation de la fenetre principale");
        MainWindow w;
        w.show();
        DebugLogger::logInfo("Boucle Qt lancee");
        const int result = app.exec();
        DebugLogger::logInfo(QString("Application terminee avec code %1").arg(result));
        DebugLogger::shutdown();
        return result;
    } catch (const std::exception& e) {
        DebugLogger::logError(QString("Unhandled exception dans main(): %1").arg(e.what()));
        QMessageBox::critical(nullptr, "Fatal error", QString("Unhandled exception:\n%1\n\nLog:\n%2").arg(e.what(), DebugLogger::logFilePath()));
        DebugLogger::shutdown();
        return 1;
    } catch (...) {
        DebugLogger::logError("Exception inconnue non geree dans main().");
        QMessageBox::critical(nullptr, "Fatal error", QString("Unknown exception.\n\nLog:\n%1").arg(DebugLogger::logFilePath()));
        DebugLogger::shutdown();
        return 1;
    }
}
